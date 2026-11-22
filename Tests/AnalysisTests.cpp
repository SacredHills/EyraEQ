#include "Analysis/AudioAnalysis.h"
#include "DSP/FilterDesign.h"
#include "TestHelpers.h"

#include <juce_core/juce_core.h>

#include <numbers>

using namespace eyra::analysis;
using namespace eyra::dsp;

namespace
{
constexpr double twoPi = 2.0 * std::numbers::pi;

void applyFilter (std::vector<float>& x, const FilterSpec& spec, double sr, int start = 0, int end = -1)
{
    const auto c = design (spec, sr);
    std::array<std::array<double, 2>, kMaxSections> z {};
    if (end < 0)
        end = (int) x.size();
    for (int i = start; i < end; ++i)
    {
        double y = x[(size_t) i];
        for (int s = 0; s < c.numSections; ++s)
        {
            const auto& k = c.sections[(size_t) s];
            const double in = y;
            y = k.b0 * in + z[(size_t) s][0];
            z[(size_t) s][0] = k.b1 * in - k.a1 * y + z[(size_t) s][1];
            z[(size_t) s][1] = k.b2 * in - k.a2 * y;
        }
        x[(size_t) i] = (float) y;
    }
}

/** Band-limited pink noise (30 Hz - 18 kHz) at roughly -20 dBFS RMS: a "clean", realistic test source. */
std::vector<float> pinkNoise (double sr, double seconds, int seed = 1)
{
    juce::Random rng (seed);
    const int n = (int) (sr * seconds);
    std::vector<float> x ((size_t) n);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double w = rng.nextFloat() * 2.0 - 1.0;
        b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520; b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
        x[(size_t) i] = (float) ((b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362) * 0.03);
        b6 = w * 0.115926;
    }
    applyFilter (x, { FilterType::HighPass, 30.0, 0.0, 0.7071, 3 }, sr);
    applyFilter (x, { FilterType::LowPass, 18000.0, 0.0, 0.7071, 3 }, sr);
    return x;
}

void addSine (std::vector<float>& x, double hz, double amplitude, double sr)
{
    for (size_t i = 0; i < x.size(); ++i)
        x[i] += (float) (amplitude * std::sin (twoPi * hz * (double) i / sr));
}

AnalysisResult run (const std::vector<float>& x, double sr)
{
    return analyseAudio (x.data(), (int) x.size(), sr);
}

const Finding* find (const AnalysisResult& r, FindingType type, double hz = 0.0, double toleranceOctaves = 1.0 / 12.0)
{
    for (const auto& f : r.findings)
        if (f.type == type && (hz <= 0.0 || std::abs (std::log2 (f.frequency / hz)) <= toleranceOctaves))
            return &f;
    return nullptr;
}

juce::String summarise (const AnalysisResult& r)
{
    juce::String s = "status " + juce::String ((int) r.status) + ", " + juce::String ((int) r.findings.size()) + " findings";
    if (r.pitch.pitched)
        s << ", pitch " << juce::String (r.pitch.medianHz, 1) << " Hz";
    if (r.percussive)
        s << ", percussive";
    for (const auto& f : r.findings)
        s << "\n            - " << f.title << " [" << juce::String (f.frequency, 1) << " Hz, sev " << juce::String (f.severity, 2)
          << ", conf " << juce::String (f.confidence, 2) << "] " << f.detail;
    return s;
}

/** Drum-like hits every `period` seconds: a noise burst plus a natural body tone at 180 Hz, and
    optionally an inharmonic overtone that rings much longer than everything else. */
std::vector<float> hits (double sr, double seconds, double period, double ringHz, double ringLevel, int seed = 3)
{
    juce::Random rng (seed);
    std::vector<float> x ((size_t) (sr * seconds), 0.0f);
    const int step = (int) (period * sr);
    for (int start = 0; start + step <= (int) x.size(); start += step)
        for (int i = 0; i < step; ++i)
        {
            const double t = i / sr;
            double v = 0.5 * (rng.nextFloat() * 2.0 - 1.0) * std::exp (-t / 0.02);
            v += 0.4 * std::sin (twoPi * 180.0 * t) * std::exp (-t / 0.12);
            if (ringLevel > 0.0)
                v += ringLevel * std::sin (twoPi * ringHz * t) * std::exp (-t / 0.3);
            x[(size_t) (start + i)] = (float) v;
        }
    applyFilter (x, { FilterType::HighPass, 40.0, 0.0, 0.7071, 3 }, sr);
    return x;
}

class AnalysisTests final : public juce::UnitTest
{
public:
    AnalysisTests() : juce::UnitTest ("AudioAnalysis", "Analysis") {}

    void runTest() override
    {
        const double sr = 48000.0;

        beginTest ("Clean band-limited pink noise: no problems reported");
        {
            const auto r = run (pinkNoise (sr, 12.0), sr);
            logMessage (summarise (r));
            expect (r.status == AnalysisResult::Status::Ok);
            expectEquals ((int) r.findings.size(), 0);
            expect (! r.pitch.pitched);
            expect (! r.percussive);
        }

        beginTest ("Narrow resonance is found at the right frequency");
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            auto x = pinkNoise (rate, 12.0);
            applyFilter (x, { FilterType::Bell, 1700.0, 12.0, 10.0, 1 }, rate);
            const auto r = run (x, rate);
            logMessage (juce::String (rate) + ": " + summarise (r));
            const auto* f = find (r, FindingType::Resonance, 1700.0, 0.03);
            expect (f != nullptr, "resonance at 1.7 kHz @ " + juce::String (rate));
            if (f != nullptr)
            {
                expectWithinAbsoluteError (f->frequency, 1700.0, 1700.0 * 0.02);
                expectGreaterThan (f->q, 4.0);
                expectGreaterOrEqual (f->severity, 0.4);
                expectGreaterThan (f->persistence, 0.8);
            }
            expect (find (r, FindingType::Buildup) == nullptr, "no broad buildup for a narrow peak");
        }

        beginTest ("Broad low-mid buildup (mud) and harshness are found by their excess, not by range");
        {
            auto x = pinkNoise (sr, 12.0);
            applyFilter (x, { FilterType::Bell, 250.0, 6.0, 1.0, 1 }, sr);
            auto r = run (x, sr);
            logMessage (summarise (r));
            const auto* f = find (r, FindingType::Buildup, 250.0, 1.0 / 3.0);
            expect (f != nullptr, "buildup near 250 Hz");
            if (f != nullptr)
                expect (f->title.containsIgnoreCase ("mud"), f->title);
            expect (find (r, FindingType::Resonance) == nullptr);

            auto y = pinkNoise (sr, 12.0, 2);
            applyFilter (y, { FilterType::Bell, 3500.0, 6.0, 1.2, 1 }, sr);
            r = run (y, sr);
            logMessage (summarise (r));
            const auto* h = find (r, FindingType::Buildup, 3500.0, 1.0 / 3.0);
            expect (h != nullptr && h->title.containsIgnoreCase ("harsh"));
        }

        beginTest ("Sub-sonic rumble and DC offset");
        {
            auto x = pinkNoise (sr, 10.0);
            addSine (x, 12.0, 0.25, sr);
            auto r = run (x, sr);
            logMessage (summarise (r));
            expect (find (r, FindingType::Rumble) != nullptr);

            auto y = pinkNoise (sr, 10.0);
            for (auto& v : y)
                v += 0.05f;
            r = run (y, sr);
            expect (find (r, FindingType::DCOffset) != nullptr);
            expect (find (r, FindingType::Rumble) == nullptr);
        }

        beginTest ("Mains hum series");
        {
            auto x = pinkNoise (sr, 10.0);
            for (int m = 1; m <= 3; ++m)
                addSine (x, 60.0 * m, 0.02 / m, sr);
            const auto r = run (x, sr);
            logMessage (summarise (r));
            const auto* f = find (r, FindingType::Hum);
            expect (f != nullptr && f->title.contains ("60"));
            expect (find (r, FindingType::Resonance, 120.0) == nullptr, "hum harmonics are not repeated as resonances");
        }

        beginTest ("Pitched harmonic source: pitch found, harmonics not reported as resonances");
        {
            std::vector<float> x ((size_t) (sr * 10.0), 0.0f);
            for (int h = 1; h <= 25; ++h)
                addSine (x, 196.0 * h, 0.2 / h, sr);
            auto noise = pinkNoise (sr, 10.0, 5);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] += 0.03f * noise[i];
            const auto r = run (x, sr);
            logMessage (summarise (r));
            expect (r.pitch.pitched);
            expectWithinAbsoluteError (r.pitch.medianHz, 196.0, 196.0 * 0.01);
            expect (find (r, FindingType::Resonance) == nullptr);
            expect (find (r, FindingType::IntermittentResonance) == nullptr);
            expect (find (r, FindingType::Buildup) == nullptr, "a clean harmonic tone has no broad buildup");
        }

        beginTest ("Pitched source with a real boxy formant: still detected");
        {
            std::vector<float> x ((size_t) (sr * 10.0), 0.0f);
            for (int h = 1; h <= 25; ++h)
                addSine (x, 196.0 * h, 0.2 / h, sr);
            applyFilter (x, { FilterType::Bell, 600.0, 7.0, 1.4, 1 }, sr);
            auto noise = pinkNoise (sr, 10.0, 6);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] += 0.03f * noise[i];
            const auto r = run (x, sr);
            logMessage (summarise (r));
            const auto* f = find (r, FindingType::Buildup, 600.0, 1.0 / 3.0);
            expect (f != nullptr, "boxiness near 600 Hz on a harmonic source");
            if (f != nullptr)
                expect (f->title.containsIgnoreCase ("box"), f->title);
        }

        beginTest ("Tom-like hits: body around the fundamental is not called mud");
        {
            juce::Random rng (9);
            std::vector<float> x ((size_t) (sr * 10.0), 0.0f);
            const int step = (int) (0.5 * sr);
            for (int start = 0; start + step <= (int) x.size(); start += step)
            {
                double phase = 0.0;
                for (int i = 0; i < step; ++i)
                {
                    const double t = i / sr;
                    const double f = 150.0 + 12.0 * std::exp (-t / 0.08); // slight downward glide
                    phase += twoPi * f / sr;
                    const double body = 0.6 * std::sin (phase) * std::exp (-t / 0.22);
                    const double stick = 0.3 * (rng.nextFloat() * 2.0 - 1.0) * std::exp (-t / 0.008);
                    x[(size_t) (start + i)] = (float) (body + stick);
                }
            }
            const auto r = run (x, sr);
            logMessage (summarise (r));
            expect (r.percussive, "percussive");
            expect (find (r, FindingType::Buildup, 160.0, 0.5) == nullptr, "the tom's body is not reported as mud");
        }

        beginTest ("Ringing mode after hits is detected; plain hits show no ringing");
        {
            auto r = run (hits (sr, 10.0, 0.6, 1130.0, 0.06), sr);
            logMessage (summarise (r));
            expect (r.percussive);
            const auto* f = find (r, FindingType::Ringing, 1130.0, 0.05);
            expect (f != nullptr, "ringing overtone at 1.13 kHz");
            if (f != nullptr)
                expectGreaterThan (f->decayRatio, 1.5);

            expect (find (r, FindingType::Ringing, 180.0, 0.1) == nullptr, "the natural body tone is not called ringing");
            expect (find (r, FindingType::Buildup) == nullptr, "a ringing overtone is not also reported as a broad buildup");

            r = run (hits (sr, 10.0, 0.6, 0.0, 0.0), sr);
            logMessage (summarise (r));
            expect (find (r, FindingType::Ringing) == nullptr);
            expect (find (r, FindingType::Resonance) == nullptr);
        }

        beginTest ("Intermittent resonance");
        {
            auto x = pinkNoise (sr, 16.0);
            const int period = (int) (4.0 * sr), on = (int) (1.3 * sr);
            for (int start = 0; start < (int) x.size(); start += period)
                applyFilter (x, { FilterType::Bell, 2200.0, 14.0, 8.0, 1 }, sr, start, std::min ((int) x.size(), start + on));
            const auto r = run (x, sr);
            logMessage (summarise (r));
            const auto* f = find (r, FindingType::IntermittentResonance, 2200.0, 0.05);
            expect (f != nullptr, "intermittent resonance at 2.2 kHz");
            if (f != nullptr)
                expect (f->persistence > 0.15 && f->persistence < 0.6);
        }

        beginTest ("Too little audio and silence are reported, not analysed");
        {
            auto r = run (pinkNoise (sr, 1.0), sr);
            expect (r.status == AnalysisResult::Status::InsufficientAudio);
            expect (r.findings.empty());

            std::vector<float> silence ((size_t) sr * 5, 0.0f);
            r = run (silence, sr);
            expect (r.status == AnalysisResult::Status::Silent);

            // Short burst of signal inside a long silence: only the active part counts.
            auto mostlySilent = silence;
            auto burst = pinkNoise (sr, 1.0);
            std::copy (burst.begin(), burst.end(), mostlySilent.begin() + (int) sr);
            r = run (mostlySilent, sr);
            expect (r.status == AnalysisResult::Status::InsufficientAudio);
            expectWithinAbsoluteError (r.activeSeconds, 1.0, 0.1);
        }

        beginTest ("Confidence grows with the amount of audio");
        {
            auto a = pinkNoise (sr, 3.0);
            auto b = pinkNoise (sr, 14.0);
            expectLessThan (run (a, sr).confidence, run (b, sr).confidence);
        }

        beginTest ("Speed: 30 s of 48 kHz audio");
        {
            auto x = pinkNoise (sr, 30.0);
            applyFilter (x, { FilterType::Bell, 800.0, 9.0, 6.0, 1 }, sr);
            const auto start = juce::Time::getMillisecondCounterHiRes();
            const auto r = run (x, sr);
            const double ms = juce::Time::getMillisecondCounterHiRes() - start;
            logMessage ("analysis took " + juce::String (juce::roundToInt (ms)) + " ms; " + summarise (r));
            if (eyra::test::kTimingAssertions)
                expectLessThan (ms, 5000.0);
            expect (find (r, FindingType::Resonance, 800.0, 0.05) != nullptr);
        }
    }
};

static AnalysisTests analysisTests;
} // namespace

//==============================================================================
#include "PluginProcessor.h"

namespace
{
/** Feeds the processor in roughly real-time-sized bursts so the analysis worker keeps up. */
void feed (EyraEQAudioProcessor& p, const std::vector<float>& signal, int blockSize = 512)
{
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos + (size_t) blockSize <= signal.size(); pos += (size_t) blockSize)
    {
        for (int i = 0; i < blockSize; ++i)
            buf.setSample (0, i, signal[pos + (size_t) i]), buf.setSample (1, i, signal[pos + (size_t) i]);
        p.processBlock (buf, midi);
        if ((pos / (size_t) blockSize) % 40 == 0)
            juce::Thread::sleep (2);
    }
}

class AnalysisPipelineTests final : public juce::UnitTest
{
public:
    AnalysisPipelineTests() : juce::UnitTest ("Analysis pipeline", "Analysis") {}

    void runTest() override
    {
        using State = eyra::analysis::AnalysisController::State;
        const double sr = 48000.0;

        beginTest ("Listen during playback, stop automatically, report the resonance");
        {
            EyraEQAudioProcessor p;
            p.setPlayConfigDetails (2, 2, sr, 512);
            p.prepareToPlay (sr, 512);

            auto x = pinkNoise (sr, 17.0, 11);
            applyFilter (x, { FilterType::Bell, 2400.0, 12.0, 10.0, 1 }, sr);

            expect (p.analysis.getState() == State::Idle);
            p.analysis.startListening();
            for (int i = 0; i < 200 && p.analysis.getState() != State::Listening; ++i)
                juce::Thread::sleep (5);
            expect (p.analysis.getState() == State::Listening);

            feed (p, x);
            expect (p.analysis.waitUntilFinished (eyra::test::kBackgroundTimeoutMs), "analysis finishes");
            expect (p.analysis.getState() == State::Done);
            const auto r = p.analysis.getResult();
            expect (r != nullptr);
            if (r != nullptr)
            {
                logMessage (summarise (*r));
                expectWithinAbsoluteError (r->activeSeconds, eyra::analysis::AnalysisController::kTargetActiveSeconds, 1.0);
                expect (find (*r, FindingType::Resonance, 2400.0, 0.03) != nullptr, "resonance found through the plug-in");
            }
            expect (p.analysis.getResultVersion() > 0);
        }

        beginTest ("Stop early, cancel, sample-rate change and clear");
        {
            EyraEQAudioProcessor p;
            p.setPlayConfigDetails (2, 2, sr, 512);
            p.prepareToPlay (sr, 512);

            // Stop & analyse is refused until there is enough signal.
            p.analysis.startListening();
            for (int i = 0; i < 200 && p.analysis.getState() != State::Listening; ++i)
                juce::Thread::sleep (5);
            feed (p, pinkNoise (sr, 1.0));
            juce::Thread::sleep (100);
            expect (! p.analysis.canStopAndAnalyse());
            feed (p, pinkNoise (sr, 4.0, 4));
            juce::Thread::sleep (100);
            expect (p.analysis.canStopAndAnalyse());
            p.analysis.stopAndAnalyse();
            expect (p.analysis.waitUntilFinished (eyra::test::kBackgroundTimeoutMs));
            expect (p.analysis.getResult() != nullptr && p.analysis.getResult()->status == AnalysisResult::Status::Ok);

            // Cancel keeps the previous result.
            p.analysis.startListening();
            for (int i = 0; i < 200 && p.analysis.getState() != State::Listening; ++i)
                juce::Thread::sleep (5);
            p.analysis.cancel();
            for (int i = 0; i < 200 && p.analysis.getState() == State::Listening; ++i)
                juce::Thread::sleep (5);
            expect (p.analysis.getState() == State::Done);

            // A sample-rate change while listening cancels the capture.
            p.analysis.startListening();
            for (int i = 0; i < 200 && p.analysis.getState() != State::Listening; ++i)
                juce::Thread::sleep (5);
            p.prepareToPlay (96000.0, 512);
            for (int i = 0; i < 200 && p.analysis.getState() == State::Listening; ++i)
                juce::Thread::sleep (5);
            expect (p.analysis.getState() != State::Listening);

            p.analysis.clearResult();
            expect (p.analysis.getResult() == nullptr);
            expect (p.analysis.getState() == State::Idle);
        }

        beginTest ("Analyse an audio file (standalone feature)");
        {
            auto x = pinkNoise (sr, 6.0, 21);
            applyFilter (x, { FilterType::Bell, 1200.0, 12.0, 10.0, 1 }, sr);
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("eyra_analysis_test.wav");
            file.deleteFile();
            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
                auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (1).withBitsPerSample (24));
                expect (writer != nullptr);
                if (writer != nullptr)
                {
                    const float* channels[] = { x.data() };
                    writer->writeFromFloatArrays (channels, 1, (int) x.size());
                }
            }

            EyraEQAudioProcessor p;
            p.analysis.analyseFile (file);
            juce::Thread::sleep (20);
            expect (p.analysis.waitUntilFinished (eyra::test::kBackgroundTimeoutMs));
            const auto r = p.analysis.getResult();
            expect (r != nullptr && find (*r, FindingType::Resonance, 1200.0, 0.03) != nullptr, "resonance found in the file");
            expect (p.analysis.getSourceDescription().contains ("eyra_analysis_test.wav"));

            p.analysis.analyseFile (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("does_not_exist.wav"));
            juce::Thread::sleep (20);
            expect (p.analysis.waitUntilFinished (eyra::test::kBackgroundTimeoutMs));
            expect (p.analysis.getStatusMessage().contains ("Could not read"));
            file.deleteFile();
        }
    }
};

static AnalysisPipelineTests analysisPipelineTests;
} // namespace
