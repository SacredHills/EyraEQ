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

    // --- High-pass for the instrument -------------------------------------------------------
    if (s.autoHighPass && profile.highPassHz > 0.0)
        offerHighPass (profile.highPassHz, contentLimit, 12, -1,
                       "Below the lowest useful content of " + instrument + "; clears room in the mix. Kept under the lowest measured content.",
                       1.5);
    if (highPass)
        out.push_back (*highPass);

    // --- Polished: subtle enhancement, only where the recording does not already have it ----
    if (s.mode == Mode::Polished)
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

    int kept = 0;
    for (size_t i = 0; i < out.size(); ++i)
    {
        auto& sg = out[i];
        if (! sg.band)
            continue;
        if (sg.kind == K::Cut)
            for (size_t j = 0; j < i; ++j)
                if (out[j].band && out[j].kind == K::Cut && out[j].band->type == sg.band->type
                    && octavesBetween (out[j].band->frequency, sg.band->frequency) < 1.0 / 6.0)
                {
                    sg.reason = "Already covered by the stronger cut at " + hz (out[j].band->frequency) + ".";
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
            sg.band.reset();
            sg.kind = K::LeftAlone;
            sg.title = "Not used";
        }
    }

    std::stable_sort (out.begin(), out.end(), [] (const Suggestion& a, const Suggestion& b) { return a.band.has_value() && ! b.band.has_value(); });
    return out;
}

} // namespace eyra::correction
