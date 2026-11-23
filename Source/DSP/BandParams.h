#pragma once

#include <array>

namespace eyra::dsp
{

/** The filter shapes the corrective EQ asks for. Mapped onto the EQ's band types by the parameter code. */
enum class FilterType : int
{
    Bell = 0,
    LowShelf,
    HighShelf,
    HighPass,
    LowPass,
    Notch,
    BandPass,
    Tilt
};
inline constexpr int kNumFilterTypes = 8;

/** Slopes available for high-pass / low-pass bands, in dB/octave. Order = slope / 6. */
inline constexpr std::array<int, 8> kSlopesDbPerOct { 6, 12, 18, 24, 36, 48, 72, 96 };
inline constexpr int kDefaultSlopeIndex = 1; // 12 dB/oct

/** One EQ band as the corrective EQ describes it. */
struct BandParams
{
    bool used = false;
    bool bypassed = false;
    FilterType type = FilterType::Bell;
    double frequency = 1000.0;
    double gainDb = 0.0;
    double q = 0.7071067811865476;
    int slopeIndex = kDefaultSlopeIndex;

    bool operator== (const BandParams&) const = default;
};

} // namespace eyra::dsp
