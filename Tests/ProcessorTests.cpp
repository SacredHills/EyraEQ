#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "TestHelpers.h"

#include <numbers>

#include <EyraBinaryData.h>

using namespace eyra;
using params::BandField;
using params::bandParamID;

namespace
{
float realValue (EyraEQAudioProcessor& p, const juce::String& id)
{
    auto* param = p.apvts.getParameter (id);
    return param->convertFrom0to1 (param->getValue());
}

void setReal (EyraEQAudioProcessor& p, const juce::String& id, float v)
{
    params::setParameterValue (p.apvts, id, v);
}

int countUsedBands (EyraEQAudioProcessor& p)
{
    int n = 0;
    for (int b = 0; b < params::kNumBands; ++b)
        n += realValue (p, bandParamID (b, BandField::Used)) >= 0.5f ? 1 : 0;
    return n;
}

void makeBand (EyraEQAudioProcessor& p, int b, int type, float freq, float gain, float q)
{
    setReal (p, bandParamID (b, BandField::Used), 1.0f);
    setReal (p, bandParamID (b, BandField::Type), (float) type);
    setReal (p, bandParamID (b, BandField::Frequency), freq);
    setReal (p, bandParamID (b, BandField::Gain), gain);
    setReal (p, bandParamID (b, BandField::Q), q);
}

class ProcessorTests final : public juce::UnitTest
{
public:
    ProcessorTests() : juce::UnitTest ("Processor", "Plugin") {}

    void runTest() override
    {
        beginTest ("Parameters: count, automation and host bypass");
        {
            EyraEQAudioProcessor p;
            expectEquals (p.getParameters().size(), params::kNumBands * 8 + 3);
            bool allAutomatable = true;
            for (auto* param : p.getParameters())
                allAutomatable &= param->isAutomatable();
            expect (allAutomatable);
            expect (p.getBypassParameter() != nullptr);
            expectEquals (p.getLatencySamples(), 0);

            // Value text shown to hosts
            auto* f = p.apvts.getParameter (bandParamID (0, BandField::Frequency));
            expectEquals (f->getText (f->convertTo0to1 (607.391f), 32), juce::String ("607 Hz"));
            expect (f->getText (f->convertTo0to1 (42.25f), 32).startsWith ("42."));
            expectEquals (f->getText (f->convertTo0to1 (2500.0f), 32), juce::String ("2.50 kHz"));
            expectWithinAbsoluteError (f->convertFrom0to1 (f->getValueForText ("3.2k")), 3200.0f, 0.5f);
        }

        beginTest ("Bus layouts: mono and stereo supported, mismatched rejected");
        {
            EyraEQAudioProcessor p;
            juce::AudioProcessor::BusesLayout mono, stereo, mixed;
            mono.inputBuses.add (juce::AudioChannelSet::mono());
            mono.outputBuses.add (juce::AudioChannelSet::mono());
            stereo.inputBuses.add (juce::AudioChannelSet::stereo());
            stereo.outputBuses.add (juce::AudioChannelSet::stereo());
            mixed.inputBuses.add (juce::AudioChannelSet::mono());
            mixed.outputBuses.add (juce::AudioChannelSet::stereo());
            expect (p.checkBusesLayoutSupported (mono));
            expect (p.checkBusesLayoutSupported (stereo));
            expect (! p.checkBusesLayoutSupported (mixed));
        }

        beginTest ("processBlock: parameter changes reach the audio path");
        for (int channels : { 1, 2 })
            for (double sr : { 44100.0, 96000.0 })
            {
                EyraEQAudioProcessor p;
                p.setPlayConfigDetails (channels, channels, sr, 480);
                makeBand (p, 3, 0, 1000.0f, -12.0f, 1.0f);
                p.prepareToPlay (sr, 480);

                juce::AudioBuffer<float> buf (channels, 480);
                juce::MidiBuffer midi;
                int64_t phase = 0;
                std::vector<float> out;
                bool finite = true;
                for (int blk = 0; blk < (int) sr / 480; ++blk)
                {
                    test::fillSine (buf, 1000.0, sr, 0.5f, phase);
                    p.processBlock (buf, midi);
                    for (int i = 0; i < 480; ++i)
                        finite &= std::isfinite (buf.getSample (0, i));
                    out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 480);
                }
                const int n = (int) out.size() / 2;
                const double gain = test::toDb (test::rms (out.data() + n, n) / (0.5 / std::sqrt (2.0)));
                expect (finite);
                expectWithinAbsoluteError (gain, -12.0, 0.05, juce::String (channels) + " ch @ " + juce::String (sr));
                p.releaseResources();
            }

        beginTest ("State save / restore round trip");
        {
            EyraEQAudioProcessor a;
            makeBand (a, 0, 3, 75.0f, 0.0f, 0.9f);
            setReal (a, bandParamID (0, BandField::Slope), 4.0f);
            makeBand (a, 5, 0, 2500.0f, 4.5f, 3.0f);
            setReal (a, bandParamID (5, BandField::Placement), 4.0f);
            setReal (a, bandParamID (5, BandField::Bypass), 1.0f);
            setReal (a, params::id::outputGain, -2.5f);
            auto ui = a.getUIState();
            ui.analyzer.fftOrder = 14;
            ui.displayRangeDb = 30.0f;
            a.setUIState (ui);
            a.presets.setCurrentPresetName ("My Setting");

            juce::MemoryBlock state;
            a.getStateInformation (state);

            EyraEQAudioProcessor b;
            b.setStateInformation (state.getData(), (int) state.getSize());

            bool same = true;
            for (int i = 0; i < a.getParameters().size(); ++i)
                same &= std::abs (a.getParameters()[i]->getValue() - b.getParameters()[i]->getValue()) < 1.0e-6f;
            expect (same, "all parameters restored");
            expectEquals (b.getUIState().analyzer.fftOrder, 14);
            expectEquals (b.getUIState().displayRangeDb, 30.0f);
            expectEquals (b.presets.getCurrentPresetName(), juce::String ("My Setting"));

            // Garbage must be ignored without crashing.
            const char junk[] = "not a state";
            b.setStateInformation (junk, (int) sizeof (junk));
            expectEquals (countUsedBands (b), 2);
        }

        beginTest ("Factory presets load and match their files");
        {
            EyraEQAudioProcessor p;
            const auto& entries = p.presets.getEntries();
            expectEquals ((int) entries.size(), EyraBinaryData::namedResourceListSize);

            for (int i = 0; i < (int) entries.size(); ++i)
            {
                if (! entries[(size_t) i].isFactory)
                    continue;
                expect (p.presets.loadPreset (i), entries[(size_t) i].name);

                int size = 0;
                const char* data = EyraBinaryData::getNamedResource (entries[(size_t) i].resourceName.toRawUTF8(), size);
                auto xml = juce::parseXML (juce::String::fromUTF8 (data, size));
                expect (xml != nullptr);
                if (xml == nullptr)
                    continue;
                expectEquals (countUsedBands (p), xml->getNumChildElements(), entries[(size_t) i].name);
                expectEquals (p.presets.getCurrentPresetName(), entries[(size_t) i].name);
            }

            const int vocal = [&] { for (int i = 0; i < (int) entries.size(); ++i) if (entries[(size_t) i].name == "Clean Vocal") return i; return -1; }();
            expect (vocal >= 0);
            p.presets.loadPreset (vocal);
            expectEquals ((int) realValue (p, bandParamID (0, BandField::Type)), 3); // high pass
            expectWithinAbsoluteError (realValue (p, bandParamID (0, BandField::Frequency)), 80.0f, 0.05f);
            expectEquals ((int) realValue (p, bandParamID (0, BandField::Slope)), 2); // 18 dB/oct
        }

        beginTest ("User preset save, reload and delete");
        {
            EyraEQAudioProcessor p;
            makeBand (p, 2, 1, 120.0f, -4.0f, 0.7f);
            const juce::String presetName = "__eyra_unit_test_preset__";
            expect (p.presets.saveUserPreset (presetName));

            p.presets.resetToDefault();
            expectEquals (countUsedBands (p), 0);

            int index = -1;
            for (int i = 0; i < (int) p.presets.getEntries().size(); ++i)
                if (p.presets.getEntries()[(size_t) i].name == presetName)
                    index = i;
            expect (index >= 0);
            expect (p.presets.loadPreset (index));
            expectEquals (countUsedBands (p), 1);
            expectWithinAbsoluteError (realValue (p, bandParamID (2, BandField::Gain)), -4.0f, 0.001f);
            expect (p.presets.deleteUserPreset (index));
        }

        beginTest ("Copy / paste format round trip");
        {
            EyraEQAudioProcessor p;
            makeBand (p, 0, 0, 440.0f, 3.0f, 2.0f);
            makeBand (p, 7, 7, 900.0f, -5.0f, 1.0f);
            const auto before = state::captureParameters (p);
            const auto xml = p.presets.createPresetXml ("clip")->toString();
            p.presets.resetToDefault();
            expect (p.presets.applyPresetXml (*juce::parseXML (xml)));
            expect (state::snapshotsEqual (before, state::captureParameters (p)));
        }

        beginTest ("Undo / redo of UI edits");
        {
            EyraEQAudioProcessor p;
            auto* gain = p.apvts.getParameter (bandParamID (0, BandField::Gain));
            const auto initial = state::captureParameters (p);

            // A UI edit is a gesture on the message thread.
            gain->beginChangeGesture();
            gain->setValueNotifyingHost (gain->convertTo0to1 (6.0f));
            gain->endChangeGesture();
            const auto edited = state::captureParameters (p);

            expect (p.history.canUndo());
            p.history.undo();
            expect (state::snapshotsEqual (initial, state::captureParameters (p)));
            expect (p.history.canRedo());
            p.history.redo();
            expect (state::snapshotsEqual (edited, state::captureParameters (p)));

            // Master bypass is not part of the undo history.
            setReal (p, params::id::masterBypass, 1.0f);
            p.history.undo();
            expectEquals (realValue (p, params::id::masterBypass), 1.0f);
            expect (state::snapshotsEqual (initial, [&] { auto s = state::captureParameters (p); s.back() = initial.back(); return s; }()));
        }

        beginTest ("A / B comparison");
        {
            EyraEQAudioProcessor p;
            makeBand (p, 0, 0, 1000.0f, 6.0f, 1.0f);
            const auto a = state::captureParameters (p);
            p.ab.toggle(); // B starts as a copy of A
            expectEquals (p.ab.getActiveSlot(), 1);
            expect (state::snapshotsEqual (a, state::captureParameters (p)));
            setReal (p, bandParamID (0, BandField::Gain), -6.0f);
            const auto b = state::captureParameters (p);
            p.ab.toggle();
            expect (state::snapshotsEqual (a, state::captureParameters (p)));
            p.ab.toggle();
            expect (state::snapshotsEqual (b, state::captureParameters (p)));

            // The inactive slot survives a state round trip.
            juce::MemoryBlock mb;
            p.getStateInformation (mb);
            EyraEQAudioProcessor q;
            q.setStateInformation (mb.getData(), (int) mb.getSize());
            expectEquals (q.ab.getActiveSlot(), 1);
            q.ab.toggle();
            expect (state::snapshotsEqual (a, state::captureParameters (q)));
        }

        beginTest ("Editor: creates, resizes and renders");
        {
            EyraEQAudioProcessor p;
            p.setPlayConfigDetails (2, 2, 48000.0, 512);
            p.presets.loadPreset (1);
            makeBand (p, 6, 0, 2400.0f, 4.0f, 2.5f);
            p.prepareToPlay (48000.0, 512);

            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorAndMakeActive());
            expect (editor != nullptr);

            // Feed noise with a hum and a resonant tone, and run a real analysis on it, so the
            // analyser, the analysis panel and the markers all have something to draw.
            juce::Random r (3);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            float lp = 0.0f;
            int64_t n = 0;
            auto* ed = dynamic_cast<EyraEQAudioProcessorEditor*> (editor.get());
            p.analysis.startListening();
            for (int i = 0; i < 200 && p.analysis.getState() != analysis::AnalysisController::State::Listening; ++i)
                juce::Thread::sleep (5);
            for (int blk = 0; blk < 800; ++blk)
            {
                for (int i = 0; i < 512; ++i, ++n)
                {
                    lp = 0.97f * lp + 0.03f * (r.nextFloat() * 2.0f - 1.0f);
                    const double t = (double) n / 48000.0;
                    const float v = 2.0f * lp + 0.02f * (r.nextFloat() * 2.0f - 1.0f)
                                  + 0.01f * (float) std::sin (2.0 * std::numbers::pi * 60.0 * t)
                                  + 0.005f * (float) std::sin (2.0 * std::numbers::pi * 120.0 * t)
                                  + 0.006f * (float) std::sin (2.0 * std::numbers::pi * 3150.0 * t);
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                p.processBlock (buf, midi);
                if (blk % 4 == 0)
                    ed->getSpectrumDisplay().tick (0.04);
                if (blk % 40 == 0)
                    juce::Thread::sleep (2);
            }
            p.analysis.stopAndAnalyse();
            expect (p.analysis.waitUntilFinished (test::kBackgroundTimeoutMs));
            ed->refreshNow();
            expect (ed->getContext().analysis != nullptr, "analysis result reaches the editor");
            if (ed->getContext().analysis != nullptr && ! ed->getContext().analysis->findings.empty())
                ed->getContext().selectedFinding = 0;

            ed->getContext().selection.selectOnly (6);
            ed->refreshNow();

            for (auto [w, h] : { std::pair { 860, 520 }, std::pair { 1240, 720 }, std::pair { 1760, 1060 } })
            {
                editor->setSize (w, h);
                const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
                expectEquals (image.getWidth(), w);

                const auto dir = juce::SystemStats::getEnvironmentVariable ("EYRA_SCREENSHOT_DIR", {});
                if (dir.isNotEmpty())
                {
                    juce::File file (juce::File (dir).getChildFile ("editor_" + juce::String (w) + "x" + juce::String (h) + ".png"));
                    file.deleteFile();
                    juce::FileOutputStream os (file);
                    juce::PNGImageFormat().writeImageToStream (image, os);
                }
            }

            // Mapping round-trip on the display.
            auto& d = ed->getSpectrumDisplay();
            expectWithinRelativeError (d.frequencyForX (d.xForFrequency (1234.0)), 1234.0, 1.0e-4);
            expectWithinAbsoluteError (d.gainForY (d.yForGain (-7.5)), -7.5, 1.0e-4);

            p.editorBeingDeleted (editor.get());
            editor.reset();
        }
    }

    void expectWithinRelativeError (double actual, double expected, double rel)
    {
        expectWithinAbsoluteError (actual, expected, std::abs (expected) * rel);
    }
};

static ProcessorTests processorTests;
} // namespace
