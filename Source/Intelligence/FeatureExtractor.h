#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace eyra::intelligence
{

/**
    YAMNet's audio front end: 16 kHz mono, 25 ms periodic-Hann frames every 10 ms, 512-point FFT
    magnitude, the model's own mel filterbank (64 bands, exported from TensorFlow), log(mel + offset),
    then 96-frame patches every 48 frames (0.96 s every 0.48 s).

    Verified against the TensorFlow reference by the tests (Models/yamnet/reference).
*/
class FeatureExtractor
{
public:
    struct Config
    {
        int sampleRate = 16000;
        int windowSamples = 400;
        int hopSamples = 160;
        int fftSize = 512;
        int melBands = 64;
        float logOffset = 0.001f;
        int patchFrames = 96;
        int patchHopFrames = 48;
    };

    /** Reads the "frontend" section of a model manifest and its mel matrix. */
    bool configure (const juce::var& frontend, const juce::File& modelDirectory, juce::String& error);
    bool isConfigured() const noexcept { return ! melMatrix.empty(); }
    const Config& getConfig() const noexcept { return config; }

    /** Low-pass filters (below the target Nyquist) and resamples to the front-end rate. */
    std::vector<float> resample (const float* input, int numSamples, double sourceRate) const;

    /** Pads with silence the way YAMNet does, to a whole number of patches. */
    std::vector<float> padForPatches (std::vector<float> waveform) const;

    /** Log-mel spectrogram, [frames x melBands] row-major. */
    std::vector<float> logMel (const std::vector<float>& waveform, int& numFrames) const;

    /** Patches, [patches x patchFrames x melBands] row-major. */
    std::vector<float> patches (const std::vector<float>& logMel, int numFrames, int& numPatches) const;

    /** Sample range of the audio a patch covers (for gating by level). */
    juce::Range<int> patchSampleRange (int patchIndex) const;

private:
    Config config;
    std::vector<float> melMatrix; // [(fftSize/2 + 1) x melBands]
};

} // namespace eyra::intelligence
