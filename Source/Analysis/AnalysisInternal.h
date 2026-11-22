#pragma once

// Internal to the analysis engine: shared state passed between the analysis stages.

#include "AudioAnalysis.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <vector>

namespace eyra::analysis::detail
{

inline double powerToDb (double p) noexcept { return 10.0 * std::log10 (std::max (p, 1.0e-20)); }

/** Windowed power spectrum, calibrated so a sine of amplitude A peaks at A^2 (i.e. 20 log10 A dBFS). */
class PowerSpectrum
{
public:
    explicit PowerSpectrum (int fftSize);

    int size() const noexcept { return n; }
    int numBins() const noexcept { return n / 2 + 1; }

    /** `input` must hold size() samples. `out` receives numBins() values. */
    void compute (const float* input, float* out);

    /** Autocorrelation of the windowed frame, normalised to 1 at lag 0 and corrected for the window taper.
        `out` receives size()/2 lags. Returns false for a silent frame. */
    bool autocorrelation (const float* input, float* out);

private:
    int n;
    juce::dsp::FFT fft, fftDouble;
    std::vector<float> window, windowAutocorr, buffer, buffer2;
    float gain = 1.0f;
};

/** Fractional-octave smoothing in the power domain (prefix sums). Window = [f / 2^(w/2), f * 2^(w/2)]. */
std::vector<double> smoothFractionalOctave (const std::vector<double>& power, double binHz, double widthOctaves, int minHalfWidthBins = 1);

/** Median of dB values over a fractional-octave window around `bin`. */
double medianDbAround (const std::vector<double>& db, int bin, double binHz, double widthOctaves, int minHalfWidthBins, std::vector<double>& scratch);

/** Vertex offset (-0.5..0.5) of a parabola through three points. */
inline double parabolicOffset (double a, double b, double c) noexcept
{
    const double d = a - 2.0 * b + c;
    return std::abs (d) < 1.0e-12 ? 0.0 : juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / d);
}

struct Candidate
{
    double frequency = 0.0;
    int bin = 0;
    double prominenceDb = 0.0;
    double levelDb = 0.0;
    double q = 0.0;
    double persistence = 0.0;
    int segmentsPresent = 0;
    int harmonicNumber = 0;
    bool fromTailSpectrum = false;
};

struct Context
{
    Context (const AnalysisSettings& s, double rate) : settings (s), sampleRate (rate) {}

    const AnalysisSettings& settings;
    double sampleRate;
    std::vector<float> signal; // DC removed
    double dcOffset = 0.0;
    double rms = 0.0;

    // Activity gate
    int gateBlock = 0;
    std::vector<bool> activeBlock;
    double activeSeconds = 0.0;
    /** True if more than minFraction of the gate blocks in [sample, sample + length) hold signal
        (at least half by default; 0 = any). */
    bool isActiveAt (int sample, int length, double minFraction = 0.5) const;

    // Short-time analysis (~85 ms window, ~10.7 ms hop): transients, decay, pitch
    int shortSize = 0, shortHop = 0;
    std::vector<double> bandCentres;                 // 1/24-octave bands
    std::vector<std::vector<float>> shortBandPower;  // [frame][band]
    std::vector<float> shortEnergyDb, flux;
    std::vector<bool> shortActive;
    std::vector<double> frameF0;                     // 0 = unvoiced / inactive
    std::vector<int> onsets;                         // short-frame indices
    double shortFrameSeconds() const { return (double) shortHop / sampleRate; }
    int bandIndexFor (double hz) const;

    // Long-window analysis (~0.34 s window): fine frequency resolution
    int longSize = 0;
    double longBinHz = 0.0;
    std::vector<std::vector<float>> longFramesDb;    // selected active frames, dB per bin
    std::vector<double> ltasPower, ltasDb, baselineDb;
    int maxAnalysisBin = 0;

    // Third-octave band levels of the LTAS
    std::vector<double> thirdOctaveCentres, thirdOctaveDb;
    int contentLowBand = 0, contentHighBand = -1;

    AnalysisResult result;
    std::vector<Candidate> resonanceCandidates;
};

// Stages, in the order they run.
void computeShortTimeAnalysis (Context&);
void estimatePitch (Context&);
void detectOnsets (Context&);
void computeLongTimeSpectrum (Context&);
void detectResonances (Context&);      // narrow peaks, hum, ringing
void analyseTonalBalance (Context&);   // DC, rumble, third-octave bands, buildup, tilt (narrow peaks removed)

/** Decay rate (dB/s, negative when decaying) of a 1/24-octave band and of its neighbourhood, median over onsets.
    Returns false when there are too few usable onsets. */
bool measureBandDecay (const Context&, int band, double& bandRate, double& neighbourRate, int& onsetsUsed);

} // namespace eyra::analysis::detail
