#include "DSP/FilterDesign.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <numbers>

using namespace eyra::dsp;

namespace
{
double analogDb (const FilterSpec& s, double hz)
{
    // Reference analogue responses with the same prototypes the design uses.
    const double w = hz / s.frequency;
    const double a = std::pow (10.0, s.gainDb / 40.0);
    AnalogBiquad p;
    if (s.type == FilterType::Bell)
    {
        p.n2 = 1; p.n1 = a / s.q; p.n0 = 1; p.d2 = 1; p.d1 = 1.0 / (a * s.q); p.d0 = 1;
    }
    else if (s.type == FilterType::LowShelf)
    {
        p.n2 = a; p.n1 = a * std::sqrt (a) / s.q; p.n0 = a * a; p.d2 = a; p.d1 = std::sqrt (a) / s.q; p.d0 = 1;
    }
    else
    {
        jassert (s.type == FilterType::HighShelf);
        p.n2 = a * a; p.n1 = a * std::sqrt (a) / s.q; p.n0 = a; p.d2 = 1; p.d1 = std::sqrt (a) / s.q; p.d0 = a;
    }
    return 10.0 * std::log10 (p.magnitudeSquared (w));
}

class FilterDesignTests final : public juce::UnitTest
{
public:
    FilterDesignTests() : juce::UnitTest ("FilterDesign", "DSP") {}

    void runTest() override
    {
        beginTest ("Bell: exact gain at centre frequency");
        {
            double maxErr = 0.0;
            for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
                for (double f : { 20.0, 100.0, 1000.0, 5000.0, 12000.0, 18000.0 })
                    for (double g : { -24.0, -6.0, -1.0, 1.0, 6.0, 24.0 })
                        for (double q : { 0.1, 0.5, 1.0, 4.0, 20.0 })
                        {
                            const FilterSpec s { FilterType::Bell, f, g, q, 1 };
                            maxErr = std::max (maxErr, std::abs (design (s, sr).magnitudeDb (f, sr) - g));
                        }
            logMessage ("max error at centre: " + juce::String (maxErr, 5) + " dB");
            expectLessThan (maxErr, 0.01);
        }

        beginTest ("Bell: tracks the analogue response far better than a bilinear design");
        {
            // The matched design is exact at DC, the centre frequency and Nyquist. Between those it
            // stays close to the analogue prototype; the error only grows when the analogue bell
            // itself extends past Nyquist (which no second-order digital filter can reproduce).
            double maxLow = 0.0, maxAll = 0.0, maxBilinear = 0.0;
            for (double sr : { 44100.0, 48000.0, 96000.0 })
                for (double f = 30.0; f < std::min (0.45 * sr, kMaxFrequency); f *= 1.15)
                    for (double g : { -18.0, -6.0, 6.0, 18.0 })
                        for (double q : { 0.3, 0.7, 1.0, 3.0, 10.0 })
                        {
                            const FilterSpec s { FilterType::Bell, f, g, q, 1 };
                            const auto cascade = design (s, sr);
                            const double a = std::pow (10.0, g / 40.0);
                            AnalogBiquad proto;
                            proto.n2 = 1; proto.n1 = a / q; proto.n0 = 1; proto.d2 = 1; proto.d1 = 1.0 / (a * q); proto.d0 = 1;
                            const auto bilinear = designBilinear (proto, 2.0 * std::numbers::pi * f / sr);

                            for (double hz = 20.0; hz < 0.45 * sr; hz *= 1.1)
                            {
                                const double ref = analogDb (s, hz);
                                const double err = std::abs (cascade.magnitudeDb (hz, sr) - ref);
                                const double errBilinear = std::abs (20.0 * std::log10 (bilinear.magnitude (2.0 * std::numbers::pi * hz / sr)) - ref);
                                if (f <= sr / 6.0 && hz <= 0.35 * sr)
                                    maxLow = std::max (maxLow, err);
                                maxAll = std::max (maxAll, err);
                                maxBilinear = std::max (maxBilinear, errBilinear);
                            }
                        }
            logMessage ("max deviation from analogue: " + juce::String (maxLow, 3) + " dB (f0 <= fs/6), "
                        + juce::String (maxAll, 2) + " dB (full range); bilinear full range: " + juce::String (maxBilinear, 2) + " dB");
            expectLessThan (maxLow, 0.8);
            expectLessThan (maxAll, 0.5 * maxBilinear);
        }

        beginTest ("Bell: bandwidth (Q) accuracy at half-gain points");
        {
            // For this bell, the analogue half-gain (in dB) points lie at w and 1/w where the
            // analogue magnitude equals G/2 dB. Find them by bisection and check the digital response.
            const double sr = 48000.0;
            for (double q : { 0.7, 2.0, 6.0 })
            {
                const FilterSpec s { FilterType::Bell, 1000.0, 12.0, q, 1 };
                double lo = 1000.0, hi = 20000.0;
                for (int i = 0; i < 80; ++i)
                {
                    const double mid = std::sqrt (lo * hi);
                    (analogDb (s, mid) > 6.0 ? lo : hi) = mid;
                }
                const double upper = lo;
                const double lower = 1.0e6 / upper;
                const auto c = design (s, sr);
                expectWithinAbsoluteError (c.magnitudeDb (upper, sr), 6.0, 0.15);
                expectWithinAbsoluteError (c.magnitudeDb (lower, sr), 6.0, 0.15);
            }
        }

        beginTest ("Shelves: DC, Nyquist-side and transition gains");
        {
            const double sr = 48000.0;
            for (double g : { -18.0, -6.0, 6.0, 18.0 })
            {
                const auto ls = design ({ FilterType::LowShelf, 200.0, g, 0.7071, 1 }, sr);
                expectWithinAbsoluteError (ls.magnitudeDb (5.0, sr), g, 0.05);
                expectWithinAbsoluteError (ls.magnitudeDb (200.0, sr), g * 0.5, 0.01);
                expectWithinAbsoluteError (ls.magnitudeDb (20000.0, sr), 0.0, 0.05);

                const auto hs = design ({ FilterType::HighShelf, 5000.0, g, 0.7071, 1 }, sr);
                expectWithinAbsoluteError (hs.magnitudeDb (20.0, sr), 0.0, 0.05);
                expectWithinAbsoluteError (hs.magnitudeDb (5000.0, sr), g * 0.5, 0.01);
                expectWithinAbsoluteError (hs.magnitudeDb (23900.0, sr), g, 0.6);
            }
        }

        beginTest ("High / low pass: -3 dB at cutoff and correct slope");
        {
            const double sr = 96000.0;
            for (int si = 0; si < (int) kSlopesDbPerOct.size(); ++si)
            {
                const int slope = kSlopesDbPerOct[(size_t) si];
                const auto hp = design ({ FilterType::HighPass, 1000.0, 0.0, 0.7071, si }, sr);
                const auto lp = design ({ FilterType::LowPass, 1000.0, 0.0, 0.7071, si }, sr);

                expectWithinAbsoluteError (hp.magnitudeDb (1000.0, sr), -3.01, 0.05, "HP " + juce::String (slope));
                expectWithinAbsoluteError (lp.magnitudeDb (1000.0, sr), -3.01, 0.05, "LP " + juce::String (slope));

                // Asymptotic slope between 2 and 3 octaves away from the cutoff.
                const double hpSlope = hp.magnitudeDb (250.0, sr) - hp.magnitudeDb (125.0, sr);
                const double lpSlope = lp.magnitudeDb (4000.0, sr) - lp.magnitudeDb (8000.0, sr);
                expectWithinAbsoluteError (hpSlope, (double) slope, 0.6, "HP slope " + juce::String (slope));
                expectWithinAbsoluteError (lpSlope, (double) slope, 0.6, "LP slope " + juce::String (slope));

                // Passband flat
                expectWithinAbsoluteError (hp.magnitudeDb (20000.0, sr), 0.0, 0.05);
                expectWithinAbsoluteError (lp.magnitudeDb (50.0, sr), 0.0, 0.05);
            }

            // Resonance control raises the response at the cutoff (12 dB/oct: |H(fc)| = Q).
            const auto res = design ({ FilterType::HighPass, 1000.0, 0.0, 4.0, 1 }, sr);
            expectWithinAbsoluteError (res.magnitudeDb (1000.0, sr), 20.0 * std::log10 (4.0), 0.05);
        }

        beginTest ("Notch, band pass and tilt");
        {
            const double sr = 48000.0;
            const auto notch = design ({ FilterType::Notch, 3000.0, 0.0, 4.0, 1 }, sr);
            expectLessThan (notch.magnitudeDb (3000.0, sr), -60.0);
            expectWithinAbsoluteError (notch.magnitudeDb (300.0, sr), 0.0, 0.05);

            const auto bp = design ({ FilterType::BandPass, 500.0, 0.0, 2.0, 1 }, sr);
            expectWithinAbsoluteError (bp.magnitudeDb (500.0, sr), 0.0, 0.01);
            expectLessThan (bp.magnitudeDb (50.0, sr), -20.0);

            const auto tilt = design ({ FilterType::Tilt, 1000.0, 8.0, 1.0, 1 }, sr);
            expectWithinAbsoluteError (tilt.magnitudeDb (10.0, sr), -4.0, 0.05);
            expectWithinAbsoluteError (tilt.magnitudeDb (1000.0, sr), 0.0, 0.05);
            expectWithinAbsoluteError (tilt.magnitudeDb (22000.0, sr), 4.0, 0.15);
        }

        beginTest ("Stability across the full parameter space");
        {
            int checked = 0, unstable = 0;
            for (double sr : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
                for (int t = 0; t < kNumFilterTypes; ++t)
                    for (int fi = 0; fi < 30; ++fi)
                    {
                        const double f = kMinFrequency * std::pow (kMaxFrequency / kMinFrequency, fi / 29.0);
                        for (double q : { kMinQ, 0.1, 0.3, 0.7071, 1.5, 5.0, 15.0, kMaxQ })
                            for (double g : { -kMaxGainDb, -12.0, 0.0, 12.0, kMaxGainDb })
                                for (int si = 0; si < (int) kSlopesDbPerOct.size(); ++si)
                                {
                                    const auto type = (FilterType) t;
                                    if (! typeUsesSlope (type) && si > 0)
                                        continue;
                                    const auto c = design ({ type, f, g, q, si }, sr);
                                    for (int s = 0; s < c.numSections; ++s)
                                    {
                                        ++checked;
                                        if (! c.sections[(size_t) s].isStable())
                                            ++unstable;
                                    }
                                }
                    }
            logMessage (juce::String (checked) + " sections checked");
            expectEquals (unstable, 0);
        }
    }
};

static FilterDesignTests filterDesignTests;
} // namespace
