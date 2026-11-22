#pragma once

#include "Analysis/AudioAnalysis.h"
#include "FeatureExtractor.h"
#include "InstrumentTypes.h"
#include "ModelManager.h"
#include "OnnxRuntime.h"

#include <memory>
#include <mutex>

namespace eyra::intelligence
{

/** Instrument categories as defined by Models/instrument_taxonomy.json. */
struct Taxonomy
{
    struct Category
    {
        juce::String id, name;
        std::vector<std::pair<int, double>> classes; // label index, weight
    };

    struct Decision
    {
        double minActiveSeconds = 3.0, presentScore = 0.12, confidentScore = 0.3, highConfidenceScore = 0.5;
        double dominanceRatio = 1.6, highDominanceRatio = 2.5, candidateRatio = 0.5;
        int maxCandidates = 3;
    } decision;

    struct Refinements
    {
        int distortionIndex = -1;
        double distortedGuitarScore = 0.2, cleanGuitarScore = 0.06;
        double maleVocalMaxMedianHz = 165.0, maleVocalMaxHighHz = 260.0;
        double femaleVocalMinMedianHz = 230.0, femaleVocalMinLowHz = 160.0;
        double rackFloorTomSplitHz = 110.0, synthBassMaxMedianHz = 140.0;
    } refinements;

    std::vector<Category> categories;

    /** Parses the JSON and resolves class names against the model's labels. */
    bool parse (const juce::var& json, const juce::StringArray& labels, juce::String& error);
};

/**
    Recognises the instrument in a recording with a local model (YAMNet via ONNX Runtime).

    One shared, lazily initialised instance per process (the model is loaded once, whatever the
    number of plug-in instances). classify() is thread-safe and must not be called from the audio thread.

    The decision does not force an answer:
      - fewer than minActiveSeconds of signal       -> NeedMoreAudio
      - best category evidence below presentScore   -> Unknown
      - best clearly ahead (confidentScore, dominanceRatio) -> Detected (Medium / High confidence)
      - otherwise                                   -> Ambiguous, with the plausible candidates
    Refinements that the model cannot make itself (male / female vocal, rack / floor tom, synth bass)
    use the measured pitch and are labelled as such.
*/
class InstrumentClassifier
{
public:
    static std::shared_ptr<InstrumentClassifier> getShared();

    /** Loads the runtime, model and taxonomy, and runs the self-test. Safe to call repeatedly. */
    bool ensureInitialised();

    bool isReady() const;
    juce::String getStatus() const;

    InstrumentDetection classify (const float* mono, int numSamples, double sampleRate, const analysis::PitchSummary& pitch) const;

    /** Pure decision step, exposed for tests. `evidence` is per category in taxonomy order. */
    static InstrumentDetection decide (const Taxonomy&, const std::vector<double>& evidence, double distortion,
                                       double activeSeconds, const analysis::PitchSummary& pitch);

    /** Runs patches through the model; exposed for the reference-vector tests. */
    bool runModel (const std::vector<float>& patches, int numPatches, std::vector<float>& scores, juce::String& error) const;

    const FeatureExtractor& getFeatureExtractor() const noexcept { return features; }
    const Taxonomy& getTaxonomy() const noexcept { return taxonomy; }
    const ModelInfo& getModelInfo() const noexcept { return modelInfo; }
    const juce::StringArray& getLabels() const noexcept { return labels; }

    /** For tests: a fresh, independent instance. */
    static std::unique_ptr<InstrumentClassifier> createForTesting() { return std::unique_ptr<InstrumentClassifier> (new InstrumentClassifier()); }

private:
    InstrumentClassifier() = default;
    bool selfTest (juce::String& error) const;

    mutable std::mutex mutex;
    bool initialised = false, ready = false;
    juce::String status { "Instrument recognition has not been loaded yet." };

    ModelInfo modelInfo;
    OnnxModel model;
    FeatureExtractor features;
    Taxonomy taxonomy;
    juce::StringArray labels;
    juce::String inputName, scoresName;
};

} // namespace eyra::intelligence
