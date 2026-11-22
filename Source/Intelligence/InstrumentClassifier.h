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

/** Instrument families and categories as defined by Models/instrument_taxonomy.json. */
struct Taxonomy
{
    struct Group
    {
        juce::String id, name;
        int family = -1;                             // categories: index into families
        std::vector<std::pair<int, double>> classes; // label index, weight
        /** Generic impact sounds (e.g. "Knock", "Thump") that count only when the analysis measured regular hits. */
        std::vector<std::pair<int, double>> percussiveClasses;
    };

    struct Decision
    {
        double minActiveSeconds = 3.0, presentScore = 0.12;
        double familyConfidentScore = 0.25, familyDominanceRatio = 1.6;
        double memberConfidentScore = 0.2, memberDominanceRatio = 2.0;
        double highConfidenceScore = 0.5, highDominanceRatio = 3.0;
        double candidateRatio = 0.5;
        int maxCandidates = 3;
    } decision;

    /** Drum piece from the sound (see estimateDrumPiece). */
    struct DrumRules
    {
        double cymbalMinHighShare = 0.4, cymbalMinCentroidHz = 2500.0, cymbalMaxLowShare = 0.05;
        double kitMinLowShare = 0.2, kitMinHighShare = 0.2;
        double lowMinShare = 0.5, kickMaxDecaySeconds = 0.22, decayMargin = 0.25;
        double snareMinFlatness = 0.1, flatnessMargin = 0.35;
        double modelPercussionScore = 0.3;
    } drumRules;

    struct Refinements
    {
        int distortionIndex = -1;
        double distortedGuitarScore = 0.2, cleanGuitarScore = 0.06;
        double maleVocalMaxMedianHz = 165.0, maleVocalMaxHighHz = 260.0;
        double femaleVocalMinMedianHz = 230.0, femaleVocalMinLowHz = 160.0;
        double rackFloorTomSplitHz = 110.0, synthBassMaxMedianHz = 140.0;
    } refinements;

    std::vector<Group> families, categories;

    int familyIndex (const juce::String& id) const;
    int categoryIndex (const juce::String& id) const;

    /** Parses the JSON and resolves class names against the model's labels. */
    bool parse (const juce::var& json, const juce::StringArray& labels, juce::String& error);
};

/** Measured sound character used by the decision (from the frequency analysis). */
struct SoundFeatures
{
    analysis::PitchSummary pitch;
    double lowShare = 0.0, lowMidShare = 0.0, midShare = 0.0, highShare = 0.0;
    double centroidHz = 0.0, flatness = 0.0, decaySeconds = 0.0;
    bool percussive = false;

    static SoundFeatures from (const analysis::AnalysisResult&);
};

/** Result of the drum-piece rules: a piece, or "not certain" between candidates. */
struct DrumEstimate
{
    Instrument piece = Instrument::Unknown;
    std::vector<Instrument> candidates;
    juce::String note;
};

DrumEstimate estimateDrumPiece (const Taxonomy::DrumRules&, const SoundFeatures&, double rackFloorSplitHz);

/**
    Recognises the instrument in a recording with a local model (YAMNet via ONNX Runtime).

    One shared, lazily initialised instance per process (the model is loaded once, whatever the
    number of plug-in instances). classify() is thread-safe and must not be called from the audio thread.

    Two stages, and no forced answers:
      1. Family (drums, bass, guitar, vocal, keys, strings & brass, other) from the model. Too little
         audio -> NeedMoreAudio; weak evidence -> Unknown; no clear family -> Ambiguous.
      2. Within the family: the model's specific classes if one clearly leads. For drums the piece
         (kick, snare, tom, cymbals, kit) comes from the measured sound, because the model cannot tell
         drum pieces apart reliably; borderline measurements give Ambiguous with both candidates.
    Refinements the model cannot make (male / female vocal, rack / floor tom, synth bass) use the
    measured pitch. Every decision carries a note saying which evidence it rests on.
*/
class InstrumentClassifier
{
public:
    static std::shared_ptr<InstrumentClassifier> getShared();

    /** Loads the runtime, model and taxonomy, and runs the self-test. Safe to call repeatedly. */
    bool ensureInitialised();

    bool isReady() const;
    juce::String getStatus() const;

    InstrumentDetection classify (const float* mono, int numSamples, double sampleRate, const analysis::AnalysisResult& analysis) const;

    /** Pure decision step, exposed for tests. Evidence vectors are in taxonomy order. */
    static InstrumentDetection decide (const Taxonomy&, const std::vector<double>& familyEvidence,
                                       const std::vector<double>& categoryEvidence, double distortion,
                                       double activeSeconds, const SoundFeatures& features);

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
