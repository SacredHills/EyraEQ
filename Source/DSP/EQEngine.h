#pragma once

#include "EQBand.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <vector>

namespace eyra::dsp
{

/** Receives audio for display (the spectrum analyser). Implementations must be real-time safe. */
struct AudioTap
{
    virtual ~AudioTap() = default;
    virtual void pushSamples (const double* left, const double* right, int numSamples) noexcept = 0;
};

/**
    The complete EQ signal path:

        input gain -> [pre tap] -> bands (in series, each in its stereo placement)
                   -> optional solo audition -> output gain -> master bypass crossfade -> [post tap]

    Processing is done in double precision on internal scratch buffers. Bands placed on Mid or Side
    are processed in the M/S domain; the conversion is exact so mixing placements is transparent.
    Zero latency (minimum-phase IIR).
*/
class EQEngine
{
public:
    static constexpr int kMaxBands = 24;

    void prepare (double sampleRate, int maximumBlockSize, int numChannels);
    void reset() noexcept;

    void setBandParams (int bandIndex, const BandParams& params) noexcept;
    void setInputGainDb (double db) noexcept;
    void setOutputGainDb (double db) noexcept;
    void setMasterBypass (bool shouldBypass) noexcept;
    /** -1 disables soloing. */
    void setSoloBand (int bandIndex) noexcept;

    void setTaps (AudioTap* preTap, AudioTap* postTap) noexcept;

    void process (juce::AudioBuffer<float>& buffer) noexcept;

    double getSampleRate() const noexcept { return sampleRate; }

private:
    void processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

    double sampleRate = 44100.0;
    int maxBlock = 0;
    int numChannelsPrepared = 0;

    std::array<EQBand, kMaxBands> bands;
    std::array<BandParams, kMaxBands> bandParams {};

    std::vector<double> work[2], dry[2];

    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> inputGain { 1.0 }, outputGain { 1.0 };
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> wetMix { 1.0 }, soloMix { 0.0 };

    int soloBand = -1;
    int activeSoloBand = -1;
    BiquadCoeffs auditionCoeffs;
    std::array<std::array<double, 2>, 2> auditionState {};

    AudioTap* pre = nullptr;
    AudioTap* post = nullptr;
};

} // namespace eyra::dsp
