#include "InstrumentClassifier.h"

#include <algorithm>
#include <numeric>

namespace eyra::intelligence
{

//==============================================================================
juce::String instrumentName (Instrument i)
{
    switch (i)
    {
        case Instrument::Unknown:            return "Unknown";
        case Instrument::KickDrum:           return "Kick drum";
        case Instrument::SnareDrum:          return "Snare drum";
        case Instrument::RackTom:            return "Rack tom";
        case Instrument::FloorTom:           return "Floor tom";
        case Instrument::Toms:               return "Toms";
        case Instrument::Cymbals:            return "Cymbals";
        case Instrument::Overheads:          return "Overheads / drum kit";
        case Instrument::Percussion:         return "Percussion";
        case Instrument::ElectricBass:       return "Electric bass";
        case Instrument::AcousticBass:       return "Acoustic bass";
        case Instrument::SynthBass:          return "Synth bass";
        case Instrument::ElectricGuitar:     return "Electric guitar";
        case Instrument::CleanGuitar:        return "Clean electric guitar";
        case Instrument::DistortedGuitar:    return "Distorted electric guitar";
        case Instrument::AcousticGuitar:     return "Acoustic guitar";
        case Instrument::MaleVocal:          return "Male vocal";
        case Instrument::FemaleVocal:        return "Female vocal";
        case Instrument::Vocal:              return "Vocal";
        case Instrument::Piano:              return "Piano";
        case Instrument::Organ:              return "Organ";
        case Instrument::Synthesizer:        return "Synthesizer";
        case Instrument::Strings:            return "Strings";
        case Instrument::Brass:              return "Brass";
        case Instrument::AcousticInstrument: return "Acoustic instrument";
        case Instrument::NumInstruments:     break;
    }
    return "Unknown";
}

Instrument instrumentForCategory (const juce::String& id)
{
    static const std::pair<const char*, Instrument> table[] = {
        { "kick", Instrument::KickDrum }, { "snare", Instrument::SnareDrum }, { "toms", Instrument::Toms },
        { "cymbals", Instrument::Cymbals }, { "overheads", Instrument::Overheads }, { "percussion", Instrument::Percussion },
        { "electric_bass", Instrument::ElectricBass }, { "acoustic_bass", Instrument::AcousticBass },
        { "electric_guitar", Instrument::ElectricGuitar }, { "acoustic_guitar", Instrument::AcousticGuitar },
        { "vocal", Instrument::Vocal }, { "piano", Instrument::Piano }, { "organ", Instrument::Organ },
        { "synth", Instrument::Synthesizer }, { "strings", Instrument::Strings }, { "brass", Instrument::Brass },
        { "acoustic_other", Instrument::AcousticInstrument },
    };
    for (const auto& [key, inst] : table)
        if (id == key)
            return inst;
    return Instrument::Unknown;
}

juce::String describe (const InstrumentDetection& d)
{
    using S = InstrumentDetection::Status;
    switch (d.status)
    {
        case S::NotRun:        return "Not analysed yet";
        case S::Unavailable:   return "Instrument recognition unavailable";
        case S::NeedMoreAudio: return "More audio needed to recognise the instrument";
        case S::Unknown:       return "Instrument not recognised";
        case S::Ambiguous:
        {
            juce::StringArray names;
            for (const auto& c : d.candidates)
                names.add (instrumentName (c.instrument));
            return "Possibly: " + names.joinIntoString (", ");
        }
        case S::Detected:      return instrumentName (d.instrument());
    }
    return {};
}

//==============================================================================
bool Taxonomy::parse (const juce::var& json, const juce::StringArray& labels, juce::String& error)
{
    if (! json.isObject())
    {
        error = "taxonomy is not valid JSON";
        return false;
    }

    const auto d = json.getProperty ("decision", {});
    decision.minActiveSeconds = d.getProperty ("minActiveSeconds", decision.minActiveSeconds);
    decision.presentScore = d.getProperty ("presentScore", decision.presentScore);
    decision.confidentScore = d.getProperty ("confidentScore", decision.confidentScore);
    decision.highConfidenceScore = d.getProperty ("highConfidenceScore", decision.highConfidenceScore);
    decision.dominanceRatio = d.getProperty ("dominanceRatio", decision.dominanceRatio);
    decision.highDominanceRatio = d.getProperty ("highDominanceRatio", decision.highDominanceRatio);
    decision.candidateRatio = d.getProperty ("candidateRatio", decision.candidateRatio);
    decision.maxCandidates = d.getProperty ("maxCandidates", decision.maxCandidates);

    const auto r = json.getProperty ("refinements", {});
    refinements.distortionIndex = labels.indexOf (r.getProperty ("distortionClass", "Distortion").toString());
    refinements.distortedGuitarScore = r.getProperty ("distortedGuitarScore", refinements.distortedGuitarScore);
    refinements.cleanGuitarScore = r.getProperty ("cleanGuitarScore", refinements.cleanGuitarScore);
    refinements.maleVocalMaxMedianHz = r.getProperty ("maleVocalMaxMedianHz", refinements.maleVocalMaxMedianHz);
    refinements.maleVocalMaxHighHz = r.getProperty ("maleVocalMaxHighHz", refinements.maleVocalMaxHighHz);
    refinements.femaleVocalMinMedianHz = r.getProperty ("femaleVocalMinMedianHz", refinements.femaleVocalMinMedianHz);
    refinements.femaleVocalMinLowHz = r.getProperty ("femaleVocalMinLowHz", refinements.femaleVocalMinLowHz);
    refinements.rackFloorTomSplitHz = r.getProperty ("rackFloorTomSplitHz", refinements.rackFloorTomSplitHz);
    refinements.synthBassMaxMedianHz = r.getProperty ("synthBassMaxMedianHz", refinements.synthBassMaxMedianHz);

    categories.clear();
    const auto cats = json.getProperty ("categories", {});
    if (! cats.isArray() || cats.size() < 2)
    {
        error = "taxonomy has no categories";
        return false;
    }

    for (const auto& c : *cats.getArray())
    {
        Category cat;
        cat.id = c.getProperty ("id", "").toString();
        cat.name = c.getProperty ("name", "").toString();
        if (auto* obj = c.getProperty ("classes", {}).getDynamicObject())
            for (const auto& prop : obj->getProperties())
            {
                const int index = labels.indexOf (prop.name.toString());
                if (index < 0)
                {
                    error = "taxonomy category '" + cat.id + "' refers to unknown label '" + prop.name.toString() + "'";
                    return false;
                }
                cat.classes.emplace_back (index, (double) prop.value);
            }
        if (cat.id.isEmpty() || cat.classes.empty() || instrumentForCategory (cat.id) == Instrument::Unknown)
        {
            error = "taxonomy category '" + cat.id + "' is invalid";
            return false;
        }
        categories.push_back (std::move (cat));
    }
    return true;
}

//==============================================================================
std::shared_ptr<InstrumentClassifier> InstrumentClassifier::getShared()
{
    static std::mutex m;
    static std::weak_ptr<InstrumentClassifier> weak;
    const std::lock_guard<std::mutex> lock (m);
    auto shared = weak.lock();
    if (shared == nullptr)
    {
        shared = std::shared_ptr<InstrumentClassifier> (new InstrumentClassifier());
        weak = shared;
    }
    return shared;
}

bool InstrumentClassifier::isReady() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return ready;
}

juce::String InstrumentClassifier::getStatus() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return status;
}

bool InstrumentClassifier::ensureInitialised()
{
    const std::lock_guard<std::mutex> lock (mutex);
    if (initialised)
        return ready;
    initialised = true;

    auto fail = [this] (const juce::String& why)
    {
        status = "Instrument recognition unavailable: " + why;
        ready = false;
        return false;
    };

    auto& rt = OnnxRuntime::getInstance();
    if (! rt.isAvailable())
        return fail (rt.getStatus());

    modelInfo = ModelManager::findBestModel ("yamnet");
    if (! modelInfo.valid)
        return fail (modelInfo.message);

    const auto& m = modelInfo.manifest;
    labels.clear();
    for (const auto& l : *m.getProperty ("labels", {}).getArray())
        labels.add (l.toString());
    inputName = m.getProperty ("input", {}).getProperty ("name", "").toString();
    scoresName = m.getProperty ("outputs", {}).getProperty ("scores", "").toString();

    juce::String error;
    if (! features.configure (m.getProperty ("frontend", {}), modelInfo.directory, error))
        return fail (error);

    const auto taxonomyFile = ModelManager::findTaxonomyFile();
    if (! taxonomyFile.existsAsFile())
        return fail ("instrument_taxonomy.json not found");
    if (! taxonomy.parse (juce::JSON::parse (taxonomyFile), labels, error))
        return fail (error);

    juce::MemoryBlock data;
    if (! modelInfo.directory.getChildFile (m.getProperty ("file", "").toString()).loadFileAsData (data))
        return fail ("could not read the model file");
    if (! model.load (data, error))
        return fail ("model could not be loaded (" + error + ")");

    if (! selfTest (error))
        return fail ("model self-test failed (" + error + ")");

    ready = true;
    status = "Instrument recognition ready (" + modelInfo.name + " " + modelInfo.version
           + (modelInfo.fromUserFolder ? ", user-installed" : "") + ", " + rt.getStatus() + ")";
    return true;
}

bool InstrumentClassifier::runModel (const std::vector<float>& patches, int numPatches, std::vector<float>& scores, juce::String& error) const
{
    const auto& c = features.getConfig();
    scores.clear();
    constexpr int batch = 64;

    for (int start = 0; start < numPatches; start += batch)
    {
        const int n = std::min (batch, numPatches - start);
        std::vector<float> out;
        int64_t columns = 0;
        if (! model.run (patches.data() + (size_t) start * (size_t) c.patchFrames * (size_t) c.melBands,
                         { n, c.patchFrames, c.melBands }, inputName, scoresName, out, columns, error))
            return false;
        if (columns != labels.size())
        {
            error = "model produced " + juce::String ((int) columns) + " scores, labels list has " + juce::String (labels.size());
            return false;
        }
        scores.insert (scores.end(), out.begin(), out.end());
    }
    return true;
}

bool InstrumentClassifier::selfTest (juce::String& error) const
{
    const auto ref = modelInfo.manifest.getProperty ("reference", {});
    if (! ref.isObject())
        return true; // models without reference vectors are trusted on checksum alone

    const int numPatches = ref.getProperty ("numPatches", 0);
    const double tolerance = ref.getProperty ("tolerance", 2.0e-3);
    juce::MemoryBlock patchData, scoreData;
    if (! modelInfo.directory.getChildFile (ref.getProperty ("patches", "").toString()).loadFileAsData (patchData)
        || ! modelInfo.directory.getChildFile (ref.getProperty ("scores", "").toString()).loadFileAsData (scoreData))
    {
        error = "reference vectors missing";
        return false;
    }

    const auto& c = features.getConfig();
    const size_t patchValues = (size_t) numPatches * (size_t) c.patchFrames * (size_t) c.melBands;
    if (patchData.getSize() != patchValues * 4 || scoreData.getSize() != (size_t) numPatches * (size_t) labels.size() * 4)
    {
        error = "reference vectors have the wrong size";
        return false;
    }

    auto toFloats = [] (const juce::MemoryBlock& b)
    {
        std::vector<float> v (b.getSize() / 4);
        for (size_t i = 0; i < v.size(); ++i)
        {
            const auto bits = juce::ByteOrder::littleEndianInt (static_cast<const char*> (b.getData()) + i * 4);
            std::memcpy (&v[i], &bits, 4);
        }
        return v;
    };

    const auto patches = toFloats (patchData);
    const auto expected = toFloats (scoreData);
    std::vector<float> scores;
    if (! runModel (patches, numPatches, scores, error))
        return false;

    double maxError = 0.0;
    for (size_t i = 0; i < expected.size(); ++i)
        maxError = std::max (maxError, (double) std::abs (scores[i] - expected[i]));
    if (maxError > tolerance)
    {
        error = "scores differ from the reference by " + juce::String (maxError, 4);
        return false;
    }
    return true;
}

//==============================================================================
InstrumentDetection InstrumentClassifier::classify (const float* mono, int numSamples, double sampleRate, const analysis::PitchSummary& pitch) const
{
    InstrumentDetection d;
    if (! isReady())
    {
        d.status = InstrumentDetection::Status::Unavailable;
        d.message = getStatus();
        return d;
    }
    d.model = modelInfo.name + " " + modelInfo.version;

    auto wave = features.padForPatches (features.resample (mono, numSamples, sampleRate));
    int numFrames = 0, numPatches = 0;
    const auto logMel = features.logMel (wave, numFrames);
    const auto patches = features.patches (logMel, numFrames, numPatches);
    if (numPatches == 0)
    {
        d.status = InstrumentDetection::Status::NeedMoreAudio;
        d.message = "Not enough audio.";
        return d;
    }

    // Gate patches by level (same rule as the validation script).
    std::vector<double> levels ((size_t) numPatches);
    for (int p = 0; p < numPatches; ++p)
    {
        const auto range = features.patchSampleRange (p).getIntersectionWith ({ 0, (int) wave.size() });
        double e = 0.0;
        for (int i = range.getStart(); i < range.getEnd(); ++i)
            e += (double) wave[(size_t) i] * wave[(size_t) i];
        levels[(size_t) p] = 10.0 * std::log10 (std::max (1.0e-20, e / std::max (1, range.getLength())));
    }
    const double gate = std::max (-60.0, *std::max_element (levels.begin(), levels.end()) - 40.0);
    std::vector<int> active;
    for (int p = 0; p < numPatches; ++p)
        if (levels[(size_t) p] > gate)
            active.push_back (p);

    d.activeSeconds = active.empty() ? 0.0 : 0.96 + 0.48 * (double) (active.size() - 1);
    if (active.empty())
    {
        d.status = InstrumentDetection::Status::NeedMoreAudio;
        d.message = "No signal.";
        return d;
    }

    // Run only the active patches.
    const auto& c = features.getConfig();
    const size_t patchSize = (size_t) c.patchFrames * (size_t) c.melBands;
    std::vector<float> activePatches (active.size() * patchSize);
    for (size_t i = 0; i < active.size(); ++i)
        std::copy_n (patches.begin() + (ptrdiff_t) ((size_t) active[i] * patchSize), patchSize, activePatches.begin() + (ptrdiff_t) (i * patchSize));

    std::vector<float> scores;
    juce::String error;
    if (! runModel (activePatches, (int) active.size(), scores, error))
    {
        d.status = InstrumentDetection::Status::Unavailable;
        d.message = "Inference failed: " + error;
        return d;
    }

    const int numLabels = labels.size();
    const int n = (int) active.size();

    // Category evidence: mean over patches of the strongest weighted class score.
    std::vector<double> evidence (taxonomy.categories.size(), 0.0);
    for (size_t ci = 0; ci < taxonomy.categories.size(); ++ci)
    {
        double sum = 0.0;
        for (int p = 0; p < n; ++p)
        {
            double best = 0.0;
            for (const auto& [index, weight] : taxonomy.categories[ci].classes)
                best = std::max (best, weight * scores[(size_t) (p * numLabels + index)]);
            sum += best;
        }
        evidence[ci] = sum / n;
    }

    std::vector<double> meanScores ((size_t) numLabels, 0.0);
    for (int p = 0; p < n; ++p)
        for (int k = 0; k < numLabels; ++k)
            meanScores[(size_t) k] += scores[(size_t) (p * numLabels + k)] / n;
    const double distortion = taxonomy.refinements.distortionIndex >= 0 ? meanScores[(size_t) taxonomy.refinements.distortionIndex] : 0.0;

    auto result = decide (taxonomy, evidence, distortion, d.activeSeconds, pitch);
    result.model = d.model;

    std::vector<int> order ((size_t) numLabels);
    std::iota (order.begin(), order.end(), 0);
    std::partial_sort (order.begin(), order.begin() + 5, order.end(), [&] (int a, int b) { return meanScores[(size_t) a] > meanScores[(size_t) b]; });
    for (int i = 0; i < 5; ++i)
        result.topLabels.emplace_back (labels[order[(size_t) i]], meanScores[(size_t) order[(size_t) i]]);
    return result;
}

InstrumentDetection InstrumentClassifier::decide (const Taxonomy& t, const std::vector<double>& evidence, double distortion,
                                                  double activeSeconds, const analysis::PitchSummary& pitch)
{
    using S = InstrumentDetection::Status;
    InstrumentDetection d;
    d.activeSeconds = activeSeconds;
    const auto& dc = t.decision;
    const auto& rf = t.refinements;

    if (activeSeconds < dc.minActiveSeconds)
    {
        d.status = S::NeedMoreAudio;
        d.message = "At least " + juce::String (dc.minActiveSeconds, 0) + " s of signal is needed to recognise the instrument.";
        return d;
    }

    std::vector<size_t> order (evidence.size());
    std::iota (order.begin(), order.end(), size_t { 0 });
    std::sort (order.begin(), order.end(), [&] (size_t a, size_t b) { return evidence[a] > evidence[b]; });
    const double best = evidence[order[0]];
    const double second = order.size() > 1 ? evidence[order[1]] : 0.0;

    // Refines a category into a specific instrument using evidence the model cannot provide itself.
    auto refine = [&] (size_t ci)
    {
        InstrumentCandidate c;
        c.categoryId = t.categories[ci].id;
        c.score = evidence[ci];
        c.instrument = instrumentForCategory (c.categoryId);
        const bool hasPitch = pitch.pitched && pitch.medianHz > 0.0;

        if (c.instrument == Instrument::Vocal && hasPitch)
        {
            if (pitch.medianHz <= rf.maleVocalMaxMedianHz && pitch.highHz <= rf.maleVocalMaxHighHz)
                c.instrument = Instrument::MaleVocal;
            else if (pitch.medianHz >= rf.femaleVocalMinMedianHz && pitch.lowHz >= rf.femaleVocalMinLowHz)
                c.instrument = Instrument::FemaleVocal;
            c.note = c.instrument == Instrument::Vocal ? "voice range not conclusive" : "from the voice's pitch range";
        }
        else if (c.instrument == Instrument::Vocal)
            c.note = "no steady pitch to judge the voice";
        else if (c.instrument == Instrument::Toms && hasPitch)
        {
            c.instrument = pitch.medianHz >= rf.rackFloorTomSplitHz ? Instrument::RackTom : Instrument::FloorTom;
            c.note = "from its pitch, " + juce::String (juce::roundToInt (pitch.medianHz)) + " Hz";
        }
        else if (c.instrument == Instrument::ElectricGuitar)
        {
            if (distortion >= rf.distortedGuitarScore)
                c.instrument = Instrument::DistortedGuitar, c.note = "distortion detected";
            else if (distortion <= rf.cleanGuitarScore)
                c.instrument = Instrument::CleanGuitar, c.note = "no distortion detected";
        }
        else if (c.instrument == Instrument::Synthesizer && hasPitch && pitch.medianHz <= rf.synthBassMaxMedianHz)
        {
            c.instrument = Instrument::SynthBass;
            c.note = "low register, " + juce::String (juce::roundToInt (pitch.medianHz)) + " Hz";
        }
        return c;
    };

    if (best < dc.presentScore)
    {
        d.status = S::Unknown;
        d.message = "No instrument category had enough evidence; choose one manually if needed.";
        return d;
    }

    if (best >= dc.confidentScore && best >= dc.dominanceRatio * std::max (second, 1.0e-9))
    {
        d.status = S::Detected;
        d.confidence = (best >= dc.highConfidenceScore && best >= dc.highDominanceRatio * std::max (second, 1.0e-9))
                         ? InstrumentDetection::Confidence::High : InstrumentDetection::Confidence::Medium;
        d.candidates.push_back (refine (order[0]));
        // Keep runners-up for transparency.
        for (size_t i = 1; i < order.size() && (int) d.candidates.size() < dc.maxCandidates; ++i)
            if (evidence[order[i]] >= dc.presentScore)
                d.candidates.push_back (refine (order[i]));
        return d;
    }

    d.status = S::Ambiguous;
    d.confidence = InstrumentDetection::Confidence::Low;
    for (size_t i = 0; i < order.size() && (int) d.candidates.size() < dc.maxCandidates; ++i)
        if (evidence[order[i]] >= dc.presentScore && evidence[order[i]] >= dc.candidateRatio * best)
            d.candidates.push_back (refine (order[i]));
    d.message = "Several instruments are plausible; the evidence does not single one out.";
    return d;
}

} // namespace eyra::intelligence
