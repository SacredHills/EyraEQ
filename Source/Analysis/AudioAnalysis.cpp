#include "AnalysisInternal.h"

#include <algorithm>

namespace eyra::analysis
{

juce::String describeFrequency (double hz)
{
    if (hz >= 1000.0)
        return juce::String (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz";
    return juce::String (juce::roundToInt (hz)) + " Hz";
}

juce::String noteName (double hz)
{
    if (hz <= 0.0)
        return {};
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0));
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}

namespace detail
{
    void computeLongTimeSpectrum (Context& ctx)
    {
        const double sr = ctx.sampleRate;
        int size = 1;
        while (size < (int) (sr * 0.34))
            size <<= 1;
        ctx.longSize = size;
        ctx.longBinHz = sr / size;

        const int hop = size / 4;
        const int n = (int) ctx.signal.size();
        std::vector<int> starts;
        for (int start = 0; start + size <= n; start += hop)
            if (ctx.isActiveAt (start, size))
                starts.push_back (start);

        // Sparse short hits (a drum machine kick, say) rarely fill half a long frame: use every frame that
        // contains signal. The average is power-weighted, so the quiet parts barely change its shape.
        if (starts.size() < 8)
        {
            starts.clear();
            for (int start = 0; start + size <= n; start += hop)
                if (ctx.isActiveAt (start, size, 0.0))
                    starts.push_back (start);
        }

        // Bound memory and time: analyse at most 240 evenly spread frames.
        constexpr size_t maxFrames = 240;
        if (starts.size() > maxFrames)
        {
            std::vector<int> picked;
            for (size_t i = 0; i < maxFrames; ++i)
                picked.push_back (starts[i * starts.size() / maxFrames]);
            starts = std::move (picked);
        }

        PowerSpectrum spectrum (size);
        const int numBins = spectrum.numBins();
        ctx.maxAnalysisBin = std::min (numBins - 1, (int) std::floor (std::min (20000.0, 0.45 * sr) / ctx.longBinHz));
        ctx.ltasPower.assign ((size_t) numBins, 0.0);
        ctx.longFramesDb.clear();
        ctx.longFramesDb.reserve (starts.size());

        std::vector<float> power ((size_t) numBins);
        for (int start : starts)
        {
            spectrum.compute (ctx.signal.data() + start, power.data());
            std::vector<float> db ((size_t) numBins);
            for (int k = 0; k < numBins; ++k)
            {
                ctx.ltasPower[(size_t) k] += power[(size_t) k];
                db[(size_t) k] = (float) powerToDb (power[(size_t) k]);
            }
            ctx.longFramesDb.push_back (std::move (db));
        }

        const double count = std::max<double> (1.0, (double) starts.size());
        ctx.ltasDb.resize ((size_t) numBins);
        for (int k = 0; k < numBins; ++k)
        {
            ctx.ltasPower[(size_t) k] /= count;
            ctx.ltasDb[(size_t) k] = powerToDb (ctx.ltasPower[(size_t) k]);
        }
    }
    /** Band shares, noisiness and decay per hit: used by instrument recognition. Needs the short-time
        analysis, onsets and long-time spectrum. */
    void measureSoundCharacter (Context& ctx)
    {
        auto& r = ctx.result;
    {
        double total = 0.0, low = 0.0, lowMid = 0.0, mid = 0.0, high = 0.0, logSum = 0.0, linSum = 0.0, weighted = 0.0;
        int flatCount = 0;
        for (int k = 1; k <= ctx.maxAnalysisBin; ++k)
        {
            const double f = k * ctx.longBinHz, p = ctx.ltasPower[(size_t) k];
            if (f < 20.0)
                continue;
            total += p;
            weighted += f * p;
            (f < 150.0 ? low : (f < 1000.0 ? lowMid : (f < 5000.0 ? mid : high))) += p;
            if (f >= 150.0 && f <= 8000.0)
            {
                logSum += std::log (std::max (p, 1.0e-20));
                linSum += p;
                ++flatCount;
            }
        }
        if (total > 0.0)
        {
            r.lowShare = low / total;
            r.lowMidShare = lowMid / total;
            r.midShare = mid / total;
            r.highShare = high / total;
            if (r.spectralCentroidHz <= 0.0) // (not measured by the tonal balance stage)
                r.spectralCentroidHz = weighted / total;
        }
        if (flatCount > 0 && linSum > 0.0)
            r.spectralFlatness = std::exp (logSum / flatCount) / (linSum / flatCount);

        // Decay per hit from a 5 ms peak envelope: time from the peak until it is 20 dB lower.
        const int block = std::max (1, (int) std::lround (0.005 * ctx.sampleRate));
        std::vector<double> decays;
        for (size_t i = 0; i < ctx.onsets.size(); ++i)
        {
            const int start = std::max (0, ctx.onsets[i] * ctx.shortHop - ctx.shortSize / 2);
            const int next = i + 1 < ctx.onsets.size() ? ctx.onsets[i + 1] * ctx.shortHop - ctx.shortSize / 2 : (int) ctx.signal.size();
            const int end = std::min ({ (int) ctx.signal.size(), next, start + (int) (1.5 * ctx.sampleRate) });
            std::vector<float> env;
            for (int b = start; b + block <= end; b += block)
            {
                float m = 0.0f;
                for (int j = b; j < b + block; ++j)
                    m = std::max (m, std::abs (ctx.signal[(size_t) j]));
                env.push_back (m);
            }
            if (env.size() < 4)
                continue;
            const auto peakIt = std::max_element (env.begin(), env.begin() + (ptrdiff_t) std::min<size_t> (env.size(), 30));
            const size_t peak = (size_t) (peakIt - env.begin());
            size_t k = peak;
            while (k < env.size() && env[k] >= 0.1f * *peakIt)
                ++k;
            const double seconds = (double) (k - peak) * block / ctx.sampleRate;
            // A hit cut off by the next one still rings at least this long: keep it when that is already long.
            if (k < env.size() || seconds >= 0.3)
                decays.push_back (seconds);
        }
        if (decays.size() >= 3)
        {
            std::nth_element (decays.begin(), decays.begin() + (ptrdiff_t) (decays.size() / 2), decays.end());
            r.medianDecaySeconds = decays[decays.size() / 2];
        }
    }

    }

} // namespace detail

AnalysisResult analyseAudio (const float* mono, int numSamples, double sampleRate, const AnalysisSettings& settings)
{
    using namespace detail;

    Context ctx (settings, sampleRate);
    auto& r = ctx.result;
    r.sampleRate = sampleRate;
    r.totalSeconds = sampleRate > 0.0 ? numSamples / sampleRate : 0.0;

    if (mono == nullptr || numSamples <= 0 || sampleRate <= 0.0)
    {
        r.status = AnalysisResult::Status::Silent;
        r.message = "No audio to analyse.";
        return r;
    }

    // --- DC removal and activity gate ----------------------------------------------------------
    double sum = 0.0;
    for (int i = 0; i < numSamples; ++i)
        sum += mono[i];
    ctx.dcOffset = sum / numSamples;
    ctx.signal.resize ((size_t) numSamples);
    double sumSq = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        const float v = (float) (mono[i] - ctx.dcOffset);
        ctx.signal[(size_t) i] = std::isfinite (v) ? v : 0.0f;
        sumSq += (double) ctx.signal[(size_t) i] * ctx.signal[(size_t) i];
    }
    ctx.rms = std::sqrt (sumSq / numSamples);

    ctx.gateBlock = std::max (64, (int) std::lround (0.02 * sampleRate));
    const int numBlocks = (numSamples + ctx.gateBlock - 1) / ctx.gateBlock;
    std::vector<double> blockDb ((size_t) numBlocks);
    double peakDb = -200.0;
    for (int b = 0; b < numBlocks; ++b)
    {
        const int start = b * ctx.gateBlock, end = std::min (numSamples, start + ctx.gateBlock);
        double e = 0.0;
        for (int i = start; i < end; ++i)
            e += (double) ctx.signal[(size_t) i] * ctx.signal[(size_t) i];
        blockDb[(size_t) b] = powerToDb (e / std::max (1, end - start));
        peakDb = std::max (peakDb, blockDb[(size_t) b]);
    }
    const double gate = std::max (-65.0, peakDb - 45.0);
    ctx.activeBlock.resize ((size_t) numBlocks);
    int activeCount = 0;
    for (int b = 0; b < numBlocks; ++b)
    {
        ctx.activeBlock[(size_t) b] = blockDb[(size_t) b] > gate;
        activeCount += ctx.activeBlock[(size_t) b] ? 1 : 0;
    }
    ctx.activeSeconds = activeCount * (double) ctx.gateBlock / sampleRate;
    r.activeSeconds = ctx.activeSeconds;

    if (peakDb < -80.0 || ctx.activeSeconds < 0.05)
    {
        r.status = AnalysisResult::Status::Silent;
        r.message = "No signal detected. Start playback before analysing.";
        return r;
    }
    if (ctx.activeSeconds < settings.minActiveSeconds)
    {
        // Too little for findings, but sparse drum hits are still measured for instrument recognition.
        computeShortTimeAnalysis (ctx);
        detectOnsets (ctx);
        computeLongTimeSpectrum (ctx);
        measureSoundCharacter (ctx);
        r.status = AnalysisResult::Status::InsufficientAudio;
        r.message = "Only " + juce::String (ctx.activeSeconds, 1) + " s of signal. Play at least "
                  + juce::String (settings.minActiveSeconds, 0) + " s for a meaningful analysis.";
        return r;
    }

    // --- Stages ----------------------------------------------------------------------------------
    computeShortTimeAnalysis (ctx);
    estimatePitch (ctx);
    detectOnsets (ctx);
    computeLongTimeSpectrum (ctx);
    detectResonances (ctx);      // first, so broad balance can ignore narrow peaks
    analyseTonalBalance (ctx);
    measureSoundCharacter (ctx);

    // --- Post-processing -----------------------------------------------------------------------
    // A modest broad "buildup" produced by a strong narrow peak is reported as that peak only.
    std::vector<Finding> kept;
    for (const auto& f : r.findings)
    {
        if (f.type == FindingType::Buildup && f.excessDb < 5.0)
        {
            bool explained = false;
            for (const auto& g : r.findings)
                explained |= g.isNarrow() && g.excessDb >= 8.0 && g.frequency >= f.lowFrequency && g.frequency <= f.highFrequency;
            if (explained)
                continue;
        }
        if (f.severity < 0.08)
            continue;
        kept.push_back (f);
    }
    std::stable_sort (kept.begin(), kept.end(), [] (const Finding& a, const Finding& b)
    {
        return a.severity * (0.5 + 0.5 * a.confidence) > b.severity * (0.5 + 0.5 * b.confidence);
    });
    if ((int) kept.size() > settings.maxFindings)
        kept.resize ((size_t) settings.maxFindings);
    r.findings = std::move (kept);

    r.confidence = juce::jlimit (0.3, 1.0, 0.3 + 0.7 * (ctx.activeSeconds - settings.minActiveSeconds)
                                                  / (settings.fullConfidenceSeconds - settings.minActiveSeconds));

    // --- Display spectrum (1/6-octave smoothed LTAS) -----------------------------------------
    const auto smoothed = smoothFractionalOctave (ctx.ltasPower, ctx.longBinHz, 1.0 / 6.0, 1);
    r.ltasBinHz = ctx.longBinHz;
    r.displayFrequencies.resize ((size_t) settings.displayPoints);
    r.ltasDb.resize ((size_t) settings.displayPoints);
    for (int i = 0; i < settings.displayPoints; ++i)
    {
        const double f = settings.displayMinHz * std::pow (settings.displayMaxHz / settings.displayMinHz, (double) i / (settings.displayPoints - 1));
        r.displayFrequencies[(size_t) i] = f;
        const double pos = f / ctx.longBinHz;
        if (pos >= (double) ctx.maxAnalysisBin || pos < 1.0)
        {
            r.ltasDb[(size_t) i] = -140.0f;
            continue;
        }
        const int k0 = (int) pos;
        const double frac = pos - k0;
        r.ltasDb[(size_t) i] = (float) powerToDb (smoothed[(size_t) k0] * (1.0 - frac) + smoothed[(size_t) k0 + 1] * frac);
    }

    r.status = AnalysisResult::Status::Ok;
    r.message = r.findings.empty() ? juce::String ("No frequency problems found.")
                                   : juce::String ((int) r.findings.size()) + (r.findings.size() == 1 ? " finding." : " findings.");
    return r;
}

} // namespace eyra::analysis
