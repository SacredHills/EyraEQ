#include "SpectrumAnalyzer.h"

#include <cmath>

namespace eyra::analysis
{

AnalyzerFifo::AnalyzerFifo (int capacity) : fifo (capacity)
{
    buffer.resize ((size_t) capacity, 0.0f);
}

void AnalyzerFifo::pushSamples (const double* left, const double* right, int numSamples) noexcept
{
    if (! enabled.load (std::memory_order_relaxed))
        return;

    int start1, size1, start2, size2;
    fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

    auto write = [&] (int dest, int count, int srcOffset)
    {
        for (int i = 0; i < count; ++i)
        {
            const double l = left[srcOffset + i];
            const double v = right != nullptr ? 0.5 * (l + right[srcOffset + i]) : l;
            buffer[(size_t) (dest + i)] = (float) v;
        }
    };

    write (start1, size1, 0);
    write (start2, size2, size1);
    fifo.finishedWrite (size1 + size2);
}

int AnalyzerFifo::pull (float* dest, int maxSamples) noexcept
{
    int start1, size1, start2, size2;
    fifo.prepareToRead (maxSamples, start1, size1, start2, size2);
    std::copy_n (buffer.data() + start1, size1, dest);
    std::copy_n (buffer.data() + start2, size2, dest + size1);
    fifo.finishedRead (size1 + size2);
    return size1 + size2;
}

//==============================================================================
SpectrumAnalyzer::SpectrumAnalyzer (AnalyzerFifo& source) : fifo (source)
{
    levels.assign ((size_t) kNumPoints, kFloorDb);
    powerAverage.assign ((size_t) kNumPoints, 0.0f);
    pullBuffer.resize ((size_t) source.getCapacity());
    configure();
}

double SpectrumAnalyzer::getPointFrequency (int index) noexcept
{
    const double t = (double) index / (double) (kNumPoints - 1);
    return kMinDisplayHz * std::pow (kMaxDisplayHz / kMinDisplayHz, t);
}

void SpectrumAnalyzer::setSettings (const AnalyzerSettings& newSettings)
{
    const bool reconfigure = newSettings.fftOrder != settings.fftOrder;
    const bool unfreezing = settings.freeze && ! newSettings.freeze;
    settings = newSettings;
    settings.fftOrder = juce::jlimit (AnalyzerSettings::kMinOrder, AnalyzerSettings::kMaxOrder, settings.fftOrder);

    if (reconfigure)
        configure();
    else if (unfreezing)
        samplesSinceFrame = 0;
}

void SpectrumAnalyzer::configure()
{
    fftSize = 1 << settings.fftOrder;
    fft = std::make_unique<juce::dsp::FFT> (settings.fftOrder);

    window.resize ((size_t) fftSize);
    juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), (size_t) fftSize,
                                                               juce::dsp::WindowingFunction<float>::blackmanHarris, false);
    double sum = 0.0;
    for (auto w : window)
        sum += w;
    windowGain = (float) (2.0 / sum);

    fftBuffer.assign ((size_t) fftSize * 2, 0.0f);
    history.assign ((size_t) fftSize, 0.0f);
    frameBinsDb.assign ((size_t) fftSize / 2 + 1, kFloorDb);
    historyWrite = 0;
    samplesSinceFrame = 0;
    samplesAvailable = 0;
}

void SpectrumAnalyzer::clear()
{
    std::fill (levels.begin(), levels.end(), kFloorDb);
    std::fill (powerAverage.begin(), powerAverage.end(), 0.0f);
    std::fill (history.begin(), history.end(), 0.0f);
    samplesAvailable = 0;
    samplesSinceFrame = 0;
}

bool SpectrumAnalyzer::update (double)
{
    const double sr = fifo.sampleRate.load();
    if (! juce::exactlyEqual (sr, lastSampleRate))
    {
        lastSampleRate = sr;
        clear();
    }

    const int n = fifo.pull (pullBuffer.data(), (int) pullBuffer.size());
    if (n == 0 || settings.freeze)
        return false;

    const int overlap = 2 << juce::jlimit (0, 2, settings.speed); // 2, 4, 8
    const int hop = juce::jmax (1, fftSize / overlap);
    const double frameSeconds = (double) hop / sr;

    // If the UI stalled, only analyse the most recent frames.
    constexpr int maxFramesPerUpdate = 8;
    const int framesPending = (samplesSinceFrame + n) / hop;
    const int framesToSkip = juce::jmax (0, framesPending - maxFramesPerUpdate);

    int frameCounter = 0;
    bool changed = false;
    double pendingSeconds = 0.0; // time covered by skipped frames, so decay / averaging stay in real time

    for (int i = 0; i < n; ++i)
    {
        history[(size_t) historyWrite] = pullBuffer[(size_t) i];
        historyWrite = (historyWrite + 1) % fftSize;
        samplesAvailable = juce::jmin (samplesAvailable + 1, fftSize);

        if (++samplesSinceFrame >= hop)
        {
            samplesSinceFrame = 0;
            pendingSeconds += frameSeconds;
            if (frameCounter++ >= framesToSkip && samplesAvailable >= fftSize)
            {
                computeFrame (pendingSeconds);
                pendingSeconds = 0.0;
                changed = true;
            }
        }
    }

    return changed;
}

void SpectrumAnalyzer::computeFrame (double frameSeconds)
{
    // Unroll the ring buffer (oldest first) and window it.
    for (int i = 0; i < fftSize; ++i)
        fftBuffer[(size_t) i] = history[(size_t) ((historyWrite + i) % fftSize)] * window[(size_t) i];
    std::fill (fftBuffer.begin() + fftSize, fftBuffer.end(), 0.0f);

    fft->performFrequencyOnlyForwardTransform (fftBuffer.data(), true);

    const int numBins = fftSize / 2 + 1;
    for (int k = 0; k < numBins; ++k)
    {
        const float mag = fftBuffer[(size_t) k] * windowGain;
        frameBinsDb[(size_t) k] = mag > 1.0e-7f ? 20.0f * std::log10 (mag) : kFloorDb;
    }

    const double sr = lastSampleRate;
    const double binHz = sr / (double) fftSize;
    const double halfStep = std::pow (kMaxDisplayHz / kMinDisplayHz, 0.5 / (kNumPoints - 1));

    static constexpr double averagingSeconds[] = { 0.0, 0.06, 0.25, 0.9 };
    const double tau = averagingSeconds[juce::jlimit (0, 3, settings.averaging)];
    const float alpha = tau > 0.0 ? (float) std::exp (-frameSeconds / tau) : 0.0f;
    const float decay = settings.decayDbPerSecond * (float) frameSeconds;

    for (int p = 0; p < kNumPoints; ++p)
    {
        const double f = getPointFrequency (p);
        float db = kFloorDb;

        if (f < 0.5 * sr)
        {
            const int lo = (int) std::ceil ((f / halfStep) / binHz);
            const int hi = juce::jmin (numBins - 1, (int) std::floor ((f * halfStep) / binHz));

            if (hi >= lo && lo >= 0)
            {
                for (int k = lo; k <= hi; ++k)
                    db = juce::jmax (db, frameBinsDb[(size_t) k]);
            }
            else
            {
                // Fewer bins than display points here: interpolate between neighbouring bins.
                const double pos = f / binHz;
                const int k0 = juce::jlimit (0, numBins - 2, (int) pos);
                const float frac = (float) (pos - k0);
                db = frameBinsDb[(size_t) k0] + frac * (frameBinsDb[(size_t) k0 + 1] - frameBinsDb[(size_t) k0]);
            }

            db += settings.slopeDbPerOctave * (float) std::log2 (f / 1000.0);
        }

        auto& level = levels[(size_t) p];

        if (settings.mode == AnalyzerSettings::Mode::RMS)
        {
            const float power = std::pow (10.0f, db / 10.0f);
            auto& avg = powerAverage[(size_t) p];
            avg = alpha * avg + (1.0f - alpha) * power;
            level = avg > 1.0e-14f ? 10.0f * std::log10 (avg) : kFloorDb;
        }
        else
        {
            level = juce::jmax (db, level - decay);
            powerAverage[(size_t) p] = std::pow (10.0f, level / 10.0f);
        }
    }
}

} // namespace eyra::analysis
