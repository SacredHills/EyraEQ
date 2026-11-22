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
