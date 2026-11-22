#pragma once

#include "FilterDesign.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace eyra::dsp
{

/** Parameter snapshot for one band, read once per audio block. */
struct BandParams
{
    bool used = false;
    bool bypassed = false;
    FilterType type = FilterType::Bell;
    double frequency = 1000.0;
    double gainDb = 0.0;
    double q = 0.7071067811865476;
    int slopeIndex = kDefaultSlopeIndex;
    StereoPlacement placement = StereoPlacement::Stereo;

    bool operator== (const BandParams&) const = default;
};

/**
    One EQ band: a cascade of up to kMaxSections double-precision biquads (transposed direct form II)
    for up to two channels.

    Continuous parameters (frequency, gain, Q) are smoothed and the coefficients recomputed every
    kCoefficientInterval samples. Discrete changes (type, slope, stereo placement) and enabling /
    bypassing are click-free: the band fades its wet signal out, switches, and fades back in.

    Real-time safe: no allocation or locking after prepare().
*/
class EQBand
{
public:
    static constexpr int kCoefficientInterval = 16;

    void prepare (double sampleRate);
    void reset() noexcept;

    /** Sets new target parameters. Call from the audio thread at the start of a block. */
    void setTarget (const BandParams& params) noexcept;

    /** True when the band contributes nothing and can be skipped entirely. */
    bool isSilent() const noexcept;

    /** Placement the band is currently processing with (may lag the target during a fade). */
    StereoPlacement currentPlacement() const noexcept { return current.placement; }

    /** Processes one or two channels in place. ch1 may be nullptr. */
    void process (double* ch0, double* ch1, int numSamples) noexcept;

    /** Smoothed values for auditioning. */
    double currentFrequency() const noexcept { return freqSmoothed.getCurrentValue(); }
    double currentQ() const noexcept { return qSmoothed.getCurrentValue(); }

private:
    void updateCoefficients() noexcept;
    void applyPendingDiscreteChange() noexcept;
    bool discreteConfigDiffers() const noexcept;
    double activeTarget() const noexcept;

    struct State
    {
        double s1 = 0.0, s2 = 0.0;
    };

    double sampleRate = 44100.0;
    BandParams target, current;
    SectionCascade cascade;
    std::array<std::array<State, kMaxSections>, 2> state {};

    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> freqSmoothed, qSmoothed;
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> gainSmoothed;
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> mix;

    int samplesUntilCoeffUpdate = 0;
    bool coefficientsValid = false;
};

} // namespace eyra::dsp
