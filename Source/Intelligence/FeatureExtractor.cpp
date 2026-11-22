#include "FeatureExtractor.h"

#include "DSP/FilterDesign.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <numbers>

namespace eyra::intelligence
{

bool FeatureExtractor::configure (const juce::var& fe, const juce::File& dir, juce::String& error)
{
    if (! fe.isObject())
    {
        error = "manifest has no frontend section";
        return false;
    }

    config.sampleRate = (int) fe.getProperty ("sampleRate", 16000);
    config.windowSamples = (int) fe.getProperty ("windowSamples", 400);
    config.hopSamples = (int) fe.getProperty ("hopSamples", 160);
    config.fftSize = (int) fe.getProperty ("fftSize", 512);
    config.melBands = (int) fe.getProperty ("melBands", 64);
    config.logOffset = (float) (double) fe.getProperty ("logOffset", 0.001);
    config.patchFrames = (int) fe.getProperty ("patchFrames", 96);
    config.patchHopFrames = (int) fe.getProperty ("patchHopFrames", 48);

    if (config.fftSize != 512 || config.windowSamples > config.fftSize || fe.getProperty ("window", "").toString() != "periodic-hann")
    {
        error = "unsupported front end configuration";
        return false;
    }

    const auto file = dir.getChildFile (fe.getProperty ("melMatrix", "").toString());
    juce::MemoryBlock data;
    const size_t expected = (size_t) (config.fftSize / 2 + 1) * (size_t) config.melBands * sizeof (float);
    if (! file.loadFileAsData (data) || data.getSize() != expected)
    {
        error = "mel matrix missing or wrong size";
        return false;
    }

    melMatrix.resize (expected / sizeof (float));
    // Stored little-endian float32
    for (size_t i = 0; i < melMatrix.size(); ++i)
    {
        juce::uint32 bits = juce::ByteOrder::littleEndianInt (static_cast<const char*> (data.getData()) + i * 4);
        std::memcpy (&melMatrix[i], &bits, 4);
    }
    return true;
}

std::vector<float> FeatureExtractor::resample (const float* input, int numSamples, double sourceRate) const
{
    const double target = config.sampleRate;
    if (std::abs (sourceRate - target) < 0.5)
        return std::vector<float> (input, input + numSamples);

    // Anti-alias below the target Nyquist (the model's mel bands stop at 7.5 kHz), then interpolate.
    std::vector<float> filtered (input, input + numSamples);
    if (sourceRate > target)
    {
        const auto cascade = dsp::design ({ dsp::FilterType::LowPass, 0.47 * target, 0.0, 0.7071, 6 }, sourceRate); // 72 dB/oct
        std::array<std::array<double, 2>, dsp::kMaxSections> z {};
        for (auto& v : filtered)
        {
            double y = v;
            for (int s = 0; s < cascade.numSections; ++s)
            {
                const auto& k = cascade.sections[(size_t) s];
                const double x = y;
                y = k.b0 * x + z[(size_t) s][0];
                z[(size_t) s][0] = k.b1 * x - k.a1 * y + z[(size_t) s][1];
                z[(size_t) s][1] = k.b2 * x - k.a2 * y;
            }
            v = (float) y;
        }
    }

    const double ratio = sourceRate / target;
    const int outSamples = (int) std::floor ((double) numSamples / ratio);
    std::vector<float> out ((size_t) std::max (0, outSamples));
    juce::LagrangeInterpolator interpolator;
    interpolator.process (ratio, filtered.data(), out.data(), outSamples, numSamples, 0);
    return out;
}

std::vector<float> FeatureExtractor::padForPatches (std::vector<float> w) const
{
    // Same rule as YAMNet's pad_waveform().
    const int minSamples = config.patchFrames * config.hopSamples + config.windowSamples - config.hopSamples;
    int n = (int) w.size();
    int pad = std::max (0, minSamples - n);
    n = std::max (n, minSamples);
    const int hop = config.patchHopFrames * config.hopSamples;
    const int after = n - minSamples;
    const int hops = (after + hop - 1) / hop;
    pad += hop * hops - after;
    w.resize (w.size() + (size_t) pad, 0.0f);
    return w;
}

std::vector<float> FeatureExtractor::logMel (const std::vector<float>& w, int& numFrames) const
{
    const int win = config.windowSamples, hop = config.hopSamples, nfft = config.fftSize, bands = config.melBands;
    const int bins = nfft / 2 + 1;
    numFrames = (int) w.size() >= win ? 1 + ((int) w.size() - win) / hop : 0;

    std::vector<float> window ((size_t) win);
    for (int i = 0; i < win; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / win)); // periodic Hann

    juce::dsp::FFT fft (9); // 512
    std::vector<float> buffer ((size_t) nfft * 2);
    std::vector<float> magnitude ((size_t) bins);
    std::vector<float> out ((size_t) numFrames * (size_t) bands);

    for (int f = 0; f < numFrames; ++f)
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        for (int i = 0; i < win; ++i)
            buffer[(size_t) i] = w[(size_t) (f * hop + i)] * window[(size_t) i];
        fft.performRealOnlyForwardTransform (buffer.data(), true);
        for (int k = 0; k < bins; ++k)
            magnitude[(size_t) k] = std::hypot (buffer[(size_t) (2 * k)], buffer[(size_t) (2 * k + 1)]);

        for (int m = 0; m < bands; ++m)
        {
            double acc = 0.0;
            for (int k = 0; k < bins; ++k)
                acc += (double) magnitude[(size_t) k] * melMatrix[(size_t) (k * bands + m)];
            out[(size_t) (f * bands + m)] = (float) std::log (acc + config.logOffset);
        }
    }
    return out;
}

std::vector<float> FeatureExtractor::patches (const std::vector<float>& logMel, int numFrames, int& numPatches) const
{
    const int pf = config.patchFrames, hop = config.patchHopFrames, bands = config.melBands;
    numPatches = numFrames >= pf ? 1 + (numFrames - pf) / hop : 0;
    std::vector<float> out ((size_t) numPatches * (size_t) pf * (size_t) bands);
    for (int p = 0; p < numPatches; ++p)
        std::copy_n (logMel.begin() + (ptrdiff_t) p * hop * bands, pf * bands, out.begin() + (ptrdiff_t) p * pf * bands);
    return out;
}

juce::Range<int> FeatureExtractor::patchSampleRange (int p) const
{
    const int start = p * config.patchHopFrames * config.hopSamples;
    return { start, start + config.patchFrames * config.hopSamples + config.windowSamples - config.hopSamples };
}

} // namespace eyra::intelligence
