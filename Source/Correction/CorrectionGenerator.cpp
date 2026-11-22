#include "CorrectionGenerator.h"

#include <algorithm>
#include <cmath>

namespace eyra::correction
{

//==============================================================================
juce::StringArray strengthNames() { return { "Gentle", "Moderate", "Aggressive" }; }
juce::StringArray modeNames() { return { "Transparent", "Polished" }; }

juce::ValueTree Settings::toValueTree() const
{
    juce::ValueTree t ("Correction");
    t.setProperty ("instrument", (int) instrument, nullptr);
    t.setProperty ("strength", (int) strength, nullptr);
    t.setProperty ("mode", (int) mode, nullptr);
    t.setProperty ("amount", amount, nullptr);
    t.setProperty ("maxCorrections", maxCorrections, nullptr);
    t.setProperty ("preserveCharacter", preserveCharacter, nullptr);
    t.setProperty ("autoHighPass", autoHighPass, nullptr);
    t.setProperty ("applyAutomatically", applyAutomatically, nullptr);
    return t;
}

Settings Settings::fromValueTree (const juce::ValueTree& t)
{
    Settings s;
    if (! t.isValid())
        return s;
    s.instrument = (Instrument) juce::jlimit (0, (int) Instrument::NumInstruments - 1, (int) t.getProperty ("instrument", 0));
    s.strength = (Strength) juce::jlimit (0, 2, (int) t.getProperty ("strength", (int) s.strength));
    s.mode = (Mode) juce::jlimit (0, 1, (int) t.getProperty ("mode", (int) s.mode));
    s.amount = juce::jlimit (0.0, 1.5, (double) t.getProperty ("amount", s.amount));
    s.maxCorrections = juce::jlimit (1, 12, (int) t.getProperty ("maxCorrections", s.maxCorrections));
    s.preserveCharacter = (bool) t.getProperty ("preserveCharacter", s.preserveCharacter);
    s.autoHighPass = (bool) t.getProperty ("autoHighPass", s.autoHighPass);
    s.applyAutomatically = (bool) t.getProperty ("applyAutomatically", s.applyAutomatically);
    return s;
}

//==============================================================================
namespace
{
    juce::String hz (double f) { return analysis::describeFrequency (f); }
    juce::String db (double g) { return juce::String (std::abs (g), 1) + " dB"; }

    dsp::BandParams makeBand (dsp::FilterType type, double frequency, double gainDb, double q, int slopeIndex = dsp::kDefaultSlopeIndex)
    {
        dsp::BandParams b;
        b.used = true;
        b.type = type;
        b.frequency = juce::jlimit (20.0, 20000.0, frequency);
        b.gainDb = gainDb;
        b.q = q;
        b.slopeIndex = slopeIndex;
        return b;
    }

    int slopeIndexFor (int dbPerOct)
    {
        for (size_t i = 0; i < dsp::kSlopesDbPerOct.size(); ++i)
            if (dsp::kSlopesDbPerOct[i] == dbPerOct)
                return (int) i;
        return dsp::kDefaultSlopeIndex;
    }

    /** Q of a bell spanning `octaves`. */
    double qForBandwidth (double octaves)
    {
        const double n = std::pow (2.0, std::max (octaves, 0.1));
        return std::sqrt (n) / (n - 1.0);
    }

    double octavesBetween (double a, double b) { return std::abs (std::log2 (b / a)); }

    struct Tuning
    {
        double scale;       // fraction of the measured excess that is removed
        double capBroad;    // largest broad cut (dB)
        double capNarrow;   // largest narrow cut (dB)
        double minSeverity; // findings below this are left alone
        double polish;      // scale of Polished enhancements
    };

    Tuning tuningFor (Strength s)
    {
        switch (s)
        {
            case Strength::Gentle:     return { 0.45, 3.0, 5.0, 0.30, 0.7 };
            case Strength::Moderate:   return { 0.65, 5.0, 8.0, 0.18, 1.0 };
            case Strength::Aggressive: return { 0.85, 8.0, 12.0, 0.08, 1.3 };
        }
        return { 0.65, 5.0, 8.0, 0.18, 1.0 };
    }

    /** The character region a finding mostly lies in (null if none). */
    const CharacterRegion* characterRegionFor (const std::vector<CharacterRegion>& regions, double lo, double hi)
    {
        const double width = std::max (octavesBetween (lo, hi), 1.0e-3);
        for (const auto& r : regions)
        {
            const double overlapLo = std::max (lo, r.lowHz), overlapHi = std::min (hi, r.highHz);
            if (overlapHi <= overlapLo)
                continue;
            if (hi <= lo * 1.0001 || octavesBetween (overlapLo, overlapHi) >= 0.5 * width)
                return &r;
        }
        return nullptr;
    }
} // namespace

juce::String describeBand (const dsp::BandParams& b)
{
    using T = dsp::FilterType;
    switch (b.type)
    {
        case T::HighPass:  return "High-pass at " + hz (b.frequency) + ", " + juce::String (dsp::kSlopesDbPerOct[(size_t) b.slopeIndex]) + " dB/oct";
        case T::LowPass:   return "Low-pass at " + hz (b.frequency) + ", " + juce::String (dsp::kSlopesDbPerOct[(size_t) b.slopeIndex]) + " dB/oct";
        case T::LowShelf:  return juce::String (b.gainDb < 0 ? "Low shelf cut " : "Low shelf boost ") + db (b.gainDb) + " below " + hz (b.frequency);
        case T::HighShelf: return juce::String (b.gainDb < 0 ? "High shelf cut " : "High shelf boost ") + db (b.gainDb) + " above " + hz (b.frequency);
        case T::Bell: case T::Notch: case T::BandPass: case T::Tilt: break;
    }
    return juce::String (b.gainDb < 0 ? "Cut " : "Boost ") + db (b.gainDb) + " at " + hz (b.frequency) + ", Q " + juce::String (b.q, b.q < 10 ? 1 : 0);
}

double targetTolerance (Strength s)
{
    switch (s)
    {
        case Strength::Gentle:     return 6.0;
        case Strength::Moderate:   return 4.5;
        case Strength::Aggressive: return 3.5;
    }
    return 4.5;
}

TargetComparison compareToTarget (const analysis::AnalysisResult& r, Instrument instrument, Strength strength)
{
    TargetComparison c;
    const auto& profile = profileFor (instrument);
    if (r.status != analysis::AnalysisResult::Status::Ok || profile.target.empty() || r.ltasDb.size() < 2
        || r.displayFrequencies.size() != r.ltasDb.size())
        return c;

    // Measured level per 1/3 octave: the per-bin spectrum + 3 dB/octave.
    auto measuredAt = [&] (double f) -> double
    {
        const auto& F = r.displayFrequencies;
        if (f <= F.front() || f >= F.back())
            return -200.0;
        const auto it = std::upper_bound (F.begin(), F.end(), f);
        const size_t i = (size_t) (it - F.begin());
        const double t = std::log (f / F[i - 1]) / std::log (F[i] / F[i - 1]);
        const double a = r.ltasDb[i - 1], b = r.ltasDb[i];
        if (a <= -139.0 || b <= -139.0)
            return -200.0;
        return a + t * (b - a) + 10.0 * std::log10 (f / 1000.0);
    };
    auto baseTargetAt = [&] (double f)
    {
        const auto& P = profile.target;
        if (f <= P.front().first)
            return P.front().second;
        if (f >= P.back().first)
            return P.back().second;
        for (size_t i = 1; i < P.size(); ++i)
            if (f <= P[i].first)
            {
                const double t = std::log (f / P[i - 1].first) / std::log (P[i].first / P[i - 1].first);
                return P[i - 1].second + t * (P[i].second - P[i - 1].second);
            }
        return P.back().second;
    };

    // Centre of the main low peak: level-weighted mean frequency of the points within 1.5 dB of the
    // highest level, searched in [lo, hi].
    auto peakCentre = [] (auto levelAt, double lo, double hi)
    {
        double best = -1.0e9;
        for (double f = lo; f <= hi; f *= std::pow (2.0, 1.0 / 24.0))
            best = std::max (best, levelAt (f));
        double sum = 0.0, weight = 0.0;
        for (double f = lo; f <= hi; f *= std::pow (2.0, 1.0 / 24.0))
            if (const double l = levelAt (f); l >= best - 1.5)
            {
                const double w = std::pow (10.0, (l - best) / 10.0);
                sum += w * std::log (f);
                weight += w;
            }
        return weight > 0.0 ? std::exp (sum / weight) : 0.0;
    };

    // Typical peak: the curve's highest region below 2 kHz.
    c.typicalPeakHz = peakCentre (baseTargetAt, std::max (20.0, profile.fitLowHz * 0.7), 2000.0);
    c.measuredPeakHz = c.typicalPeakHz;
    double ratio = 1.0;
    if (profile.followsFundamental && c.typicalPeakHz > 0.0)
    {
        const double lo = std::max (20.0, c.typicalPeakHz / 2.0), hi = c.typicalPeakHz * 2.0;
        const double measured = peakCentre ([&] (double f) { const double m = measuredAt (f); return m <= -150.0 ? -1.0e9 : m; }, lo, hi);
        if (measured > 0.0)
        {
            ratio = juce::jlimit (0.5, 2.0, measured / c.typicalPeakHz);
            c.measuredPeakHz = c.typicalPeakHz * ratio;
        }
    }

    // The low part follows the measured peak; from 3x to 8x the peak it blends back to the typical curve.
    auto targetAt = [&] (double f)
    {
        if (std::abs (ratio - 1.0) < 1.0e-6)
            return baseTargetAt (f);
        const double p = c.measuredPeakHz;
        double shift = ratio;
        if (f > 8.0 * p)
            shift = 1.0;
        else if (f > 3.0 * p)
            shift = std::pow (ratio, 1.0 - std::log (f / (3.0 * p)) / std::log (8.0 / 3.0));
        return baseTargetAt (f / shift);
    };

    const double hi = std::min (profile.fitHighHz, std::min (20000.0, 0.45 * r.sampleRate));
    for (double f = profile.fitLowHz; f <= hi; f *= std::pow (2.0, 1.0 / 12.0))
    {
        const double m = measuredAt (f);
        if (m <= -150.0)
            continue;
        c.frequencies.push_back (f);
        c.measuredDb.push_back (m);
        c.targetDb.push_back (targetAt (f));
    }
    if (c.frequencies.size() < 24)
        return c;

    // Align the target with the measurement: median difference, so a few strong regions do not pull it.
    std::vector<double> diff (c.frequencies.size());
    for (size_t i = 0; i < diff.size(); ++i)
        diff[i] = c.measuredDb[i] - c.targetDb[i];
    auto sorted = diff;
    std::nth_element (sorted.begin(), sorted.begin() + (ptrdiff_t) (sorted.size() / 2), sorted.end());
    const double offset = sorted[sorted.size() / 2];
    for (auto& t : c.targetDb)
        t += offset;

    // Deviation, smoothed over 1/3 octave (+-2 grid points): narrow peaks are the resonance detector's job.
    c.deviationDb.resize (diff.size());
    for (size_t i = 0; i < diff.size(); ++i)
    {
        double sum = 0.0;
        int n = 0;
        for (int k = -2; k <= 2; ++k)
        {
            const auto j = (ptrdiff_t) i + k;
            if (j >= 0 && j < (ptrdiff_t) diff.size())
                sum += diff[(size_t) j] - offset, ++n;
        }
        c.deviationDb[i] = sum / n;
    }
    c.toleranceDb = targetTolerance (strength);
    c.valid = true;
    return c;
}

std::vector<Suggestion> generateCorrections (const analysis::AnalysisResult& r, const Settings& s)
{
    using FT = analysis::FindingType;
    using K = Suggestion::Kind;

    std::vector<Suggestion> out;
    if (r.status != analysis::AnalysisResult::Status::Ok)
        return out;

    const auto& profile = profileFor (s.instrument);
    const auto tuning = tuningFor (s.strength);
    const double amount = juce::jlimit (0.0, 1.5, s.amount);
    const juce::String instrument = s.instrument == Instrument::General ? juce::String ("this source") : "a " + instrumentName (s.instrument).toLowerCase();

    // Regions that belong to the instrument's sound: the profile's, plus the measured note range.
    auto character = profile.character;
    if (profile.protectFundamental && r.pitch.pitched && r.pitch.lowHz > 0.0)
        character.push_back ({ r.pitch.lowHz * 0.9, r.pitch.highHz * 1.1, "range of the played notes" });

    // The highest a high-pass may go without thinning the notes. A rumble high-pass is placed from the
    // measured rumble itself; an instrument high-pass also stays under the lowest measured content.
    double notesLimit = 20000.0;
    if (r.pitch.pitched && r.pitch.lowHz > 0.0)
        notesLimit = r.pitch.lowHz * 0.7;
    const double contentLimit = r.lowestContentHz > 0.0 ? std::min (notesLimit, r.lowestContentHz * 0.8) : notesLimit;

    auto leftAlone = [&] (int index, const juce::String& title, const juce::String& reason)
    {
        Suggestion sg;
        sg.kind = K::LeftAlone;
        sg.findingIndex = index;
        sg.title = title;
        sg.reason = reason;
        out.push_back (sg);
    };

    const auto comparison = compareToTarget (r, s.instrument, s.strength);
    std::vector<int> broadJudgedByTarget;

    std::optional<Suggestion> highPass;
    auto offerHighPass = [&] (double frequency, double limit, int slope, int index, const juce::String& reason, double priority)
    {
        frequency = std::min (frequency, limit);
        if (frequency < 20.0)
            return false;
        if (highPass && highPass->band->frequency >= frequency)
            return true; // an existing, higher high-pass already covers it
        Suggestion sg;
        sg.kind = K::HighPass;
        sg.band = makeBand (dsp::FilterType::HighPass, frequency, 0.0, 0.7071, slopeIndexFor (slope));
        sg.findingIndex = index;
        sg.title = describeBand (*sg.band);
        sg.reason = reason;
        sg.priority = priority;
        highPass = sg;
        return true;
    };


    // Narrow peaks that form a harmonic series are the notes of a sustained, often polyphonic, source (strings, pads,
    // organ, chords): they are the sound itself, not resonances. A common fundamental must explain at least four of the peaks and most of them.
    std::vector<bool> isNoteHarmonic (r.findings.size(), false);
    std::vector<double> noteFundamentals;
    {
        std::vector<size_t> narrow;
        for (size_t i = 0; i < r.findings.size(); ++i)
            if (r.findings[i].type == FT::Resonance || r.findings[i].type == FT::IntermittentResonance)
                narrow.push_back (i);
        std::sort (narrow.begin(), narrow.end(), [&] (size_t a, size_t b) { return r.findings[a].frequency < r.findings[b].frequency; });

        auto harmonicNumber = [] (double f, double f0, int maxN)
        {
            const int n = (int) std::lround (f / f0);
            return (n >= 1 && n <= maxN && std::abs (f - n * f0) <= 0.01 * f) ? n : 0;
        };
        std::vector<bool> used (r.findings.size(), false);
        for (int pass = 0; pass < 3; ++pass)
        {
            double bestF0 = 0.0;
            int bestCore = 0, bestTotal = 0;
            for (size_t a = 0; a < std::min<size_t> (narrow.size(), 6); ++a)
                for (int n = 1; n <= 4; ++n)
                {
                    const double f0 = r.findings[narrow[a]].frequency / n;
                    if (f0 < 55.0)
                        continue;
                    int core = 0, total = 0;
                    for (auto j : narrow)
                        if (! used[j])
                        {
                            core += harmonicNumber (r.findings[j].frequency, f0, 16) > 0 ? 1 : 0;
                            total += harmonicNumber (r.findings[j].frequency, f0, 80) > 0 ? 1 : 0;
                        }
                    if (core >= 4 && core * 2 >= (int) narrow.size() && (total > bestTotal || (total == bestTotal && f0 > bestF0)))
                        bestCore = core, bestTotal = total, bestF0 = f0;
                }
            if (bestCore < 4 || bestTotal * 10 < (int) narrow.size() * 6)
                break;
            for (auto j : narrow)
                if (! used[j] && harmonicNumber (r.findings[j].frequency, bestF0, 80) > 0)
                    used[j] = isNoteHarmonic[j] = true;
            noteFundamentals.push_back (bestF0);
        }
    }

    // --- One suggestion per finding ---------------------------------------------------------
    for (int i = 0; i < (int) r.findings.size(); ++i)
    {
        const auto& f = r.findings[(size_t) i];
        const double priority = f.severity * (0.5 + 0.5 * f.confidence);

        if (f.type == FT::Rumble || f.type == FT::DCOffset)
        {
            const double freq = f.type == FT::Rumble ? f.highFrequency * 0.8 : 20.0;
            if (! offerHighPass (freq, notesLimit, f.type == FT::Rumble ? 18 : 12, i,
                                 f.type == FT::Rumble ? "Removes the energy measured below the musical content."
                                                      : "Removes the constant offset (DC) in the signal.",
                                 2.0 + priority))
                leftAlone (i, "No high-pass", "The notes reach too low to filter this without thinning them.");
            continue;
        }

        // With a typical tonal balance for the instrument, broad balance is judged against it instead.
        if (comparison.valid && ! f.isNarrow())
        {
            broadJudgedByTarget.push_back (i);
            continue;
        }

        if (f.severity < tuning.minSeverity)
        {
            leftAlone (i, "Left alone", "Minor: below what " + strengthNames()[(int) s.strength] + " strength corrects.");
            continue;
        }

        Suggestion sg;
        sg.kind = K::Cut;
        sg.findingIndex = i;
        sg.priority = priority;

        if (f.isNarrow())
        {
            if (f.type != FT::Hum && comparison.valid && s.preserveCharacter)
            {
                // Narrow peaks only matter where the instrument has real energy.
                double peakTarget = -1.0e9, here = -1.0e9;
                for (size_t k = 0; k < comparison.frequencies.size(); ++k)
                {
                    peakTarget = std::max (peakTarget, comparison.targetDb[k]);
                    if (octavesBetween (comparison.frequencies[k], f.frequency) < 1.0 / 24.0)
                        here = comparison.targetDb[k];
                }
                if (! comparison.frequencies.empty() && f.frequency > comparison.frequencies.back())
                    here = comparison.targetDb.back();   // above the compared range: at most as loud as its top
                else if (! comparison.frequencies.empty() && f.frequency < comparison.frequencies.front())
                    here = comparison.targetDb.front();
                if (here > -1.0e8 && here < peakTarget - 18.0)
                {
                    leftAlone (i, "Left alone", "This part of the spectrum is quiet for this instrument ("
                                                    + instrumentName (s.instrument).toLowerCase() + "); a narrow peak here is not worth a cut.");
                    continue;
                }
            }
            if (isNoteHarmonic[(size_t) i] && s.preserveCharacter && ! profile.drum)
            {
                leftAlone (i, "Left alone", "This peak belongs to a harmonic series (the notes and their overtones, around "
                                            + hz (noteFundamentals.front()) + "), which is the sound of the instrument, not a resonance.");
                continue;
            }
            if (profile.drum && s.preserveCharacter && (f.type == FT::Resonance || f.type == FT::IntermittentResonance))
            {
                leftAlone (i, "Left alone", "Drum heads and shells ring at their own frequencies; these tones are part of the drum's sound. "
                                            "Only ringing that lasts much longer than the rest of the hit is cut.");
                continue;
            }
            if (f.type != FT::Hum && profile.protectFundamental && r.pitch.pitched && r.pitch.medianHz > 0.0
                && octavesBetween (f.frequency, r.pitch.medianHz) < 0.06 && s.preserveCharacter)
            {
                leftAlone (i, "Left alone", "It sits on the fundamental of the played note, which is part of the sound.");
                continue;
            }

            double cut = std::min (tuning.capNarrow, f.excessDb * tuning.scale);
            double q = f.q > 0.0 ? f.q : 6.0;
            if (f.type == FT::Hum)
            {
                cut = std::min (tuning.capNarrow, std::max (3.0, f.excessDb * (tuning.scale + 0.2)));
                q = std::max (q, 10.0);
                sg.reason = "Mains hum is never part of the sound; a narrow cut removes it with little effect on the rest.";
            }
            else if (f.type == FT::Ringing)
            {
                q = juce::jlimit (2.0, 10.0, q);
                sg.reason = "This frequency keeps ringing after each hit; a cut shortens the ring.";
            }
            else if (f.type == FT::IntermittentResonance)
            {
                cut *= 0.6;
                q = juce::jlimit (2.0, 16.0, q);
                sg.reason = "Present only part of the time, so the cut is kept lighter (a dynamic band would follow it better).";
            }
            else
            {
                q = juce::jlimit (2.0, 16.0, q);
                sg.reason = "A narrow cut, so the rest of the sound is untouched.";
            }

            if (s.instrument != Instrument::General && f.type != FT::Hum)
            {
                // On a single instrument, narrow cuts change its tone quickly: smaller and less surgical.
                cut *= 0.75;
                q = std::min (q, 10.0);
            }
            cut *= amount;
            if (cut < 0.5)
            {
                leftAlone (i, "Left alone", "The cut would be under 0.5 dB at this amount.");
                continue;
            }
            sg.band = makeBand (dsp::FilterType::Bell, f.frequency, -cut, q);
        }
        else // broad buildup
        {
            const double lo = std::max (20.0, f.lowFrequency), hi = std::max (lo * 1.05, f.highFrequency);
            double cut = std::min (tuning.capBroad, f.excessDb * tuning.scale);
            sg.reason = "A broad, gentle cut over the region, removing part of the excess.";

            if (s.preserveCharacter)
                if (const auto* region = characterRegionFor (character, lo, hi))
                {
                    if (f.excessDb < 4.0)
                    {
                        leftAlone (i, "Left alone", "This is the " + region->what + ", part of " + instrument
                                                        + "'s sound, and only " + juce::String (f.excessDb, 1) + " dB above its surroundings.");
                        continue;
                    }
                    cut *= 0.5;
                    sg.reason = "Half the usual cut, because this is the " + region->what + " (Preserve character).";
                }

            cut *= amount;
            if (cut < 0.5)
            {
                leftAlone (i, "Left alone", "The cut would be under 0.5 dB at this amount.");
                continue;
            }

            if (lo >= 7000.0)
                sg.band = makeBand (dsp::FilterType::HighShelf, lo, -cut, 0.7071);
            else if (hi <= 120.0)
                sg.band = makeBand (dsp::FilterType::LowShelf, hi, -cut, 0.7071);
            else
                sg.band = makeBand (dsp::FilterType::Bell, f.frequency > 0.0 ? f.frequency : std::sqrt (lo * hi), -cut,
                                    juce::jlimit (0.5, 3.0, qForBandwidth (octavesBetween (lo, hi))));
        }

        sg.title = describeBand (*sg.band);
        out.push_back (sg);
    }


    // --- Tonal balance compared with the instrument's typical balance ---------------------------
    struct Region { double lo, hi; };
    std::vector<Region> targetCuts;
    if (comparison.valid)
    {
        const auto& F = comparison.frequencies;
        const auto& D = comparison.deviationDb;
        const double tol = comparison.toleranceDb;
        const juce::String name = instrumentName (s.instrument).toLowerCase();

        auto zoneName = [&] (double hz)
        {
            for (const auto& z : profile.zones)
                if (hz >= z.lowHz && hz < z.highHz)
                    return z.what;
            return juce::String ("balance");
        };

        auto scan = [&] (bool excess)
        {
            size_t i = 0;
            while (i < F.size())
            {
                const bool outside = excess ? D[i] > tol : D[i] < -(tol + 1.0);
                if (! outside)
                {
                    ++i;
                    continue;
                }
                size_t j = i, peak = i;
                while (j < F.size() && (excess ? D[j] > tol : D[j] < -(tol + 1.0)))
                {
                    if (std::abs (D[j]) > std::abs (D[peak]))
                        peak = j;
                    ++j;
                }
                const double lo = F[i], hi = F[j - 1], width = octavesBetween (lo, hi);
                const bool atBottom = i == 0, atTop = j == F.size();
                i = j;
                if (width < (excess ? 1.0 / 3.0 : 0.5))
                    continue;

                const double peakHz = F[peak], over = std::abs (D[peak]);
                Suggestion sg;
                sg.heading = juce::String (zoneName (peakHz)).substring (0, 1).toUpperCase() + juce::String (zoneName (peakHz)).substring (1);
                if (excess)
                {
                    double cut = std::min (tuning.capBroad, (over - 0.5 * tol) * tuning.scale);
                    sg.reason = "Around " + hz (peakHz) + " this track is " + juce::String (over, 1) + " dB above the typical "
                              + name + " balance (allowed: " + juce::String (tol, 1) + " dB).";
                    if (s.preserveCharacter && profile.followsFundamental
                        && octavesBetween (peakHz, comparison.measuredPeakHz) < 0.5)
                    {
                        leftAlone (-1, "Left alone", "Around " + hz (peakHz) + " is this track's main low peak (its note or body), "
                                                     "which makes up the instrument's sound. Turn off Preserve character to allow a cut here.");
                        out.back().heading = sg.heading;
                        out.back().optionalBand = makeBand (dsp::FilterType::Bell, peakHz, -std::max (1.0, cut * amount),
                                                            juce::jlimit (0.5, 3.0, qForBandwidth (width)));
                        continue;
                    }
                    if (s.preserveCharacter)
                        if (const auto* region = characterRegionFor (character, lo, hi))
                        {
                            cut = std::min (cut * 0.6, 3.0);
                            sg.reason += " Cut kept small: this is the " + region->what + ".";
                        }
                    cut *= amount;
                    if (cut < 0.5)
                        continue;
                    sg.kind = K::Cut;
                    if (atTop && lo >= 5000.0)
                        sg.band = makeBand (dsp::FilterType::HighShelf, lo, -cut, 0.7071);
                    else if (atBottom && hi <= 200.0)
                        sg.band = makeBand (dsp::FilterType::LowShelf, hi, -cut, 0.7071);
                    else
                        sg.band = makeBand (dsp::FilterType::Bell, peakHz, -cut, juce::jlimit (0.5, 3.0, qForBandwidth (width)));
                    sg.priority = 0.3 + 0.05 * over;
                    targetCuts.push_back ({ lo, hi });
                }
                else
                {
                    if (s.mode != Mode::Polished || atBottom || ! profile.moves.empty())
                        continue;
                    const double cap = 1.5 * tuning.polish + 0.5;
                    const double boost = std::min (cap, (over - tol) * 0.5) * amount;
                    if (boost < 0.3)
                        continue;
                    sg.kind = K::Enhancement;
                    sg.band = atTop && lo >= 5000.0 ? makeBand (dsp::FilterType::HighShelf, lo, boost, 0.7071)
                                                    : makeBand (dsp::FilterType::Bell, peakHz, boost, juce::jlimit (0.5, 2.0, qForBandwidth (width)));
                    sg.reason = "Polished: around " + hz (peakHz) + " this track is " + juce::String (over, 1)
                              + " dB below the typical " + name + " balance, so a little is added back.";
                    sg.priority = 0.02;
                }
                sg.title = describeBand (*sg.band);
                out.push_back (sg);
            }
        };
        scan (true);
        scan (false);

        for (int index : broadJudgedByTarget)
        {
            const auto& f = r.findings[(size_t) index];
            juce::String reason = "Normal for this instrument (" + name + "): within its typical tonal balance.";
            for (const auto& c : targetCuts)
                if (f.highFrequency >= c.lo && f.lowFrequency <= c.hi)
                    reason = "Handled by the " + name + " balance correction in this region.";
            leftAlone (index, "Left alone", reason);
        }
    }


    // --- The usual mixing moves for the instrument, placed and sized from the measurement -------
    if (! profile.moves.empty() && comparison.valid)
    {
        const auto& F = comparison.frequencies;
        const auto& D = comparison.deviationDb;
        const juce::String name = instrumentName (s.instrument).toLowerCase();
        auto deviationAt = [&] (double f)
        {
            size_t best = 0;
            for (size_t k = 1; k < F.size(); ++k)
                if (octavesBetween (F[k], f) < octavesBetween (F[best], f))
                    best = k;
            return D[best];
        };
        auto cutNear = [&] (double f, double octaves)
        {
            for (const auto& o : out)
                if (o.band && o.kind == K::Cut && octavesBetween (o.band->frequency, f) < octaves)
                    return true;
            return false;
        };

        for (const auto& m : profile.moves)
        {
            const bool boost = m.gainDb > 0.0;
            if (boost && s.mode != Mode::Polished)
                continue;

            // Where: the point in the range that most needs it (most excess for a cut, most lacking for a boost).
            double freq = m.lowHz;
            if (m.shape == TypicalMove::Shape::AtMainPeak)
                freq = comparison.measuredPeakHz;
            else if (m.shape == TypicalMove::Shape::Bell)
            {
                double bestScore = -1.0e9;
                freq = std::sqrt (m.lowHz * m.highHz);
                for (size_t k = 0; k < F.size(); ++k)
                    if (F[k] >= m.lowHz && F[k] <= m.highHz)
                        if (const double score = boost ? -D[k] : D[k]; score > bestScore)
                            bestScore = score, freq = F[k];
            }
            if (freq <= 0.0 || freq > 0.45 * r.sampleRate)
                continue;
            if (r.highestContentHz > 0.0 && boost && freq > r.highestContentHz)
                continue; // nothing there to bring out

            // How much: the usual amount, more where the track has too much (cut) or too little (boost).
            const double d = (m.shape == TypicalMove::Shape::HighShelf) ? deviationAt (freq * 1.3) : deviationAt (freq);
            double gain = boost ? m.gainDb - 0.5 * d : -(std::abs (m.gainDb) + 0.5 * d);
            gain = boost ? juce::jlimit (0.0, 1.5 * m.gainDb, gain) : juce::jlimit (-tuning.capBroad, 0.0, gain);
            gain *= tuning.polish * amount;

            Suggestion sg;
            sg.heading = juce::String (m.what).substring (0, 1).toUpperCase() + juce::String (m.what).substring (1);
            if (std::abs (gain) < (boost ? 0.5 : 1.0))
                continue; // the track does not need it
            if (cutNear (freq, 0.5))
                continue; // a measured problem cut already works here
            if (! boost && s.preserveCharacter && profile.followsFundamental && octavesBetween (freq, comparison.measuredPeakHz) < 0.5)
                continue;

            sg.kind = boost ? K::Enhancement : K::Cut;
            sg.band = m.shape == TypicalMove::Shape::HighShelf ? makeBand (dsp::FilterType::HighShelf, freq, gain, 0.7071)
                                                               : makeBand (dsp::FilterType::Bell, freq, gain, m.q);
            sg.title = describeBand (*sg.band);
            sg.reason = "A usual move for a " + name + " (" + m.what + "). "
                      + (std::abs (d) < 1.0 ? juce::String ("This track is close to the typical balance here, so the usual amount.")
                                            : "This track is " + juce::String (std::abs (d), 1) + " dB " + (d > 0 ? "above" : "below")
                                                  + " the typical balance here, so " + ((d > 0) != boost ? "more." : "less."));
            sg.priority = boost ? 0.05 : 0.25;
            out.push_back (sg);
        }
    }

    // --- High-pass for the instrument -------------------------------------------------------
    if (s.autoHighPass && profile.highPassHz > 0.0)
        offerHighPass (profile.highPassHz, contentLimit, 12, -1,
                       "Below the lowest useful content of " + instrument + "; clears room in the mix. Kept under the lowest measured content.",
                       1.5);
    if (highPass)
        out.push_back (*highPass);

    // --- Polished: subtle enhancement, only where the recording does not already have it ----
    if (s.mode == Mode::Polished && ! comparison.valid)
        for (const auto& e : profile.polish)
        {
            bool problemNearby = false;
            for (const auto& f : r.findings)
            {
                const double lo = f.isNarrow() ? f.frequency : f.lowFrequency, hi = f.isNarrow() ? f.frequency : f.highFrequency;
                if (e.shape == Enhancement::Shape::HighShelf ? hi >= e.frequency / 2.0
                                                             : (hi >= e.frequency / 1.6 && lo <= e.frequency * 1.6))
                    problemNearby = true;
            }
            if (problemNearby || (r.highestContentHz > 0.0 && r.highestContentHz < e.frequency))
                continue;

            const double gain = e.gainDb * tuning.polish * amount;
            if (gain < 0.3)
                continue;
            Suggestion sg;
            sg.kind = K::Enhancement;
            sg.band = e.shape == Enhancement::Shape::HighShelf ? makeBand (dsp::FilterType::HighShelf, e.frequency, gain, 0.7071)
                                                               : makeBand (dsp::FilterType::Bell, e.frequency, gain, e.q);
            sg.title = describeBand (*sg.band);
            sg.reason = "Polished: a little " + e.what + ". No problem was measured in this region.";
            sg.priority = 0.01;
            out.push_back (sg);
        }

    // --- Overlaps, limit, order ---------------------------------------------------------------
    std::stable_sort (out.begin(), out.end(), [] (const Suggestion& a, const Suggestion& b)
    {
        if (a.band.has_value() != b.band.has_value())
            return a.band.has_value();
        return a.priority > b.priority;
    });

    int kept = 0, narrowKept = 0;
    const int maxNarrow = s.instrument == Instrument::General ? 99 : 3;
    for (size_t i = 0; i < out.size(); ++i)
    {
        auto& sg = out[i];
        if (! sg.band)
            continue;
        if (sg.kind == K::Cut && sg.band->type == dsp::FilterType::Bell && sg.band->q >= 4.0 && ++narrowKept > maxNarrow)
        {
            sg.reason = "Only the " + juce::String (maxNarrow) + " strongest narrow cuts are used on a single instrument, to keep its tone: "
                      + sg.title.toLowerCase() + ".";
            sg.optionalBand = sg.band;
            sg.band.reset();
            sg.kind = K::LeftAlone;
            sg.title = "Not used";
            continue;
        }
        if (sg.kind == K::Cut)
            for (size_t j = 0; j < i; ++j)
                if (out[j].band && out[j].kind == K::Cut && out[j].band->type == sg.band->type
                    && octavesBetween (out[j].band->frequency, sg.band->frequency) < 1.0 / 6.0
                    && std::max (out[j].band->q, sg.band->q) < 2.0 * std::min (out[j].band->q, sg.band->q))
                {
                    sg.reason = "Already covered by the stronger cut at " + hz (out[j].band->frequency) + ".";
                    sg.optionalBand = sg.band;
                    sg.band.reset();
                    sg.kind = K::LeftAlone;
                    sg.title = "Left alone";
                    break;
                }
        if (! sg.band)
            continue;
        if (++kept > s.maxCorrections)
        {
            sg.reason = "Beyond the number of corrections (" + juce::String (s.maxCorrections) + "): " + sg.title.toLowerCase() + ".";
            sg.optionalBand = sg.band;
            sg.band.reset();
            sg.kind = K::LeftAlone;
            sg.title = "Not used";
        }
    }

    // Everything left alone can still be chosen: give each finding the band it would get if corrected.
    for (auto& sg : out)
    {
        if (sg.band || sg.optionalBand || sg.findingIndex < 0 || sg.findingIndex >= (int) r.findings.size())
            continue;
        const auto& f = r.findings[(size_t) sg.findingIndex];
        const double strength = std::max (0.5, amount);
        if (f.type == FT::Rumble || f.type == FT::DCOffset)
        {
            const double freq = f.type == FT::Rumble ? std::max (20.0, f.highFrequency * 0.8) : 20.0;
            sg.optionalBand = makeBand (dsp::FilterType::HighPass, freq, 0.0, 0.7071, slopeIndexFor (12));
        }
        else if (f.isNarrow())
        {
            const double cut = juce::jlimit (1.0, tuning.capNarrow, std::max (0.0, f.excessDb) * tuning.scale * strength);
            const double q = f.type == FT::Hum ? std::max (10.0, f.q) : juce::jlimit (2.0, 10.0, f.q > 0.0 ? f.q : 6.0);
            sg.optionalBand = makeBand (dsp::FilterType::Bell, f.frequency, -cut, q);
        }
        else
        {
            const double lo = std::max (20.0, f.lowFrequency), hi = std::max (lo * 1.05, f.highFrequency);
            const double cut = juce::jlimit (1.0, tuning.capBroad, std::max (0.0, f.excessDb) * tuning.scale * strength);
            sg.optionalBand = makeBand (dsp::FilterType::Bell, f.frequency > 0.0 ? f.frequency : std::sqrt (lo * hi), -cut,
                                        juce::jlimit (0.5, 3.0, qForBandwidth (octavesBetween (lo, hi))));
        }
    }

    std::stable_sort (out.begin(), out.end(), [] (const Suggestion& a, const Suggestion& b) { return a.band.has_value() && ! b.band.has_value(); });
    return out;
}

} // namespace eyra::correction
