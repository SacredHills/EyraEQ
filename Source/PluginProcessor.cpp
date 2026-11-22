#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace
{
    const juce::Identifier stateType { "EyraEQState" };
    const juce::Identifier uiType { "UIState" };
    const juce::Identifier versionProp { "version" };
    const juce::Identifier presetProp { "preset" };
    const juce::Identifier instrumentProp { "instrumentOverride" };

    juce::ValueTree uiStateToTree (const EyraUIState& s)
    {
        juce::ValueTree t (uiType);
        t.setProperty ("fftOrder", s.analyzer.fftOrder, nullptr);
        t.setProperty ("speed", s.analyzer.speed, nullptr);
        t.setProperty ("averaging", s.analyzer.averaging, nullptr);
        t.setProperty ("mode", (int) s.analyzer.mode, nullptr);
        t.setProperty ("decay", s.analyzer.decayDbPerSecond, nullptr);
        t.setProperty ("slope", s.analyzer.slopeDbPerOctave, nullptr);
        t.setProperty ("showPre", s.showPreSpectrum, nullptr);
        t.setProperty ("showPost", s.showPostSpectrum, nullptr);
        t.setProperty ("range", s.displayRangeDb, nullptr);
        t.setProperty ("analysisPanel", s.analysisPanelOpen, nullptr);
        t.setProperty ("analysisOverlay", s.showAnalysisOverlay, nullptr);
        t.setProperty ("width", s.editorWidth, nullptr);
        t.setProperty ("height", s.editorHeight, nullptr);
        return t;
    }

    EyraUIState uiStateFromTree (const juce::ValueTree& t)
    {
        EyraUIState s;
        if (! t.isValid())
            return s;
        s.analyzer.fftOrder = juce::jlimit (eyra::analysis::AnalyzerSettings::kMinOrder, eyra::analysis::AnalyzerSettings::kMaxOrder,
                                            (int) t.getProperty ("fftOrder", s.analyzer.fftOrder));
        s.analyzer.speed = juce::jlimit (0, 2, (int) t.getProperty ("speed", s.analyzer.speed));
        s.analyzer.averaging = juce::jlimit (0, 3, (int) t.getProperty ("averaging", s.analyzer.averaging));
        s.analyzer.mode = (eyra::analysis::AnalyzerSettings::Mode) juce::jlimit (0, 1, (int) t.getProperty ("mode", 0));
        s.analyzer.decayDbPerSecond = (float) t.getProperty ("decay", s.analyzer.decayDbPerSecond);
        s.analyzer.slopeDbPerOctave = (float) t.getProperty ("slope", s.analyzer.slopeDbPerOctave);
        s.showPreSpectrum = (bool) t.getProperty ("showPre", s.showPreSpectrum);
        s.showPostSpectrum = (bool) t.getProperty ("showPost", s.showPostSpectrum);
        s.displayRangeDb = (float) t.getProperty ("range", s.displayRangeDb);
        s.analysisPanelOpen = (bool) t.getProperty ("analysisPanel", s.analysisPanelOpen);
        s.showAnalysisOverlay = (bool) t.getProperty ("analysisOverlay", s.showAnalysisOverlay);
        s.editorWidth = (int) t.getProperty ("width", s.editorWidth);
        s.editorHeight = (int) t.getProperty ("height", s.editorHeight);
        return s;
    }
} // namespace

EyraEQAudioProcessor::EyraEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", eyra::params::createParameterLayout()),
      history (*this, { eyra::params::id::masterBypass }),
      ab (*this, history, { eyra::params::id::masterBypass }),
      presets (apvts, history),
      reader (apvts)
{
    preFanout.first = &preFifo;
    preFanout.second = &analysis.getTap();
    engine.setTaps (&preFanout, &postFifo);
}

void EyraEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engineSampleRate = sampleRate;
    preFifo.sampleRate = sampleRate;
    postFifo.sampleRate = sampleRate;
    analysis.prepare (sampleRate);

    // Push the current parameter values first so the bands start in their final state.
    for (int b = 0; b < eyra::params::kNumBands; ++b)
        engine.setBandParams (b, reader.readBand (b));
    engine.setInputGainDb (reader.inputGainDb());
    engine.setOutputGainDb (reader.outputGainDb());
    engine.setMasterBypass (reader.masterBypass());

    engine.prepare (sampleRate, samplesPerBlock, juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels()));
    setLatencySamples (0);
}

void EyraEQAudioProcessor::releaseResources()
{
    engine.reset();
}

bool EyraEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void EyraEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    for (int b = 0; b < eyra::params::kNumBands; ++b)
        engine.setBandParams (b, reader.readBand (b));

    engine.setInputGainDb (reader.inputGainDb());
    engine.setOutputGainDb (reader.outputGainDb());
    engine.setMasterBypass (reader.masterBypass());
    engine.setSoloBand (soloBand.load (std::memory_order_relaxed));

    engine.process (buffer);
}

juce::AudioProcessorEditor* EyraEQAudioProcessor::createEditor()
{
    return new EyraEQAudioProcessorEditor (*this);
}

eyra::intelligence::Instrument EyraEQAudioProcessor::getEffectiveInstrument() const
{
    const int manual = instrumentOverride.load();
    if (manual > 0)
        return (eyra::intelligence::Instrument) manual;
    if (const auto r = analysis.getResult())
        return r->instrument.instrument();
    return eyra::intelligence::Instrument::Unknown;
}

juce::AudioProcessorParameter* EyraEQAudioProcessor::getBypassParameter() const
{
    return apvts.getParameter (eyra::params::id::masterBypass);
}

EyraUIState EyraEQAudioProcessor::getUIState() const
{
    const juce::ScopedLock lock (uiStateLock);
    return uiState;
}

void EyraEQAudioProcessor::setUIState (const EyraUIState& s)
{
    const juce::ScopedLock lock (uiStateLock);
    uiState = s;
}

//==============================================================================
void EyraEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root (stateType);
    root.setProperty (versionProp, 1, nullptr);
    root.setProperty (presetProp, presets.getCurrentPresetName(), nullptr);
    root.setProperty (instrumentProp, instrumentOverride.load(), nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    root.appendChild (uiStateToTree (getUIState()), nullptr);
    root.appendChild (ab.toValueTree(), nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void EyraEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType (stateType))
        return;

    const auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params);

    setUIState (uiStateFromTree (root.getChildWithName (uiType)));
    ab.fromValueTree (root.getChildWithName ("ABComparison"));
    presets.setCurrentPresetName (root.getProperty (presetProp, "Default").toString());
    const int instrument = root.getProperty (instrumentProp, -1);
    instrumentOverride = (instrument > 0 && instrument < (int) eyra::intelligence::Instrument::NumInstruments) ? instrument : -1;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EyraEQAudioProcessor();
}
