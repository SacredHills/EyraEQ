#pragma once

#include <array>
#include <complex>

namespace eyra::dsp
{

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

enum class StereoPlacement : int
{
    Stereo = 0,
    Left,
    Right,
    Mid,
    Side
};
inline constexpr int kNumStereoPlacements = 5;

/** Slopes available for high-pass / low-pass bands, in dB/octave. Order = slope / 6. */
inline constexpr std::array<int, 8> kSlopesDbPerOct { 6, 12, 18, 24, 36, 48, 72, 96 };
inline constexpr int kDefaultSlopeIndex = 1; // 12 dB/oct

/** Maximum number of second-order sections a single band can need (96 dB/oct = order 16). */
inline constexpr int kMaxSections = 8;

inline constexpr double kMinFrequency = 10.0;
inline constexpr double kMaxFrequency = 30000.0;
inline constexpr double kMinQ = 0.025;
inline constexpr double kMaxQ = 40.0;
inline constexpr double kMaxGainDb = 30.0;

/** Normalised (a0 == 1) biquad: H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2). */
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    std::complex<double> response (double omega) const noexcept;
    double magnitude (double omega) const noexcept { return std::abs (response (omega)); }
    bool isStable() const noexcept;
};

struct FilterSpec
{
    FilterType type = FilterType::Bell;
    double frequency = 1000.0;
    double gainDb = 0.0;
    double q = 0.7071067811865476;
    int slopeIndex = kDefaultSlopeIndex;
};

struct SectionCascade
{
    std::array<BiquadCoeffs, kMaxSections> sections {};
    int numSections = 0;

    std::complex<double> response (double omega) const noexcept;
    double magnitudeDb (double frequencyHz, double sampleRate) const noexcept;
};

/** Whether the band's gain / Q / slope controls have any effect for a given type. */
bool typeUsesGain (FilterType) noexcept;
bool typeUsesQ (FilterType, int slopeIndex) noexcept;
bool typeUsesSlope (FilterType) noexcept;

/** Highest design frequency used for a given sample rate (designs are clamped below Nyquist). */
double maxDesignFrequency (double sampleRate) noexcept;

/** Designs the cascade of sections for a band. Always returns stable sections. */
SectionCascade design (const FilterSpec& spec, double sampleRate) noexcept;

/** Band-pass used for soloing (auditioning) a band. */
BiquadCoeffs designAuditionBandPass (double frequency, double q, double sampleRate) noexcept;

/** Exposed for testing: analogue prototype magnitude evaluation and matched design. */
struct AnalogBiquad
{
    // H(s) = (n2 s^2 + n1 s + n0) / (d2 s^2 + d1 s + d0), with s normalised so that w0 = 1.
    double n2 = 0, n1 = 0, n0 = 1, d2 = 0, d1 = 0, d0 = 1;
    bool firstOrder = false; // when true, only n1, n0, d1, d0 are used

    double magnitudeSquared (double normalisedOmega) const noexcept;
};

BiquadCoeffs designMatched (const AnalogBiquad& prototype, double omega0) noexcept;
BiquadCoeffs designBilinear (const AnalogBiquad& prototype, double omega0) noexcept;

} // namespace eyra::dsp
