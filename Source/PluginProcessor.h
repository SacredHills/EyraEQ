#pragma once

#include "Analysis/AnalysisController.h"
#include "Analysis/SpectrumAnalyzer.h"
#include "Correction/CorrectionGenerator.h"
#include "DSP/EQEngine.h"
#include "Parameters/Parameters.h"
#include "State/ABComparison.h"
#include "State/EditHistory.h"
#include "State/PresetManager.h"

#include <juce_audio_processors/juce_audio_processors.h>

/** Per-instance editor / analyser preferences, saved with the plug-in state. */
struct EyraUIState
{
    eyra::analysis::AnalyzerSettings analyzer;
    bool showPreSpectrum = true;
    bool showPostSpectrum = true;
    float displayRangeDb = 18.0f; // +/- range of the EQ curve scale: 6, 12, 18, 30
    bool analysisPanelOpen = true;
    bool showAnalysisOverlay = true;
    int editorWidth = 1240;
    int editorHeight = 720;
};

class EyraEQAudioProcessor final : public juce::AudioProcessor
{
public:
    EyraEQAudioProcessor();
    ~EyraEQAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return presets.getCurrentPresetName(); }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

    eyra::analysis::AnalyzerFifo preFifo, postFifo;

    /** Listen-then-analyse workflow (pre-EQ signal). */
    eyra::analysis::AnalysisController analysis;

    eyra::state::EditHistory history;
    eyra::state::ABComparison ab;
    eyra::state::PresetManager presets;

    //==============================================================================
    // Corrective EQ (message thread). Settings are saved with the session, not automatable.
    eyra::correction::Settings getCorrectionSettings() const;
    void setCorrectionSettings (const eyra::correction::Settings&);

    /** Suggestions for the latest analysis with the current settings (empty if there is none). */
    std::vector<eyra::correction::Suggestion> getSuggestions() const;

    /**
        Writes the bands into free EQ band slots as one undoable edit, replacing bands written by the
        previous call. Returns how many were placed (fewer if the EQ ran out of free bands).
    */
    int applyCorrections (const std::vector<eyra::dsp::BandParams>& bands);

    /** Removes the bands written by applyCorrections, as one undoable edit. */
    void resetCorrections();

    /** Band slots currently holding generated corrections. */
    std::vector<int> getCorrectionSlots() const;

    /** Band currently soloed for audition (-1 = none). Not saved, not automatable. */
    std::atomic<int> soloBand { -1 };

    EyraUIState getUIState() const;
    void setUIState (const EyraUIState&);

    /** Current sample rate as seen by the audio engine (for drawing exact responses). */
    double getEngineSampleRate() const noexcept { return engineSampleRate.load(); }

    /** For tests. */
    eyra::dsp::EQEngine& getEngine() noexcept { return engine; }

private:
    /** Sends the pre-EQ signal to both the spectrum display and the analysis capture. */
    struct TapFanout final : eyra::dsp::AudioTap
    {
        eyra::dsp::AudioTap* first = nullptr;
        eyra::dsp::AudioTap* second = nullptr;
        void pushSamples (const double* l, const double* r, int n) noexcept override
        {
            first->pushSamples (l, r, n);
            second->pushSamples (l, r, n);
        }
    } preFanout;

    eyra::params::ParameterReader reader;
    eyra::dsp::EQEngine engine;
    std::atomic<double> engineSampleRate { 48000.0 };

    mutable juce::CriticalSection uiStateLock;
    EyraUIState uiState;

    mutable juce::CriticalSection correctionLock;
    eyra::correction::Settings correctionSettings;
    std::vector<int> correctionSlots;
    void clearCorrectionSlots();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EyraEQAudioProcessor)
};
