// Narrow-band problems: resonances, intermittent resonances, ringing and mains hum.
//
// A peak is a candidate when it rises above the median of its own third-octave neighbourhood in the
// long-term spectrum. Candidates are then tested for:
//   - persistence (share of frames in which the peak is present) and consistency across the capture,
//   - being a harmonic of the source's fundamental (natural tonal content, never reported),
//   - mains-hum series (50 / 60 Hz multiples, stationary),
//   - ringing: decaying slower than neighbouring frequencies after transients.

#include "AnalysisInternal.h"

#include <algorithm>

namespace eyra::analysis::detail
{

namespace
{
    double baseConfidence (const Context& ctx)
    {
        const auto& s = ctx.settings;
        return juce::jlimit (0.3, 1.0, 0.3 + 0.7 * (ctx.activeSeconds - s.minActiveSeconds) / (s.fullConfidenceSeconds - s.minActiveSeconds));
    }

    double medianOfFloatWindow (const std::vector<float>& db, int lo, int hi, std::vector<float>& scratch)
    {
        scratch.assign (db.begin() + lo, db.begin() + hi + 1);
        auto mid = scratch.begin() + (ptrdiff_t) (scratch.size() / 2);
        std::nth_element (scratch.begin(), mid, scratch.end());
        return *mid;
    }

    struct Ringing
    {
        bool evaluated = false;
        bool ringing = false;
        double ratio = 0.0;
        double ringTime = 0.0;
    };

    Ringing evaluateRinging (const Context& ctx, double frequency)
    {
        Ringing out;
        double bandRate = 0.0, neighbourRate = 0.0;
        int used = 0;
        if (! measureBandDecay (ctx, ctx.bandIndexFor (frequency), bandRate, neighbourRate, used))
            return out;

        out.evaluated = true;
        if (bandRate >= -1.0)
            out.ratio = neighbourRate <= -20.0 ? 10.0 : 1.0;
        else
            out.ratio = juce::jlimit (0.0, 10.0, neighbourRate / bandRate);
        out.ringTime = bandRate < -0.1 ? juce::jmin (10.0, 60.0 / -bandRate) : 10.0;
        out.ringing = out.ratio >= 1.5 && out.ringTime >= 0.12 && neighbourRate <= -10.0;
        return out;
    }
} // namespace

void detectResonances (Context& ctx)
{
    auto& r = ctx.result;
    const auto& s = ctx.settings;
    const double binHz = ctx.longBinHz;
    const auto& L = ctx.ltasDb;
    const int maxBin = ctx.maxAnalysisBin;
    const double conf = baseConfidence (ctx);
    const double threshold = s.resonanceProminenceDb;

    const int kMin = std::max (3, (int) std::ceil (25.0 / binHz));
    if (maxBin - kMin < 16)
        return;

    // --- Baseline: median of the 2/3-octave neighbourhood (wide enough not to be lifted by the peak's skirt)
    ctx.baselineDb.assign (L.size(), -200.0);
    std::vector<double> scratch;
    for (int k = kMin; k <= maxBin; ++k)
        ctx.baselineDb[(size_t) k] = medianDbAround (L, k, binHz, 2.0 / 3.0, 8, scratch);

    const double globalMax = *std::max_element (L.begin() + kMin, L.begin() + maxBin + 1);

    // --- Refine the pitch on the fine spectrum ----------------------------------------------
    // Autocorrelation can be pulled off by other partials; snap to the actual spectral peak near it.
    if (r.pitch.pitched && r.pitch.medianHz > 0.0)
    {
        const int lo = std::max (kMin, (int) std::floor (r.pitch.medianHz * 0.97 / binHz));
        const int hi = std::min (maxBin - 1, (int) std::ceil (r.pitch.medianHz * 1.03 / binHz));
        int best = -1;
        for (int k = lo; k <= hi; ++k)
            if (best < 0 || L[(size_t) k] > L[(size_t) best])
                best = k;
        if (best > 0 && L[(size_t) best] - ctx.baselineDb[(size_t) best] >= 6.0)
        {
            const double refined = (best + parabolicOffset (L[(size_t) best - 1], L[(size_t) best], L[(size_t) best + 1])) * binHz;
            const double ratio = refined / r.pitch.medianHz;
            r.pitch.medianHz = refined;
            r.pitch.lowHz *= ratio;
            r.pitch.highHz *= ratio;
        }
    }

    // --- Candidates -----------------------------------------------------------------------------
    std::vector<Candidate> cands;
    for (int k = kMin + 2; k <= maxBin - 2; ++k)
    {
        const double v = L[(size_t) k];
        const double prom = v - ctx.baselineDb[(size_t) k];
        if (prom < threshold || v < globalMax - 60.0)
            continue;
        if (v < L[(size_t) k - 1] || v < L[(size_t) k + 1] || v < L[(size_t) k - 2] || v < L[(size_t) k + 2])
            continue;

        Candidate c;
        c.bin = k;
        c.frequency = (k + parabolicOffset (L[(size_t) k - 1], v, L[(size_t) k + 1])) * binHz;
        c.prominenceDb = prom;
        c.levelDb = v;

        // -3 dB bandwidth, interpolated, searched within half an octave.
        const double target = v - 3.0;
        double lowEdge = c.frequency / std::sqrt (2.0), highEdge = c.frequency * std::sqrt (2.0);
        for (int j = k - 1; j > std::max (1, (int) (k / 1.42)); --j)
            if (L[(size_t) j] < target)
            {
                const double frac = (target - L[(size_t) j]) / (L[(size_t) j + 1] - L[(size_t) j]);
                lowEdge = (j + frac) * binHz;
                break;
            }
        for (int j = k + 1; j < std::min (maxBin, (int) (k * 1.42) + 1); ++j)
            if (L[(size_t) j] < target)
            {
                const double frac = (L[(size_t) j - 1] - target) / (L[(size_t) j - 1] - L[(size_t) j]);
                highEdge = (j - 1 + frac) * binHz;
                break;
            }
        c.q = c.frequency / std::max (binHz * 0.5, highEdge - lowEdge);
        if (c.q < 2.0)
            continue;

        cands.push_back (c);
    }

    // Keep the strongest peak within 1/12 octave.
    std::sort (cands.begin(), cands.end(), [] (const Candidate& a, const Candidate& b) { return a.prominenceDb > b.prominenceDb; });
    std::vector<Candidate> unique;
    for (const auto& c : cands)
    {
        bool near = false;
        for (const auto& u : unique)
            near |= std::abs (std::log2 (c.frequency / u.frequency)) < 1.0 / 12.0;
        if (! near)
            unique.push_back (c);
    }

    // --- Persistence and consistency across the capture -------------------------------------
    // Single-frame noise spectra fluctuate too much to judge presence, so frames are averaged in groups
    // of four (~0.6 s) and the peak is smoothed over three bins before comparing with the local median.
    const int numFrames = (int) ctx.longFramesDb.size();
    constexpr int groupSize = 4;
    const int numGroups = std::max (1, numFrames / groupSize);
    std::vector<std::vector<float>> groups ((size_t) numGroups, std::vector<float> ((size_t) maxBin + 2, 0.0f));
    for (int g = 0; g < numGroups; ++g)
    {
        const int t0 = g * groupSize, t1 = (g == numGroups - 1) ? numFrames : t0 + groupSize;
        auto& acc = groups[(size_t) g];
        for (int k = 0; k <= maxBin + 1; ++k)
        {
            double p = 0.0;
            for (int t = t0; t < t1; ++t)
                p += std::pow (10.0, ctx.longFramesDb[(size_t) t][(size_t) k] / 10.0);
            acc[(size_t) k] = (float) powerToDb (p / std::max (1, t1 - t0));
        }
    }

    std::vector<float> fscratch;
    for (auto& c : unique)
    {
        const double half = std::pow (2.0, 1.0 / 3.0);
        const int lo = std::clamp (std::min ((int) std::floor (c.bin / half), c.bin - 8), 1, maxBin);
        const int hi = std::clamp (std::max ((int) std::ceil (c.bin * half), c.bin + 8), 1, maxBin);
        const double presenceDb = std::max (3.0, 0.5 * c.prominenceDb);

        int present = 0;
        int segmentPresent[3] = { 0, 0, 0 }, segmentTotal[3] = { 0, 0, 0 };
        for (int g = 0; g < numGroups; ++g)
        {
            const auto& spec = groups[(size_t) g];
            const double peak = powerToDb ((std::pow (10.0, spec[(size_t) c.bin - 1] / 10.0) + std::pow (10.0, spec[(size_t) c.bin] / 10.0)
                                            + std::pow (10.0, spec[(size_t) c.bin + 1] / 10.0)) / 3.0);
            const bool isPresent = peak - medianOfFloatWindow (spec, lo, hi, fscratch) >= presenceDb;
            const int seg = std::min (2, g * 3 / numGroups);
            ++segmentTotal[seg];
            if (isPresent)
            {
                ++present;
                ++segmentPresent[seg];
            }
        }
        c.persistence = (double) present / numGroups;
        for (int seg = 0; seg < 3; ++seg)
            if (segmentTotal[seg] > 0 && segmentPresent[seg] >= std::max (1, (int) std::ceil (0.3 * segmentTotal[seg])))
                ++c.segmentsPresent;
    }

    // --- Mains hum -----------------------------------------------------------------------------
    for (double base : { 50.0, 60.0 })
    {
        std::vector<int> members;
        double strongest = 0.0, maxPersistence = 0.0;
        for (int m = 1; m <= 8; ++m)
        {
            const double target = base * m;
            const double tol = std::max (1.2 * binHz, 0.004 * target);
            for (int i = 0; i < (int) unique.size(); ++i)
                if (std::abs (unique[(size_t) i].frequency - target) <= tol)
                {
                    members.push_back (i);
                    strongest = std::max (strongest, unique[(size_t) i].prominenceDb);
                    maxPersistence = std::max (maxPersistence, unique[(size_t) i].persistence);
                }
        }

        if (members.size() >= 2 && maxPersistence >= 0.8)
        {
            Finding f;
            f.type = FindingType::Hum;
            f.title = "Mains hum (" + juce::String ((int) base) + " Hz)";
            juce::StringArray freqs;
            for (int i : members)
                freqs.add (juce::String (juce::roundToInt (unique[(size_t) i].frequency)));
            f.detail = "Constant narrow peaks at " + freqs.joinIntoString (", ") + " Hz: a " + juce::String ((int) base)
                     + " Hz mains hum series, up to " + juce::String (strongest, 1) + " dB above the surrounding spectrum.";
            f.frequency = unique[(size_t) members.front()].frequency;
            f.lowFrequency = f.highFrequency = f.frequency;
            f.excessDb = strongest;
            f.persistence = maxPersistence;
            f.q = unique[(size_t) members.front()].q;
            f.severity = juce::jlimit (0.3, 1.0, (strongest - 3.0) / 12.0);
            f.confidence = conf * 0.95;
            r.findings.push_back (f);

            std::sort (members.begin(), members.end(), std::greater<>());
            for (int i : members)
                unique.erase (unique.begin() + i);
        }
    }

    // --- Classification --------------------------------------------------------------------
    const auto& pitch = r.pitch;
    const bool stablePitch = pitch.pitched && pitch.spreadCents <= 100.0 && pitch.medianHz > 0.0;
    const bool varyingPitch = pitch.pitched && ! stablePitch;

    std::vector<Finding> narrow;
    auto describe = [] (const Candidate& c)
    {
        const juce::String width = c.q >= 60.0 ? juce::String ("very narrow (tonal)") : "Q " + (c.q < 10.0 ? juce::String (c.q, 1) : juce::String (juce::roundToInt (c.q)));
        return juce::String (c.prominenceDb, 1) + " dB above its neighbourhood, " + width;
    };

    for (auto& c : unique)
    {
        if (stablePitch)
        {
            const double h = std::round (c.frequency / pitch.medianHz);
            const double tol = std::max (0.012, 1.5 * binHz / c.frequency);
            if (h >= 1.0 && h <= 64.0 && std::abs (c.frequency - h * pitch.medianHz) / c.frequency <= tol)
            {
                c.harmonicNumber = (int) h;
                continue; // natural partial of the source
            }
        }

        FindingType type;
        if (c.persistence >= 0.6)
            type = FindingType::Resonance;
        else if (c.persistence >= 0.2)
            type = FindingType::IntermittentResonance;
        else
            continue;

        Ringing ring;
        if (r.percussive)
            ring = evaluateRinging (ctx, c.frequency);

        Finding f;
        f.frequency = c.frequency;
        f.lowFrequency = f.highFrequency = c.frequency;
        f.excessDb = c.prominenceDb;
        f.q = c.q;
        f.persistence = c.persistence;

        const double segmentFactor = c.segmentsPresent >= 3 ? 1.0 : (c.segmentsPresent == 2 ? 0.8 : 0.55);
        const double promScore = juce::jlimit (0.0, 1.0, (c.prominenceDb - 3.0) / 9.0);

        if (ring.ringing)
        {
            f.type = FindingType::Ringing;
            f.decayRatio = ring.ratio;
            f.ringTimeSeconds = ring.ringTime;
            f.title = "Ringing at " + describeFrequency (c.frequency);
            f.detail = "Decays " + juce::String (ring.ratio, 1) + "x slower than nearby frequencies after each hit (ring time "
                     + (ring.ringTime >= 9.9 ? juce::String ("over 10 s") : "about " + juce::String (ring.ringTime, 2) + " s") + "); " + describe (c) + ".";
            f.severity = juce::jlimit (0.0, 1.0, 0.6 * juce::jlimit (0.0, 1.0, (ring.ratio - 1.2) / 2.0) + 0.4 * promScore);
            f.confidence = conf * segmentFactor;
        }
        else if (type == FindingType::Resonance)
        {
            f.type = type;
            f.title = "Resonance at " + describeFrequency (c.frequency);
            f.detail = describe (c) + ", present in " + juce::String (juce::roundToInt (100.0 * c.persistence)) + "% of the audio.";
            f.severity = promScore * (0.55 + 0.45 * c.persistence);
            f.confidence = conf * segmentFactor * (varyingPitch ? 0.7 : 1.0);
        }
        else
        {
            f.type = type;
            f.title = "Intermittent resonance at " + describeFrequency (c.frequency);
            f.detail = describe (c) + ", present in " + juce::String (juce::roundToInt (100.0 * c.persistence))
                     + "% of the audio. A dynamic band may suit it better than a static cut.";
            if (varyingPitch)
                f.detail << " The source changes pitch, so this may be a played note's harmonic.";
            f.severity = 0.8 * juce::jlimit (0.0, 1.0, (c.prominenceDb - 6.0) / 12.0);
            f.confidence = conf * segmentFactor * (varyingPitch ? 0.5 : 0.85);
        }
        narrow.push_back (f);
    }

    // --- Ringing that only shows in the decays (percussive material) --------------------------
    if (r.percussive && ! ctx.onsets.empty())
    {
        const int numBands = (int) ctx.bandCentres.size();
        const int numShort = (int) ctx.shortBandPower.size();
        const double fs = ctx.shortFrameSeconds();
        const int t0 = (int) std::lround (0.06 / fs), t1 = (int) std::lround (0.3 / fs);

        std::vector<double> tail ((size_t) numBands, 0.0);
        int count = 0;
        for (size_t i = 0; i < ctx.onsets.size(); ++i)
        {
            const int o = ctx.onsets[i];
            const int next = i + 1 < ctx.onsets.size() ? ctx.onsets[i + 1] : numShort;
            for (int t = o + t0; t <= std::min (o + t1, next - 1); ++t)
            {
                if (t >= numShort)
                    break;
                for (int b = 0; b < numBands; ++b)
                    tail[(size_t) b] += ctx.shortBandPower[(size_t) t][(size_t) b];
                ++count;
            }
        }

        if (count > 0)
        {
            std::vector<double> tailDb ((size_t) numBands);
            for (int b = 0; b < numBands; ++b)
                tailDb[(size_t) b] = powerToDb (tail[(size_t) b] / count);

            std::vector<double> window;
            for (int b = 8; b < numBands - 8; ++b)
            {
                if (ctx.bandCentres[(size_t) b] < 80.0)
                    continue;
                if (tailDb[(size_t) b] < tailDb[(size_t) b - 1] || tailDb[(size_t) b] < tailDb[(size_t) b + 1])
                    continue;
                window.assign (tailDb.begin() + b - 8, tailDb.begin() + b + 9);
                std::nth_element (window.begin(), window.begin() + 8, window.end());
                const double prom = tailDb[(size_t) b] - window[8];
                if (prom < 4.0)
                    continue;

                // Refine on the fine-resolution spectrum.
                const double c = ctx.bandCentres[(size_t) b];
                const int lo = std::max (kMin, (int) (c * std::pow (2.0, -1.0 / 24.0) / binHz));
                const int hi = std::min (maxBin, (int) (c * std::pow (2.0, 1.0 / 24.0) / binHz) + 1);
                int bestBin = lo;
                for (int k = lo; k <= hi; ++k)
                    if (L[(size_t) k] > L[(size_t) bestBin])
                        bestBin = k;
                const double freq = bestBin * binHz;

                // The source's own fundamental and harmonics ring naturally (e.g. a tom's body).
                if (stablePitch)
                {
                    const double h = std::round (freq / pitch.medianHz);
                    if (h >= 1.0 && h <= 64.0 && std::abs (freq - h * pitch.medianHz) / freq <= std::max (0.02, 1.5 * binHz / freq))
                        continue;
                }

                bool known = false;
                for (const auto& f : narrow)
                    known |= std::abs (std::log2 (freq / f.frequency)) < 1.0 / 12.0;
                for (const auto& f : r.findings)
                    known |= f.isNarrow() && std::abs (std::log2 (freq / std::max (1.0, f.frequency))) < 1.0 / 12.0;
                if (known)
                    continue;

                const auto ring = evaluateRinging (ctx, freq);
                if (! ring.ringing)
                    continue;

                Finding f;
                f.type = FindingType::Ringing;
                f.frequency = freq;
                f.lowFrequency = f.highFrequency = freq;
                f.excessDb = prom;
                f.decayRatio = ring.ratio;
                f.ringTimeSeconds = ring.ringTime;
                f.title = "Ringing at " + describeFrequency (freq);
                f.detail = "Decays " + juce::String (ring.ratio, 1) + "x slower than nearby frequencies after each hit (ring time "
                         + (ring.ringTime >= 9.9 ? juce::String ("over 10 s") : "about " + juce::String (ring.ringTime, 2) + " s") + "); "
                         + juce::String (prom, 1) + " dB above its neighbourhood in the decay.";
                f.severity = juce::jlimit (0.0, 1.0, 0.6 * juce::jlimit (0.0, 1.0, (ring.ratio - 1.2) / 2.0) + 0.4 * juce::jlimit (0.0, 1.0, (prom - 3.0) / 12.0));
                f.confidence = conf * 0.8;
                narrow.push_back (f);
            }
        }
    }

    std::sort (narrow.begin(), narrow.end(), [] (const Finding& a, const Finding& b) { return a.severity > b.severity; });
    if ((int) narrow.size() > s.maxResonances)
        narrow.resize ((size_t) s.maxResonances);
    for (auto& f : narrow)
        r.findings.push_back (f);

    ctx.resonanceCandidates = unique;
}

} // namespace eyra::analysis::detail
