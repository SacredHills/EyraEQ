/*
    Loads the built EyraEQ.vst3 through JUCE's VST3 hosting layer (the same path a DAW takes) and
    exercises it: scanning, instantiation, bus layouts, parameters, processing, state, editor and
    repeated load / unload.

    Usage: EyraEQHostTest /path/to/EyraEQ.vst3
*/

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <cmath>
#include <iostream>
#include <numbers>

namespace
{
int failures = 0, passes = 0;

void check (bool condition, const juce::String& what)
{
    (condition ? passes : failures)++;
    std::cout << (condition ? "  [ok]   " : "  [FAIL] ") << what << "\n";
}

juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* param : p.getParameters())
        if (param->getName (128) == name)
            return param;
    return nullptr;
}

double measureGainDb (juce::AudioPluginInstance& p, double sr, int block, int channels, double hz)
{
    juce::AudioBuffer<float> buf (channels, block);
    juce::MidiBuffer midi;
    std::vector<float> out;
    int64_t n = 0;
    for (int pos = 0; pos < (int) sr; pos += block)
    {
        for (int i = 0; i < block; ++i)
        {
            const float v = 0.25f * (float) std::sin (2.0 * std::numbers::pi * hz * (double) n++ / sr);
            for (int c = 0; c < channels; ++c)
                buf.setSample (c, i, v);
        }
        p.processBlock (buf, midi);
        out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + block);
    }
    const int len = (int) (sr / 2.0);
    double sum = 0.0;
    for (size_t i = out.size() - (size_t) len; i < out.size(); ++i)
        sum += (double) out[i] * out[i];
    return 20.0 * std::log10 (std::sqrt (sum / len) / (0.25 / std::sqrt (2.0)));
}

std::unique_ptr<juce::AudioPluginInstance> load (juce::AudioPluginFormatManager& fm, const juce::PluginDescription& d, double sr, int block)
{
    juce::String error;
    auto instance = fm.createPluginInstance (d, sr, block, error);
    if (instance == nullptr)
        std::cout << "  load error: " << error << "\n";
    return instance;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc < 2)
    {
        std::cout << "usage: EyraEQHostTest <path to EyraEQ.vst3>\n";
        return 2;
    }

    const juce::File bundle (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    std::cout << "Hosting " << bundle.getFullPathName() << "\n";

    juce::AudioPluginFormatManager fm;
    fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
    auto* vst3 = fm.getFormat (0);

    // --- Bundle contents: a single self-contained plug-in, no extra libraries or model files ---
    {
        const auto contents = bundle.getChildFile ("Contents");
        bool extraLibrary = false;
        for (const auto& f : contents.findChildFiles (juce::File::findFiles, true, "*onnxruntime*"))
            extraLibrary = extraLibrary || f.existsAsFile();
        check (! extraLibrary, "bundle needs no ONNX Runtime library");
        check (! contents.getChildFile ("Resources").getChildFile ("Models").exists(), "bundle needs no model files");
    }

    // --- Scan ------------------------------------------------------------------------------
    juce::OwnedArray<juce::PluginDescription> found;
    vst3->findAllTypesForFile (found, bundle.getFullPathName());
    check (found.size() == 1, "scan finds exactly one plug-in");
    if (found.isEmpty())
        return 1;
    const auto desc = *found[0];
    check (desc.name == "EyraEQ", "name is EyraEQ (" + desc.name + ")");
    check (desc.manufacturerName == "SacredHills", "manufacturer is SacredHills");
    check (! desc.isInstrument, "registered as an effect");
    check (desc.category.containsIgnoreCase ("EQ"), "category contains EQ (" + desc.category + ")");

    // --- Instantiate, layouts --------------------------------------------------------------
    auto p = load (fm, desc, 48000.0, 512);
    check (p != nullptr, "instantiates");
    if (p == nullptr)
        return 1;

    // A plug-in must be inactive while its layout is queried or changed.
    p->releaseResources();

    juce::AudioProcessor::BusesLayout mono, stereo;
    mono.inputBuses.add (juce::AudioChannelSet::mono());
    mono.outputBuses.add (juce::AudioChannelSet::mono());
    stereo.inputBuses.add (juce::AudioChannelSet::stereo());
    stereo.outputBuses.add (juce::AudioChannelSet::stereo());
    check (p->checkBusesLayoutSupported (stereo), "stereo layout supported");
    check (p->checkBusesLayoutSupported (mono), "mono layout supported");
    check (p->getLatencySamples() == 0, "reports zero latency (no spectral band)");
    check (p->getParameters().size() >= 106, "exposes all parameters (" + juce::String (p->getParameters().size()) + ")");

    // --- Parameters drive the DSP ----------------------------------------------------------
    auto* used = findParam (*p, "Band 1 in use");
    auto* on = findParam (*p, "Band 1 on");
    auto* freq = findParam (*p, "Band 1 freq");
    auto* gain = findParam (*p, "Band 1 gain");
    check (on != nullptr && used != nullptr && freq != nullptr && gain != nullptr, "band parameters visible to host");
    if (on == nullptr || used == nullptr || freq == nullptr || gain == nullptr)
        return 1;

    for (int channels : { 2, 1 })
        for (double sr : { 44100.0, 96000.0 })
            for (int block : { 64, 512 })
            {
                p->releaseResources();
                p->setBusesLayout (channels == 2 ? stereo : mono);
                p->prepareToPlay (sr, block);

                used->setValue (1.0f);
                on->setValue (1.0f);
                freq->setValue (freq->getValueForText ("1000"));
                gain->setValue (gain->getValueForText ("-9"));
                const double g = measureGainDb (*p, sr, block, channels, 1000.0);
                check (std::abs (g + 9.0) < 0.1, juce::String (channels) + " ch, " + juce::String (sr) + " Hz, block " + juce::String (block)
                                                     + ": host-set -9 dB band measures " + juce::String (g, 2) + " dB");
            }

    check (freq->getText (freq->getValue(), 32).startsWith ("1"), "frequency text from plug-in: " + freq->getText (freq->getValue(), 32));

    // --- State -----------------------------------------------------------------------------
    juce::MemoryBlock state;
    p->getStateInformation (state);
    check (state.getSize() > 0, "state saved (" + juce::String ((int) state.getSize()) + " bytes)");

    auto q = load (fm, desc, 44100.0, 512);
    check (q != nullptr, "second instance");
    if (q != nullptr)
    {
        q->setStateInformation (state.getData(), (int) state.getSize());
        auto* qGain = findParam (*q, "Band 1 gain");
        auto* qUsed = findParam (*q, "Band 1 in use");
        check (qGain != nullptr && std::abs (qGain->getValue() - gain->getValue()) < 1.0e-5f, "state restores parameter values in a new instance");
        check (qUsed != nullptr && qUsed->getValue() > 0.5f, "state restores band enabled");
        q->releaseResources();
        q->setBusesLayout (stereo);
        q->prepareToPlay (44100.0, 512);
        const double g = measureGainDb (*q, 44100.0, 512, 2, 1000.0);
        check (std::abs (g + 9.0) < 0.1, "restored instance processes identically (" + juce::String (g, 2) + " dB)");
    }

    // --- Editor ----------------------------------------------------------------------------
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorAndMakeActive());
        check (editor != nullptr, "editor opens");
        if (editor != nullptr)
        {
            editor->setSize (1000, 600);
            check (editor->getWidth() == 1000, "editor resizes");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
            p->editorBeingDeleted (editor.get());
        }
    }

    // --- Bypass ----------------------------------------------------------------------------
    if (auto* bypass = p->getBypassParameter())
    {
        bypass->setValue (1.0f);
        p->releaseResources();
        p->setBusesLayout (stereo);
        p->prepareToPlay (48000.0, 512);
        const double g = measureGainDb (*p, 48000.0, 512, 2, 1000.0);
        check (std::abs (g) < 0.01, "host bypass parameter passes audio unchanged (" + juce::String (g, 3) + " dB)");
    }
    else
        check (false, "host bypass parameter exposed");

    p->releaseResources();
    p.reset();
    q.reset();

    // --- Repeated load / unload ------------------------------------------------------------
    bool allLoaded = true;
    for (int i = 0; i < 10; ++i)
    {
        auto inst = load (fm, desc, 48000.0, 256);
        allLoaded &= inst != nullptr;
        if (inst != nullptr)
        {
            inst->prepareToPlay (48000.0, 256);
            juce::AudioBuffer<float> buf (2, 256);
            buf.clear();
            juce::MidiBuffer midi;
            inst->processBlock (buf, midi);
            inst->releaseResources();
        }
    }
    check (allLoaded, "10 load / process / unload cycles");

    std::cout << "\nHost test: " << passes << " passed, " << failures << " failed\n";
    return failures == 0 ? 0 : 1;
}
