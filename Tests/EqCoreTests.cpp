#include "PluginProcessor.h"
#include "TestHelpers.h"

using namespace eyra;
using params::BandField;
using params::bandParamID;

namespace
{
double gainAt (const dsp::EqBandFilter& f, double hz, double sr) { return test::toDb (f.magnitude (hz, sr)); }

class EqCoreTests final : public juce::UnitTest
{
public:
    EqCoreTests() : juce::UnitTest ("EQ core", "Plugin") {}

    void runTest() override
    {
        const double sr = 48000.0;

        beginTest ("Bell: gain at the centre, unity far away");
        {
            dsp::EqBand b;
            b.type = (float) dsp::eqBell;
            b.freq = 1000.0f;
            b.q = 2.0f;
            dsp::EqBandFilter f;
            f.design (b, sr, 9.0f);
            expectWithinAbsoluteError (gainAt (f, 1000.0, sr), 9.0, 0.05);
            expectWithinAbsoluteError (gainAt (f, 30.0, sr), 0.0, 0.2);
            expectWithinAbsoluteError (gainAt (f, 15000.0, sr), 0.0, 0.3);
        }

        beginTest ("Shelves reach their gain");
        {
            dsp::EqBand lo, hi;
            lo.type = (float) dsp::eqLowShelf;
            lo.freq = 200.0f;
            hi.type = (float) dsp::eqHighShelf;
            hi.freq = 5000.0f;
            dsp::EqBandFilter fl, fh;
            fl.design (lo, sr, -6.0f);
            fh.design (hi, sr, 4.0f);
            expectWithinAbsoluteError (gainAt (fl, 20.0, sr), -6.0, 0.3);
            expectWithinAbsoluteError (gainAt (fh, 18000.0, sr), 4.0, 0.5);
        }

        beginTest ("Cuts: -3 dB at the corner and the chosen slope");
        for (int slope = 0; slope < 8; ++slope)
        {
            dsp::EqBand b;
            b.type = (float) dsp::eqLowCut;
            b.freq = 200.0f;
            b.q = 0.7071f;
            b.slope = (float) slope;
            dsp::EqBandFilter f;
            f.design (b, sr, 0.0f);
            expectWithinAbsoluteError (gainAt (f, 200.0, sr), -3.0, 0.3, "corner, slope " + juce::String (slope));
            const double perOct = gainAt (f, 100.0, sr) - gainAt (f, 50.0, sr);
            expect (std::abs (perOct - (double) dsp::eqSlopeDb[slope]) < 0.12 * dsp::eqSlopeDb[slope] + 1.5,
                    "slope " + juce::String (slope) + " measured " + juce::String (perOct, 1));
            expectWithinAbsoluteError (gainAt (f, 8000.0, sr), 0.0, 0.2);
        }

        beginTest ("Notch removes its centre");
        {
            dsp::EqBand b;
            b.type = (float) dsp::eqNotch;
            b.freq = 1000.0f;
            b.q = 8.0f;
            dsp::EqBandFilter f;
            f.design (b, sr, 0.0f);
            expect (gainAt (f, 1000.0, sr) < -40.0);
        }

        beginTest ("Latency follows spectral bands");
        {
            EyraEQAudioProcessor p;
            p.setPlayConfigDetails (2, 2, sr, 512);
            p.prepareToPlay (sr, 512);
            expectEquals (p.getEq().getLatencySamples(), 0);
            params::setParameterValue (p.apvts, bandParamID (0, BandField::Used), 1.0f);
            params::setParameterValue (p.apvts, bandParamID (0, BandField::On), 1.0f);
            params::setParameterValue (p.apvts, bandParamID (0, BandField::Dynamics), 2.0f);
            p.getEq().updateLatency();
            expectEquals (p.getEq().getLatencySamples(), dsp::EqSpectral::size);
            params::setParameterValue (p.apvts, bandParamID (0, BandField::Dynamics), 0.0f);
            p.getEq().updateLatency();
            expectEquals (p.getEq().getLatencySamples(), 0);
        }

        beginTest ("Dynamic band turns a loud tone down, leaves a quiet one alone");
        for (float level : { 0.5f, 0.001f })
        {
            EyraEQAudioProcessor p;
            p.setPlayConfigDetails (1, 1, sr, 480);
            auto set = [&] (BandField f, float v) { params::setParameterValue (p.apvts, bandParamID (0, f), v); };
            set (BandField::Used, 1.0f);
            set (BandField::On, 1.0f);
            set (BandField::Type, (float) dsp::eqBell);
            set (BandField::Frequency, 1000.0f);
            set (BandField::Q, 2.0f);
            set (BandField::Dynamics, 1.0f);
            set (BandField::Range, -12.0f);
            set (BandField::Threshold, -30.0f);
            set (BandField::Attack, 1.0f);
            p.prepareToPlay (sr, 480);

            juce::AudioBuffer<float> buf (1, 480);
            juce::MidiBuffer midi;
            int64_t phase = 0;
            std::vector<float> out;
            for (int blk = 0; blk < 200; ++blk)
            {
                test::fillSine (buf, 1000.0, sr, level, phase);
                p.processBlock (buf, midi);
                out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 480);
            }
            const int n = (int) out.size() / 4;
            const double g = test::toDb (test::rms (out.data() + 3 * n, n) / (level / std::sqrt (2.0)));
            if (level > 0.1f)
                expect (g < -3.0, "loud tone reduced: " + juce::String (g, 1) + " dB");
            else
                expectWithinAbsoluteError (g, 0.0, 0.3, "quiet tone untouched");
            p.releaseResources();
        }

        beginTest ("Spectral band turns down only what pokes out");
        {
            EyraEQAudioProcessor p;
            p.setPlayConfigDetails (1, 1, sr, 480);
            auto set = [&] (BandField f, float v) { params::setParameterValue (p.apvts, bandParamID (0, f), v); };
            set (BandField::Used, 1.0f);
            set (BandField::On, 1.0f);
            set (BandField::Type, (float) dsp::eqBell);
            set (BandField::Frequency, 2000.0f);
            set (BandField::Q, 0.5f);
            set (BandField::Dynamics, 2.0f);
            set (BandField::Range, -12.0f);
            set (BandField::Threshold, -40.0f);
            set (BandField::Attack, 1.0f);
            p.prepareToPlay (sr, 480);
            p.getEq().updateLatency();

            juce::AudioBuffer<float> buf (1, 480);
            juce::MidiBuffer midi;
            int64_t phase = 0;
            std::vector<float> out;
            for (int blk = 0; blk < 200; ++blk)
            {
                test::fillSine (buf, 2000.0, sr, 0.5f, phase);
                p.processBlock (buf, midi);
                out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 480);
            }
            const int n = (int) out.size() / 4;
            const double g = test::toDb (test::rms (out.data() + 3 * n, n) / (0.5 / std::sqrt (2.0)));
            expect (g < -3.0, "tone in the band reduced: " + juce::String (g, 1) + " dB");
            p.releaseResources();
        }
    }
};

static EqCoreTests eqCoreTests;
} // namespace
