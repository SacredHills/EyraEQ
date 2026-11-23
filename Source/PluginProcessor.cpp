#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace
{
    const juce::Identifier stateType { "EyraEQState" };
    const juce::Identifier uiType { "UIState" };
    const juce::Identifier versionProp { "version" };
    const juce::Identifier presetProp { "preset" };
    const juce::Identifier correctionSlotsProp { "correctionBands" };

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
      presets (apvts, history)
{
    preFanout.first = &preFifo;
    preFanout.second = &analysis.getTap();
    bypassParam = apvts.getRawParameterValue (eyra::params::id::masterBypass);
    for (auto& r : dryRing)
        r.assign ((size_t) kDryRing, 0.0f);
}

void EyraEQAudioProcessor::prepareToPlay (double sampleRate, int)
{
    engineSampleRate = sampleRate;
    preFifo.sampleRate = sampleRate;
    postFifo.sampleRate = sampleRate;
    analysis.prepare (sampleRate);

    eq.prepare (sampleRate);
    for (auto& r : dryRing)
        std::fill (r.begin(), r.end(), 0.0f);
    dryWrite = 0;
    wetMix.reset (sampleRate, 0.02);
    wetMix.setCurrentAndTargetValue (bypassParam->load() >= 0.5f ? 0.0f : 1.0f);
}

void EyraEQAudioProcessor::releaseResources() {}

bool EyraEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void EyraEQAudioProcessor::pushTap (eyra::dsp::AudioTap& tap, const float* l, const float* r, int n) noexcept
{
    for (int off = 0; off < n; off += (int) tapL.size())
    {
        const int len = juce::jmin ((int) tapL.size(), n - off);
        for (int i = 0; i < len; ++i)
        {
            tapL[(size_t) i] = l[off + i];
            tapR[(size_t) i] = r != nullptr ? r[off + i] : l[off + i];
        }
        tap.pushSamples (tapL.data(), tapR.data(), len);
    }
}

void EyraEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int nch = juce::jmin (2, buffer.getNumChannels(), getTotalNumOutputChannels());
    for (int ch = getTotalNumInputChannels(); ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);
    if (n == 0 || nch == 0)
        return;

    float* L = buffer.getWritePointer (0);
    float* R = nch > 1 ? buffer.getWritePointer (1) : nullptr;

    pushTap (preFanout, L, R, n);

    // Keep the dry signal for the bypass crossfade, then run the EQ (always, so it is warm when bypass is released).
    const int latency = eq.getLatencySamples();
    for (int i = 0; i < n; ++i)
    {
        dryRing[0][(size_t) ((dryWrite + i) & (kDryRing - 1))] = L[i];
        if (R != nullptr)
            dryRing[1][(size_t) ((dryWrite + i) & (kDryRing - 1))] = R[i];
    }
    eq.process (L, R, n);

    wetMix.setTargetValue (bypassParam->load() >= 0.5f ? 0.0f : 1.0f);
    if (wetMix.isSmoothing() || wetMix.getTargetValue() < 0.5f)
    {
        for (int i = 0; i < n; ++i)
        {
            const float w = wetMix.getNextValue();
            const size_t at = (size_t) ((dryWrite + i - latency) & (kDryRing - 1));
            L[i] = L[i] * w + dryRing[0][at] * (1.0f - w);
            if (R != nullptr)
                R[i] = R[i] * w + dryRing[1][at] * (1.0f - w);
        }
    }
    dryWrite = (dryWrite + n) & (kDryRing - 1);

    pushTap (postFifo, L, R, n);
}

juce::AudioProcessorEditor* EyraEQAudioProcessor::createEditor()
{
    return new EyraEQAudioProcessorEditor (*this);
}

//==============================================================================
eyra::correction::Settings EyraEQAudioProcessor::getCorrectionSettings() const
{
    const juce::ScopedLock lock (correctionLock);
    return correctionSettings;
}

void EyraEQAudioProcessor::setCorrectionSettings (const eyra::correction::Settings& s)
{
    const juce::ScopedLock lock (correctionLock);
    correctionSettings = s;
}

std::vector<eyra::correction::Suggestion> EyraEQAudioProcessor::getSuggestions() const
{
    if (const auto r = analysis.getResult())
        return eyra::correction::generateCorrections (*r, getCorrectionSettings());
    return {};
}

std::vector<int> EyraEQAudioProcessor::getCorrectionSlots() const
{
    const juce::ScopedLock lock (correctionLock);
    return correctionSlots;
}

void EyraEQAudioProcessor::clearCorrectionSlots()
{
    for (int slot : getCorrectionSlots())
        if (slot >= 0 && slot < eyra::params::kNumBands && eyra::params::readBand (apvts, slot).used)
            eyra::params::clearBand (apvts, slot);
    const juce::ScopedLock lock (correctionLock);
    correctionSlots.clear();
}

int EyraEQAudioProcessor::applyCorrections (const std::vector<eyra::dsp::BandParams>& bands)
{
    history.beginEdit();
    const eyra::state::EditHistory::ScopedSuspend suspend (history);

    clearCorrectionSlots();

    std::vector<int> slots;
    size_t next = 0;
    for (int slot = 0; slot < eyra::params::kNumBands && next < bands.size(); ++slot)
    {
        if (eyra::params::readBand (apvts, slot).used)
            continue;
        auto b = bands[next++];
        b.bypassed = false;
        eyra::params::writeBand (apvts, slot, b);
        slots.push_back (slot);
    }

    const juce::ScopedLock lock (correctionLock);
    correctionSlots = slots;
    return (int) slots.size();
}

void EyraEQAudioProcessor::resetCorrections()
{
    history.beginEdit();
    const eyra::state::EditHistory::ScopedSuspend suspend (history);
    clearCorrectionSlots();
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
    {
        const juce::ScopedLock lock (correctionLock);
        juce::StringArray slots;
        for (int s : correctionSlots)
            slots.add (juce::String (s));
        root.setProperty (correctionSlotsProp, slots.joinIntoString (","), nullptr);
        root.appendChild (correctionSettings.toValueTree(), nullptr);
    }
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
    {
        const juce::ScopedLock lock (correctionLock);
        correctionSettings = eyra::correction::Settings::fromValueTree (root.getChildWithName ("Correction"));
        correctionSlots.clear();
        for (const auto& s : juce::StringArray::fromTokens (root.getProperty (correctionSlotsProp, "").toString(), ",", ""))
            if (s.isNotEmpty() && s.getIntValue() >= 0 && s.getIntValue() < eyra::params::kNumBands)
                correctionSlots.push_back (s.getIntValue());
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EyraEQAudioProcessor();
}
