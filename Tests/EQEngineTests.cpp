#include "DSP/EQEngine.h"
#include "TestHelpers.h"

#include <juce_core/juce_core.h>

using namespace eyra::dsp;
using namespace eyra::test;

namespace
{
BandParams bell (double f, double g, double q = 1.0)
{
    BandParams p;
    p.used = true;
    p.type = FilterType::Bell;
    p.frequency = f;
    p.gainDb = g;
    p.q = q;
    return p;
}

/** Runs a sine through the engine and returns the steady-state gain in dB per channel. */
std::array<double, 2> measureGain (EQEngine& engine, double sr, double freq, int numChannels, int blockSize = 512,
                                   std::array<float, 2> amplitudes = { 0.25f, 0.25f })
{
    const int total = (int) sr; // one second
    juce::AudioBuffer<float> block (numChannels, blockSize);
    std::vector<float> out[2];
    int64_t phase = 0;

    for (int pos = 0; pos < total; pos += blockSize)
    {
        fillSine (block, freq, sr, 1.0f, phase);
        for (int c = 0; c < numChannels; ++c)
            block.applyGain (c, 0, blockSize, amplitudes[(size_t) c]);
        engine.process (block);
        for (int c = 0; c < numChannels; ++c)
            out[c].insert (out[c].end(), block.getReadPointer (c), block.getReadPointer (c) + blockSize);
    }

    // Measure over the last half second: a whole number of periods for every test frequency.
    std::array<double, 2> result { 0.0, 0.0 };
    const int n = (int) (sr / 2.0);
    for (int c = 0; c < numChannels; ++c)
    {
        const double inRms = std::abs (amplitudes[(size_t) c]) / std::sqrt (2.0);
        result[(size_t) c] = toDb (rms (out[c].data() + out[c].size() - (size_t) n, n) / inRms);
    }
    return result;
}

/** Peak level (dBFS) of content above ~6 kHz in a signal: used to detect clicks on a low sine. */
double highFrequencyPeakDb (const std::vector<float>& signal, double sr)
{
    FilterSpec spec { FilterType::HighPass, 6000.0, 0.0, 0.7071, 7 }; // 96 dB/oct
    const auto c = design (spec, sr);
    std::array<std::array<double, 2>, kMaxSections> z {};
    double peak = 0.0;

    for (size_t i = 0; i < signal.size(); ++i)
    {
        double y = signal[i];
        for (int s = 0; s < c.numSections; ++s)
        {
            const auto& k = c.sections[(size_t) s];
            const double x = y;
            y = k.b0 * x + z[(size_t) s][0];
            z[(size_t) s][0] = k.b1 * x - k.a1 * y + z[(size_t) s][1];
            z[(size_t) s][1] = k.b2 * x - k.a2 * y;
        }
        if (i > 4800) // skip the filter's own start-up transient
            peak = std::max (peak, std::abs (y));
    }
    return toDb (peak);
}

class EQEngineTests final : public juce::UnitTest
{
public:
    EQEngineTests() : juce::UnitTest ("EQEngine", "DSP") {}

    void runTest() override
    {
        beginTest ("No bands: output is bit-identical to the input");
        {
            EQEngine e;
            e.prepare (48000.0, 256, 2);
            juce::AudioBuffer<float> buf (2, 256), ref (2, 256);
            juce::Random r (1);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 256; ++i)
                    buf.setSample (c, i, r.nextFloat() * 2.0f - 1.0f);
            ref.makeCopyOf (buf);
            e.process (buf);
            bool identical = true;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 256; ++i)
                    identical &= juce::exactlyEqual (buf.getSample (c, i), ref.getSample (c, i));
            expect (identical);
        }

        beginTest ("Measured gain matches the design (sine through the engine)");
        for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            EQEngine e;
            e.setBandParams (0, bell (1000.0, 6.0, 1.0));
            e.setBandParams (1, bell (8000.0, -9.0, 2.0));
            e.prepare (sr, 512, 2);

            const auto c0 = design ({ FilterType::Bell, 1000.0, 6.0, 1.0, 1 }, sr);
            const auto c1 = design ({ FilterType::Bell, 8000.0, -9.0, 2.0, 1 }, sr);

            for (double f : { 100.0, 1000.0, 3000.0, 8000.0 })
            {
                const double expected = c0.magnitudeDb (f, sr) + c1.magnitudeDb (f, sr);
                const auto g = measureGain (e, sr, f, 2);
                expectWithinAbsoluteError (g[0], expected, 0.05, "sr " + juce::String (sr) + " f " + juce::String (f));
                expectWithinAbsoluteError (g[1], expected, 0.05);
            }
        }

        beginTest ("Sample-rate changes re-derive coefficients");
        {
            EQEngine e;
            e.setBandParams (0, bell (1000.0, 6.0, 1.0));
            for (double sr : { 44100.0, 96000.0, 22050.0, 192000.0, 48000.0 })
            {
                e.prepare (sr, 512, 2);
                expectWithinAbsoluteError (measureGain (e, sr, 1000.0, 2)[0], 6.0, 0.05);
            }
        }

        beginTest ("Block size independence (including blocks larger than prepared)");
        {
            std::vector<float> reference;
            for (int blockSize : { 512, 1, 17, 333, 4096 })
            {
                EQEngine e;
                e.setBandParams (0, bell (300.0, 4.0, 0.8));
                BandParams hp;
                hp.used = true;
                hp.type = FilterType::HighPass;
                hp.frequency = 80.0;
                hp.slopeIndex = 3;
                e.setBandParams (1, hp);
                e.prepare (48000.0, 512, 1);

                juce::Random r (7);
                std::vector<float> out;
                juce::AudioBuffer<float> buf (1, blockSize);
                for (int pos = 0; pos < 24000; pos += blockSize)
                {
                    for (int i = 0; i < blockSize; ++i)
                        buf.setSample (0, i, r.nextFloat() - 0.5f);
                    e.process (buf);
                    out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + blockSize);
                }
                out.resize (24000);

                if (reference.empty())
                    reference = out;
                else
                {
                    float maxDiff = 0.0f;
                    for (size_t i = 0; i < out.size(); ++i)
                        maxDiff = std::max (maxDiff, std::abs (out[i] - reference[i]));
                    expectLessThan (maxDiff, 1.0e-6f, "block size " + juce::String (blockSize));
                }
            }
        }

        beginTest ("Stereo placements: left / right / mid / side");
        {
            const double sr = 48000.0;
            auto run = [&] (StereoPlacement place, std::array<float, 2> amps)
            {
                EQEngine e;
                auto p = bell (1000.0, 6.0, 1.0);
                p.placement = place;
                e.setBandParams (0, p);
                e.prepare (sr, 512, 2);
                return measureGain (e, sr, 1000.0, 2, 512, amps);
            };

            auto g = run (StereoPlacement::Left, { 0.25f, 0.25f });
            expectWithinAbsoluteError (g[0], 6.0, 0.05);
            expectWithinAbsoluteError (g[1], 0.0, 0.01);

            g = run (StereoPlacement::Right, { 0.25f, 0.25f });
            expectWithinAbsoluteError (g[0], 0.0, 0.01);
            expectWithinAbsoluteError (g[1], 6.0, 0.05);

            // Mono-compatible signal (L == R) has no side content.
            g = run (StereoPlacement::Side, { 0.25f, 0.25f });
            expectWithinAbsoluteError (g[0], 0.0, 0.001);
            expectWithinAbsoluteError (g[1], 0.0, 0.001);
            g = run (StereoPlacement::Mid, { 0.25f, 0.25f });
            expectWithinAbsoluteError (g[0], 6.0, 0.05);
            expectWithinAbsoluteError (g[1], 6.0, 0.05);

            // Pure side signal (L == -R) has no mid content.
            g = run (StereoPlacement::Mid, { 0.25f, -0.25f });
            expectWithinAbsoluteError (g[0], 0.0, 0.001);
            g = run (StereoPlacement::Side, { 0.25f, -0.25f });
            expectWithinAbsoluteError (g[0], 6.0, 0.05);
            expectWithinAbsoluteError (g[1], 6.0, 0.05);
        }

        beginTest ("Mono processing");
        {
            EQEngine e;
            e.setBandParams (0, bell (2000.0, -6.0, 1.0));
            e.prepare (44100.0, 256, 1);
            expectWithinAbsoluteError (measureGain (e, 44100.0, 2000.0, 1, 256)[0], -6.0, 0.05);
        }

        beginTest ("Input / output gain and master bypass");
        {
            EQEngine e;
            e.setBandParams (0, bell (1000.0, 6.0, 1.0));
            e.setInputGainDb (-3.0);
            e.setOutputGainDb (5.0);
            e.prepare (48000.0, 512, 2);
            expectWithinAbsoluteError (measureGain (e, 48000.0, 1000.0, 2)[0], 8.0, 0.05);

            e.setMasterBypass (true);
            measureGain (e, 48000.0, 1000.0, 2); // let the crossfade finish
            juce::AudioBuffer<float> buf (2, 512), ref (2, 512);
            int64_t phase = 0;
            fillSine (buf, 440.0, 48000.0, 0.5f, phase);
            ref.makeCopyOf (buf);
            e.process (buf);
            float maxDiff = 0.0f;
            for (int i = 0; i < 512; ++i)
                maxDiff = std::max (maxDiff, std::abs (buf.getSample (0, i) - ref.getSample (0, i)));
            expectEquals (maxDiff, 0.0f, "bypassed output must equal the input");
        }

        beginTest ("No clicks when parameters change abruptly");
        {
            const double sr = 48000.0;
            struct Change
            {
                const char* name;
                BandParams before, after;
            };

            auto base = bell (120.0, 0.0, 1.0);
            auto boosted = bell (120.0, 18.0, 1.0);
            auto moved = bell (1500.0, 12.0, 1.0);
            auto hp = base; hp.type = FilterType::HighPass; hp.frequency = 300.0;
            auto hpSteep = hp; hpSteep.slopeIndex = 7;
            auto off = boosted; off.used = false;
            auto bypassed = boosted; bypassed.bypassed = true;
            auto side = boosted; side.placement = StereoPlacement::Left;
            auto narrow = boosted; narrow.q = 30.0;

            const Change changes[] = {
                { "gain 0 -> +18 dB", base, boosted },
                { "frequency 120 Hz -> 1.5 kHz", boosted, moved },
                { "type bell -> high pass", boosted, hp },
                { "slope 12 -> 96 dB/oct", hp, hpSteep },
                { "band enable", off, boosted },
                { "band disable", boosted, off },
                { "band bypass", boosted, bypassed },
                { "placement stereo -> left", boosted, side },
                { "Q 1 -> 30", boosted, narrow },
            };

            for (const auto& ch : changes)
            {
                EQEngine e;
                e.setBandParams (0, ch.before);
                e.prepare (sr, 256, 2);

                std::vector<float> out;
                juce::AudioBuffer<float> buf (2, 256);
                int64_t phase = 0;
                for (int blk = 0; blk < 200; ++blk)
                {
                    if (blk == 100)
                        e.setBandParams (0, ch.after);
                    fillSine (buf, 80.0, sr, 0.1f, phase);
                    e.process (buf);
                    out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 256);
                }

                const double hf = highFrequencyPeakDb (out, sr);
                logMessage (juce::String (ch.name) + ": HF transient peak " + juce::String (hf, 1) + " dBFS");
                expectLessThan (hf, -80.0, ch.name);
            }

            // Master bypass toggle
            {
                EQEngine e;
                e.setBandParams (0, boosted);
                e.prepare (sr, 256, 2);
                std::vector<float> out;
                juce::AudioBuffer<float> buf (2, 256);
                int64_t phase = 0;
                for (int blk = 0; blk < 200; ++blk)
                {
                    if (blk == 100)
                        e.setMasterBypass (true);
                    fillSine (buf, 80.0, sr, 0.1f, phase);
                    e.process (buf);
                    out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 256);
                }
                expectLessThan (highFrequencyPeakDb (out, sr), -80.0, "master bypass");
            }
        }

        beginTest ("Solo auditions the band's region");
        {
            EQEngine e;
            e.setBandParams (0, bell (5000.0, 3.0, 2.0));
            e.setSoloBand (0);
            e.prepare (48000.0, 512, 2);
            expectLessThan (measureGain (e, 48000.0, 100.0, 2)[0], -30.0);
            expectWithinAbsoluteError (measureGain (e, 48000.0, 5000.0, 2)[0], 3.0, 0.1);

            e.setSoloBand (-1);
            expectWithinAbsoluteError (measureGain (e, 48000.0, 100.0, 2)[0], 0.0, 0.05);
        }

        beginTest ("CPU: 24 active bands, stereo, 48 kHz");
        {
            EQEngine e;
            for (int b = 0; b < EQEngine::kMaxBands; ++b)
            {
                auto p = bell (40.0 * std::pow (1.28, b), (b % 2 == 0 ? 4.0 : -4.0), 1.5);
                if (b == 0)
                {
                    p.type = FilterType::HighPass;
                    p.slopeIndex = 7; // 96 dB/oct: 8 sections
                }
                p.placement = (b % 3 == 0) ? StereoPlacement::Mid : StereoPlacement::Stereo;
                e.setBandParams (b, p);
            }
            e.prepare (48000.0, 512, 2);

            juce::AudioBuffer<float> buf (2, 512);
            int64_t phase = 0;
            const int blocks = 48000 * 10 / 512; // 10 s of audio
            const auto start = juce::Time::getHighResolutionTicks();
            for (int i = 0; i < blocks; ++i)
            {
                fillSine (buf, 440.0, 48000.0, 0.2f, phase);
                e.process (buf);
            }
            const double seconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);
            const double audioSeconds = blocks * 512.0 / 48000.0;
            const double realtimeFactor = audioSeconds / seconds;
            logMessage ("processed " + juce::String (audioSeconds, 1) + " s in " + juce::String (seconds * 1000.0, 1)
                        + " ms (" + juce::String (juce::roundToInt (realtimeFactor)) + "x real time, includes test signal generation)");
            if (kTimingAssertions)
                expectGreaterThan (realtimeFactor, 20.0);
        }

        beginTest ("Silence after signal: no NaN / Inf, decays to zero");
        {
            EQEngine e;
            for (int b = 0; b < EQEngine::kMaxBands; ++b)
            {
                auto p = bell (30.0 * std::pow (1.3, b), b % 2 == 0 ? 12.0 : -12.0, 8.0);
                e.setBandParams (b, p);
            }
            e.prepare (44100.0, 512, 2);
            juce::AudioBuffer<float> buf (2, 512);
            buf.clear();
            buf.setSample (0, 0, 1.0f);
            buf.setSample (1, 0, 1.0f);
            bool finite = true;
            float last = 0.0f;
            for (int blk = 0; blk < 2000; ++blk) // ~23 s
            {
                e.process (buf);
                for (int i = 0; i < 512; ++i)
                    finite &= std::isfinite (buf.getSample (0, i));
                last = buf.getMagnitude (0, 512);
                buf.clear();
            }
            expect (finite);
            expectLessThan (last, 1.0e-20f);
        }
    }
};

static EQEngineTests eqEngineTests;
} // namespace
