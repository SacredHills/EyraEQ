#pragma once

// The EQ engine: eight bands (bell, shelves, low / high cut with 6..96 dB/oct slopes, notch, band pass), each of which
// can be static, Dynamic (the band's gain moves with the level in that band) or Spectral (the dynamics work frequency by
// frequency inside the band, so only the frequencies that poke out get turned down or up).
// Taken from the Ravenyr DAW's built-in EQ.

#include "Biquad.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <vector>

namespace eyra::dsp
{

inline constexpr int eqBandCount = 8;
enum EqType { eqBell, eqLowShelf, eqHighShelf, eqLowCut, eqHighCut, eqNotch, eqBandPass };
enum EqDyn { eqDynOff, eqDynDynamic, eqDynSpectral };
inline const juce::StringArray eqTypeNames { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass" };
inline const juce::StringArray eqSlopeNames { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct" };
inline constexpr int eqSlopeDb[] { 6, 12, 18, 24, 36, 48, 72, 96 };
inline const juce::StringArray eqDynNames { "Off", "Dynamic", "Spectral" };
inline const juce::Colour eqBandColours[eqBandCount] { juce::Colour (0xfff0a030), juce::Colour (0xff46c38a), juce::Colour (0xff4c9bf5),
                                                juce::Colour (0xffe5484d), juce::Colour (0xffb57cf0), juce::Colour (0xff3cc8c8),
                                                juce::Colour (0xfff07cb4), juce::Colour (0xffc8c83c) };
constexpr float eqGainRange = 30.0f;

inline bool eqHasGain (int type) { return type == eqBell || type == eqLowShelf || type == eqHighShelf; }
inline bool eqIsCut (int type) { return type == eqLowCut || type == eqHighCut; }

inline juce::String eqId (int band, const char* what) { return "b" + juce::String (band + 1) + what; }

/** One band's settings. All floats (choices as their index) so a whole set compares with memcmp. */
struct EqBand
{
    float on = 0, type = 0, freq = 1000, gain = 0, q = 1, slope = 1, dyn = 0, range = -6, thr = -24, use = 0, atk = 10, rel = 120, arel = 0;   // arel: release follows the signal; use: the band exists (on: not bypassed)
    int typeI() const { return juce::roundToInt (type); }
    int dynI() const { return juce::roundToInt (dyn); }
    bool isUsed() const { return use > 0.5f; }
    bool isOn() const { return isUsed() && on > 0.5f; }
    bool dynamic() const { return isOn() && dynI() == eqDynDynamic && eqHasGain (typeI()); }
    bool spectral() const { return isOn() && dynI() == eqDynSpectral && ! eqIsCut (typeI()); }
};
struct EqAll
{
    std::array<EqBand, eqBandCount> b {};
    float attack = 10, release = 120, out = 0;
    bool operator== (const EqAll& o) const { return std::memcmp (this, &o, sizeof (*this)) == 0; }
    bool anySpectral() const
    {
        for (auto& x : b) if (x.spectral()) return true;
        return false;
    }
};

/** The filter sections of one band (a cut needs up to eight). */
struct EqBandFilter
{
    std::array<Biquad, 8> sec;
    int n = 0;
    void design (const EqBand& b, double sr, float gainDb) noexcept
    {
        const double f = juce::jlimit (10.0, sr * 0.49, (double) b.freq), q = juce::jlimit (0.1, 30.0, (double) b.q);
        n = 1;
        switch (b.typeI())
        {
            case eqBell:      sec[0].set (Biquad::peak, sr, f, q, gainDb); break;
            case eqLowShelf:  sec[0].set (Biquad::lowShelf, sr, f, juce::jlimit (0.25, 4.0, q), gainDb); break;
            case eqHighShelf: sec[0].set (Biquad::highShelf, sr, f, juce::jlimit (0.25, 4.0, q), gainDb); break;
            case eqNotch:     sec[0].set (Biquad::notch, sr, f, q); break;
            case eqBandPass:  sec[0].set (Biquad::bandPass, sr, f, q); break;
            case eqLowCut:
            case eqHighCut:
            {
                // Butterworth of order slope / 6: one first-order section if odd, then second-order sections; the Q
                // knob adds resonance to the sharpest section
                const bool hp = b.typeI() == eqLowCut;
                const int order = eqSlopeDb[juce::jlimit (0, 7, juce::roundToInt (b.slope))] / 6;
                n = 0;
                if (order % 2 == 1) sec[(size_t) n++].setFirstOrder (hp, sr, f);
                for (int k = 1; k <= order / 2; ++k)
                {
                    double qk = 1.0 / (2.0 * std::sin ((2 * k - 1) * juce::MathConstants<double>::pi / (2.0 * order)));
                    if (k == 1) qk *= q / 0.7071;
                    sec[(size_t) n++].set (hp ? Biquad::highPass : Biquad::lowPass, sr, f, qk);
                }
                break;
            }
            default: sec[0].setIdentity(); break;
        }
    }
    void process (float* d, int len) noexcept
    {
        for (int s = 0; s < n; ++s) sec[(size_t) s].process (d, len);
    }
    double magnitude (double freq, double sr) const noexcept
    {
        double m = 1.0;
        for (int s = 0; s < n; ++s) m *= sec[(size_t) s].magnitude (freq, sr);
        return m;
    }
};

/** How strongly a band's dynamics act at `f` (1 at its centre), for spectral mode. */
inline float eqSpectralWeight (const EqBand& b, double f)
{
    const double f0 = juce::jmax (10.0, (double) b.freq);
    switch (b.typeI())
    {
        case eqLowShelf:  return (float) (1.0 / (1.0 + std::pow (f / f0, 4.0)));
        case eqHighShelf: return (float) (1.0 / (1.0 + std::pow (f0 / juce::jmax (1.0, f), 4.0)));
        default:
        {
            const double x = juce::jmax (0.1, (double) b.q) * (f / f0 - f0 / juce::jmax (1.0, f));
            return (float) (1.0 / (1.0 + x * x));
        }
    }
}

/** dB change for a level `over` dB above the threshold: a 4:1-like move toward `range`, never past it. */
inline float eqDynamicDelta (float over, float range)
{
    if (over <= 0.0f) return 0.0f;
    const float d = juce::jmin (std::abs (range), over * 0.75f);
    return range < 0.0f ? -d : d;
}

/** Short-time Fourier processor for spectral dynamics (2048-point frames, 75 % overlap, latency = frame size). */
struct EqSpectral
{
    static constexpr int order = 11, size = 1 << order, hop = size / 4, bins = size / 2 + 1, displayPoints = 160;
    juce::dsp::FFT fft { order };
    std::vector<float> window = std::vector<float> ((size_t) size), frame[2], inRing[2], outRing[2];
    std::vector<float> env = std::vector<float> ((size_t) bins, -120.0f), gainDb = std::vector<float> ((size_t) bins, 0.0f);
    std::vector<float> weights = std::vector<float> ((size_t) (bins * eqBandCount), 0.0f);
    std::vector<float> binAtk = std::vector<float> ((size_t) bins, 0.5f), binRel = std::vector<float> ((size_t) bins, 0.1f);   // per-frame coefficients
    std::array<std::atomic<float>, displayPoints> display {};
    float norm = 1.0f;
    int pos = 0, hopCount = 0;

    EqSpectral()
    {
        float sum = 0.0f;
        for (int i = 0; i < size; ++i)
        {
            window[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) size));
            sum += window[(size_t) i];
        }
        norm = 2.0f / sum;   // a full-scale sine reads 0 dB in its bin
        for (int c = 0; c < 2; ++c)
        {
            frame[c].assign ((size_t) size * 2, 0.0f);
            inRing[c].assign ((size_t) size, 0.0f);
            outRing[c].assign ((size_t) size, 0.0f);
        }
        for (auto& d : display) d.store (0.0f);
    }
    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (inRing[c].begin(), inRing[c].end(), 0.0f);
            std::fill (outRing[c].begin(), outRing[c].end(), 0.0f);
        }
        std::fill (env.begin(), env.end(), -120.0f);
        std::fill (gainDb.begin(), gainDb.end(), 0.0f);
        for (auto& d : display) d.store (0.0f);
        pos = hopCount = 0;
    }
    void setWeights (const EqAll& s, double sr) noexcept
    {
        for (int b = 0; b < eqBandCount; ++b)
            for (int k = 0; k < bins; ++k)
                weights[(size_t) (b * bins + k)] = s.b[(size_t) b].spectral() ? eqSpectralWeight (s.b[(size_t) b], (double) k * sr / size) : 0.0f;
        // each frequency follows the attack / release of the band that acts most there
        const float frameSec = (float) hop / (float) sr;
        for (int k = 0; k < bins; ++k)
        {
            int best = -1;
            float bw = 0.0f;
            for (int b = 0; b < eqBandCount; ++b)
                if (weights[(size_t) (b * bins + k)] > bw) { bw = weights[(size_t) (b * bins + k)]; best = b; }
            const float a = best >= 0 ? s.b[(size_t) best].atk : 10.0f, r = best >= 0 ? (s.b[(size_t) best].arel > 0.5f ? 80.0f : s.b[(size_t) best].rel) : 120.0f;   // auto: a medium release
            binAtk[(size_t) k] = 1.0f - std::exp (-frameSec / juce::jmax (0.0005f, a * 0.001f));
            binRel[(size_t) k] = 1.0f - std::exp (-frameSec / juce::jmax (0.005f, r * 0.001f));
        }
    }

    void process (float* L, float* R, int n, const EqAll& s, double sr) noexcept
    {
        float* io[2] = { L, R };
        const int chans = R != nullptr ? 2 : 1;
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < chans; ++c)
            {
                const float x = io[c][i];
                io[c][i] = outRing[c][(size_t) pos];
                outRing[c][(size_t) pos] = 0.0f;
                inRing[c][(size_t) pos] = x;
            }
            if (++pos == size) pos = 0;
            if (++hopCount == hop) { hopCount = 0; processFrame (chans, s, sr); }
        }
    }

private:
    void processFrame (int chans, const EqAll& s, double sr) noexcept
    {
        for (int c = 0; c < chans; ++c)
        {
            auto& f = frame[c];
            for (int j = 0; j < size; ++j) f[(size_t) j] = inRing[c][(size_t) ((pos + j) % size)] * window[(size_t) j];
            std::fill (f.begin() + size, f.end(), 0.0f);
            fft.performRealOnlyForwardTransform (f.data(), true);
        }
        for (int k = 0; k < bins; ++k)
        {
            float mag2 = 0.0f;
            for (int c = 0; c < chans; ++c)
            {
                const float re = frame[c][(size_t) (2 * k)], im = frame[c][(size_t) (2 * k + 1)];
                mag2 = juce::jmax (mag2, re * re + im * im);
            }
            const float lvl = 10.0f * std::log10 (mag2 * norm * norm + 1.0e-12f);
            auto& e = env[(size_t) k];
            e += (lvl - e) * (lvl > e ? binAtk[(size_t) k] : binRel[(size_t) k]);
            float g = 0.0f;
            for (int b = 0; b < eqBandCount; ++b)
                if (const float w = weights[(size_t) (b * bins + k)]; w > 0.001f)
                    g += w * eqDynamicDelta (e - s.b[(size_t) b].thr, s.b[(size_t) b].range);
            g = juce::jlimit (-eqGainRange, eqGainRange, g);
            gainDb[(size_t) k] = g;
            const float lin = std::pow (10.0f, g / 20.0f);
            for (int c = 0; c < chans; ++c)
            {
                frame[c][(size_t) (2 * k)] *= lin;
                frame[c][(size_t) (2 * k + 1)] *= lin;
            }
        }
        const float scale = 0.5f;   // sqrt-Hann in and out at 75 % overlap sums to 2
        for (int c = 0; c < chans; ++c)
        {
            auto& f = frame[c];
            fft.performRealOnlyInverseTransform (f.data());
            for (int j = 0; j < size; ++j) outRing[c][(size_t) ((pos + j) % size)] += f[(size_t) j] * window[(size_t) j] * scale;
        }
        for (int d = 0; d < displayPoints; ++d)   // for the display: log-spaced 20 Hz .. 20 kHz
        {
            const double fq = 20.0 * std::pow (1000.0, (double) d / (displayPoints - 1));
            const int k = juce::jlimit (0, bins - 1, (int) std::round (fq / sr * size));
            display[(size_t) d].store (gainDb[(size_t) k], std::memory_order_relaxed);
        }
    }
};


/** The EQ itself. Reads its settings straight from the processor's parameters; audio in place (R is null for mono). */
class EqCore final : private juce::AudioProcessorParameter::Listener, private juce::AsyncUpdater
{
public:
    EqCore (juce::AudioProcessor& owner, juce::AudioProcessorValueTreeState& state);
    ~EqCore() override;

    void prepare (double sampleRate);
    void process (float* L, float* R, int numSamples) noexcept;

    EqAll settings() const noexcept;
    juce::RangedAudioParameter* bandParam (int band, int k) const { return bandParams[(size_t) band][(size_t) k]; }
    double getSampleRate() const noexcept { return sampleRate; }
    int getLatencySamples() const noexcept { return spectralOn.load() ? EqSpectral::size : 0; }

    /** Latency follows whether any band is spectral. Message thread. */
    void updateLatency();

    // shared with the editor
    std::array<std::atomic<float>, eqBandCount> dynNow {};   // live dynamic gain change per band (dB)
    EqSpectral spectral;
    std::atomic<bool> spectralOn { false };
    std::atomic<int> listenBand { -1 };   // band being listened to (the editor's Listen button / Alt-drag), -1: none

private:
    static float value (juce::RangedAudioParameter* p) noexcept { return p->convertFrom0to1 (p->getValue()); }
    void parameterValueChanged (int, float) override { triggerAsyncUpdate(); }
    void parameterGestureChanged (int, bool) override {}
    void handleAsyncUpdate() override { updateLatency(); }

    juce::AudioProcessor& owner;
    double sampleRate = 48000.0;
    std::array<std::array<juce::RangedAudioParameter*, 13>, eqBandCount> bandParams {};
    juce::RangedAudioParameter* outP = nullptr;
    std::array<std::array<EqBandFilter, eqBandCount>, 2> filters;
    std::array<Biquad, eqBandCount> detectors;
    std::array<float, eqBandCount> envelopes {}, applied {};
    std::array<bool, eqBandCount> active {};
    std::array<std::vector<float>, 2> listenIn;
    std::array<std::array<Biquad, 2>, 2> listenFilters;   // per channel, two sections: a steeper slice
    int listenDesigned = -2;
    float listenFreq = 0.0f, listenQ = 0.0f;
    EqAll last {};
    bool spectralWasOn = false;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outGain;
};

} // namespace eyra::dsp
