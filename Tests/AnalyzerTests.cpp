#include "Analysis/SpectrumAnalyzer.h"
#include "TestHelpers.h"

#include <juce_core/juce_core.h>

using namespace eyra::analysis;

namespace
{
int nearestPoint (double hz)
{
    int best = 0;
    for (int i = 0; i < SpectrumAnalyzer::kNumPoints; ++i)
        if (std::abs (std::log (SpectrumAnalyzer::getPointFrequency (i) / hz)) < std::abs (std::log (SpectrumAnalyzer::getPointFrequency (best) / hz)))
            best = i;
    return best;
}

void pushSine (AnalyzerFifo& fifo, double hz, double sr, double amplitude, int numSamples, int64_t& phase)
{
    std::vector<double> data ((size_t) numSamples);
    for (int i = 0; i < numSamples; ++i)
        data[(size_t) i] = amplitude * std::sin (2.0 * std::numbers::pi * hz * (double) (phase + i) / sr);
    phase += numSamples;
    fifo.pushSamples (data.data(), data.data(), numSamples);
}

class AnalyzerTests final : public juce::UnitTest
{
public:
    AnalyzerTests() : juce::UnitTest ("SpectrumAnalyzer", "Analysis") {}

    void runTest() override
    {
        beginTest ("Calibration: a sine reads its level in dBFS");
        for (double amplitudeDb : { 0.0, -20.0, -60.0 })
        {
            AnalyzerFifo fifo;
            fifo.sampleRate = 48000.0;
            SpectrumAnalyzer analyzer (fifo);
            AnalyzerSettings s;
            s.slopeDbPerOctave = 0.0f;
            s.averaging = 0;
            analyzer.setSettings (s);

            int64_t phase = 0;
            for (int i = 0; i < 20; ++i)
            {
                pushSine (fifo, 1000.0, 48000.0, std::pow (10.0, amplitudeDb / 20.0), 2400, phase);
                analyzer.update (0.05);
            }

            const float level = analyzer.getLevels()[(size_t) nearestPoint (1000.0)];
            expectWithinAbsoluteError ((double) level, amplitudeDb, 0.6, "sine at " + juce::String (amplitudeDb) + " dBFS");

            // Far away from the tone the level must be much lower.
            expectLessThan ((double) analyzer.getLevels()[(size_t) nearestPoint (100.0)], amplitudeDb - 60.0);
        }

        beginTest ("FIFO never blocks the writer and drops when full");
        {
            AnalyzerFifo fifo;
            std::vector<double> data ((size_t) AnalyzerFifo::kCapacity * 2, 0.1);
            fifo.pushSamples (data.data(), nullptr, (int) data.size());
            expectLessThan (fifo.getNumReady(), AnalyzerFifo::kCapacity);
        }

        beginTest ("Peak mode decays at the configured rate; freeze holds the display");
        {
            AnalyzerFifo fifo;
            fifo.sampleRate = 48000.0;
            SpectrumAnalyzer analyzer (fifo);
            AnalyzerSettings s;
            s.mode = AnalyzerSettings::Mode::Peak;
            s.decayDbPerSecond = 12.0f;
            s.slopeDbPerOctave = 0.0f;
            analyzer.setSettings (s);

            int64_t phase = 0;
            for (int i = 0; i < 10; ++i)
            {
                pushSine (fifo, 1000.0, 48000.0, 1.0, 4800, phase);
                analyzer.update (0.1);
            }
            const int p = nearestPoint (1000.0);
            const float peak = analyzer.getLevels()[(size_t) p];

            // One second of silence: the peak should fall by up to 12 dB (slightly less, because the
            // tone stays inside the FFT window for its first ~0.17 s).
            std::vector<double> silence (48000, 0.0);
            fifo.pushSamples (silence.data(), nullptr, 48000);
            analyzer.update (1.0);
            const float after = analyzer.getLevels()[(size_t) p];
            expectGreaterThan ((double) (peak - after), 9.0);
            expectLessOrEqual ((double) (peak - after), 12.01);

            s.freeze = true;
            analyzer.setSettings (s);
            pushSine (fifo, 1000.0, 48000.0, 1.0, 48000, phase);
            analyzer.update (1.0);
            expectEquals (analyzer.getLevels()[(size_t) p], after);
        }
    }
};

static AnalyzerTests analyzerTests;
} // namespace
