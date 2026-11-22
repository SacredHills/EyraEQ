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
        case Instrument::Drums:              return "Drums";
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
int Taxonomy::familyIndex (const juce::String& id) const
{
    for (size_t i = 0; i < families.size(); ++i)
        if (families[i].id == id)
            return (int) i;
    return -1;
}

int Taxonomy::categoryIndex (const juce::String& id) const
{
    for (size_t i = 0; i < categories.size(); ++i)
        if (categories[i].id == id)
            return (int) i;
    return -1;
}

bool Taxonomy::parse (const juce::var& json, const juce::StringArray& labels, juce::String& error)
{
    if (! json.isObject())
    {
        error = "taxonomy is not valid JSON";
        return false;
    }

    auto read = [] (const juce::var& obj, const char* key, double& value) { value = obj.getProperty (key, value); };

    const auto d = json.getProperty ("decision", {});
    read (d, "minActiveSeconds", decision.minActiveSeconds);
    read (d, "presentScore", decision.presentScore);
    read (d, "familyConfidentScore", decision.familyConfidentScore);
    read (d, "familyDominanceRatio", decision.familyDominanceRatio);
    read (d, "memberConfidentScore", decision.memberConfidentScore);
    read (d, "memberDominanceRatio", decision.memberDominanceRatio);
    read (d, "highConfidenceScore", decision.highConfidenceScore);
    read (d, "highDominanceRatio", decision.highDominanceRatio);
    read (d, "candidateRatio", decision.candidateRatio);
    decision.maxCandidates = d.getProperty ("maxCandidates", decision.maxCandidates);

    const auto dr = json.getProperty ("drumRules", {});
    read (dr, "cymbalMinHighShare", drumRules.cymbalMinHighShare);
    read (dr, "cymbalMinCentroidHz", drumRules.cymbalMinCentroidHz);
    read (dr, "cymbalMaxLowShare", drumRules.cymbalMaxLowShare);
    read (dr, "kitMinLowShare", drumRules.kitMinLowShare);
    read (dr, "kitMinHighShare", drumRules.kitMinHighShare);
    read (dr, "lowMinShare", drumRules.lowMinShare);
    read (dr, "kickMaxDecaySeconds", drumRules.kickMaxDecaySeconds);
    read (dr, "decayMargin", drumRules.decayMargin);
    read (dr, "snareMinFlatness", drumRules.snareMinFlatness);
    read (dr, "flatnessMargin", drumRules.flatnessMargin);
    read (dr, "modelPercussionScore", drumRules.modelPercussionScore);

    const auto r = json.getProperty ("refinements", {});
    refinements.distortionIndex = labels.indexOf (r.getProperty ("distortionClass", "Distortion").toString());
    read (r, "distortedGuitarScore", refinements.distortedGuitarScore);
    read (r, "cleanGuitarScore", refinements.cleanGuitarScore);
    read (r, "maleVocalMaxMedianHz", refinements.maleVocalMaxMedianHz);
    read (r, "maleVocalMaxHighHz", refinements.maleVocalMaxHighHz);
    read (r, "femaleVocalMinMedianHz", refinements.femaleVocalMinMedianHz);
    read (r, "femaleVocalMinLowHz", refinements.femaleVocalMinLowHz);
    read (r, "rackFloorTomSplitHz", refinements.rackFloorTomSplitHz);
    read (r, "synthBassMaxMedianHz", refinements.synthBassMaxMedianHz);

    auto parseGroups = [&] (const juce::var& list, std::vector<Group>& out, bool isCategory)
    {
        out.clear();
        if (! list.isArray() || list.size() < 2)
        {
            error = isCategory ? "taxonomy has no categories" : "taxonomy has no families";
            return false;
        }
        for (const auto& c : *list.getArray())
        {
            Group g;
            g.id = c.getProperty ("id", "").toString();
            g.name = c.getProperty ("name", "").toString();
            for (auto [key, target] : { std::pair { "classes", &g.classes }, std::pair { "percussiveClasses", &g.percussiveClasses } })
                if (auto* obj = c.getProperty (key, {}).getDynamicObject())
                    for (const auto& prop : obj->getProperties())
                    {
                        const int index = labels.indexOf (prop.name.toString());
                        if (index < 0)
                        {
                            error = "taxonomy group '" + g.id + "' refers to unknown label '" + prop.name.toString() + "'";
                            return false;
                        }
                        target->emplace_back (index, (double) prop.value);
                    }
            if (isCategory)
            {
                g.family = familyIndex (c.getProperty ("family", "").toString());
                if (g.family < 0 || instrumentForCategory (g.id) == Instrument::Unknown)
                {
                    error = "taxonomy category '" + g.id + "' is invalid (unknown id or family)";
                    return false;
                }
            }
            if (g.id.isEmpty() || g.classes.empty())
            {
                error = "taxonomy group '" + g.id + "' is invalid";
                return false;
            }
            out.push_back (std::move (g));
        }
        return true;
    };

    return parseGroups (json.getProperty ("families", {}), families, false)
        && parseGroups (json.getProperty ("categories", {}), categories, true);
}

SoundFeatures SoundFeatures::from (const analysis::AnalysisResult& r)
{
    SoundFeatures f;
    f.pitch = r.pitch;
    f.lowShare = r.lowShare;
    f.lowMidShare = r.lowMidShare;
    f.midShare = r.midShare;
    f.highShare = r.highShare;
    f.centroidHz = r.spectralCentroidHz;
    f.flatness = r.spectralFlatness;
    f.decaySeconds = r.medianDecaySeconds;
    f.percussive = r.percussive;
    return f;
}

DrumEstimate estimateDrumPiece (const Taxonomy::DrumRules& r, const SoundFeatures& f, double split)
{
    DrumEstimate e;
    auto pct = [] (double share) { return juce::String (juce::roundToInt (100.0 * share)) + "%"; };
    auto near = [] (double x, double threshold, double margin) { return std::abs (x - threshold) <= margin * threshold; };

    if (f.lowShare + f.lowMidShare + f.midShare + f.highShare <= 0.0)
        return e; // no measurements

    if (f.highShare >= r.cymbalMinHighShare || (f.centroidHz >= r.cymbalMinCentroidHz && f.lowShare < r.cymbalMaxLowShare))
    {
        e.piece = Instrument::Cymbals;
        e.note = pct (f.highShare) + " of the energy above 5 kHz";
        return e;
    }

    if (f.lowShare >= r.kitMinLowShare && f.highShare >= r.kitMinHighShare)
    {
        e.piece = Instrument::Overheads;
        e.note = "strong low and high drums together (" + pct (f.lowShare) + " below 150 Hz, " + pct (f.highShare) + " above 5 kHz)";
        return e;
    }

    if (f.lowShare >= r.lowMinShare)
    {
        const juce::String where = pct (f.lowShare) + " of the energy below 150 Hz";
        if (f.decaySeconds <= 0.0 || near (f.decaySeconds, r.kickMaxDecaySeconds, r.decayMargin))
        {
            e.candidates = { Instrument::KickDrum, Instrument::FloorTom };
            e.note = where + "; decay " + (f.decaySeconds > 0.0 ? juce::String (juce::roundToInt (f.decaySeconds * 1000.0)) + " ms is borderline" : juce::String ("not measurable"));
            return e;
        }
        const bool kick = f.decaySeconds < r.kickMaxDecaySeconds;
        e.piece = kick ? Instrument::KickDrum : Instrument::FloorTom;
        e.note = where + ", " + (kick ? "short" : "long") + " decay (" + juce::String (juce::roundToInt (f.decaySeconds * 1000.0)) + " ms)";
        return e;
    }

    const juce::String noise = "noisiness " + juce::String (f.flatness, 2);
    auto tom = [&]
    {
        const bool hasPitch = f.pitch.pitched && f.pitch.medianHz > 0.0;
        return hasPitch && f.pitch.medianHz < split ? Instrument::FloorTom : Instrument::RackTom;
    };
    if (near (f.flatness, r.snareMinFlatness, r.flatnessMargin))
    {
        e.candidates = { Instrument::SnareDrum, tom() };
        e.note = noise + " is borderline between snare and tom";
        return e;
    }
    if (f.flatness >= r.snareMinFlatness)
    {
        e.piece = Instrument::SnareDrum;
        e.note = "noisy midrange (snare wires), " + noise;
    }
    else
    {
        e.piece = tom();
        e.note = "tonal midrange body, " + noise + (f.pitch.pitched ? ", pitch " + juce::String (juce::roundToInt (f.pitch.medianHz)) + " Hz" : juce::String());
    }
    return e;
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
InstrumentDetection InstrumentClassifier::classify (const float* mono, int numSamples, double sampleRate, const analysis::AnalysisResult& analysis) const
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

    // Evidence: mean over patches of the strongest weighted class score of each group.
    auto evidenceFor = [&] (const std::vector<Taxonomy::Group>& groups)
    {
        std::vector<double> e (groups.size(), 0.0);
        for (size_t gi = 0; gi < groups.size(); ++gi)
        {
            double sum = 0.0;
            for (int p = 0; p < n; ++p)
            {
                double best = 0.0;
                for (const auto& [index, weight] : groups[gi].classes)
                    best = std::max (best, weight * scores[(size_t) (p * numLabels + index)]);
                if (analysis.percussive)
                    for (const auto& [index, weight] : groups[gi].percussiveClasses)
                        best = std::max (best, weight * scores[(size_t) (p * numLabels + index)]);
                sum += best;
            }
            e[gi] = sum / n;
        }
        return e;
    };
    const auto familyEvidence = evidenceFor (taxonomy.families);
    const auto categoryEvidence = evidenceFor (taxonomy.categories);

    std::vector<double> meanScores ((size_t) numLabels, 0.0);
    for (int p = 0; p < n; ++p)
        for (int k = 0; k < numLabels; ++k)
            meanScores[(size_t) k] += scores[(size_t) (p * numLabels + k)] / n;
    const double distortion = taxonomy.refinements.distortionIndex >= 0 ? meanScores[(size_t) taxonomy.refinements.distortionIndex] : 0.0;

    auto result = decide (taxonomy, familyEvidence, categoryEvidence, distortion, d.activeSeconds, SoundFeatures::from (analysis));
    result.model = d.model;

    std::vector<int> order ((size_t) numLabels);
    std::iota (order.begin(), order.end(), 0);
    std::partial_sort (order.begin(), order.begin() + 5, order.end(), [&] (int a, int b) { return meanScores[(size_t) a] > meanScores[(size_t) b]; });
    for (int i = 0; i < 5; ++i)
        result.topLabels.emplace_back (labels[order[(size_t) i]], meanScores[(size_t) order[(size_t) i]]);
    return result;
}

InstrumentDetection InstrumentClassifier::decide (const Taxonomy& t, const std::vector<double>& familyEvidence,
                                                  const std::vector<double>& categoryEvidence, double distortion,
                                                  double activeSeconds, const SoundFeatures& f)
{
    using S = InstrumentDetection::Status;
    using C = InstrumentDetection::Confidence;
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

    auto ranked = [] (const std::vector<double>& v, const std::vector<int>& subset)
    {
        std::vector<int> order (subset);
        std::sort (order.begin(), order.end(), [&] (int a, int b) { return v[(size_t) a] > v[(size_t) b]; });
        return order;
    };
    auto all = [] (size_t n) { std::vector<int> v (n); std::iota (v.begin(), v.end(), 0); return v; };

    // Refines a category into a specific instrument with evidence the model cannot provide itself.
    auto refine = [&] (int ci)
    {
        InstrumentCandidate c;
        c.categoryId = t.categories[(size_t) ci].id;
        c.score = categoryEvidence[(size_t) ci];
        c.instrument = instrumentForCategory (c.categoryId);
        const bool hasPitch = f.pitch.pitched && f.pitch.medianHz > 0.0;

        if (c.instrument == Instrument::Vocal && hasPitch)
        {
            if (f.pitch.medianHz <= rf.maleVocalMaxMedianHz && f.pitch.highHz <= rf.maleVocalMaxHighHz)
                c.instrument = Instrument::MaleVocal;
            else if (f.pitch.medianHz >= rf.femaleVocalMinMedianHz && f.pitch.lowHz >= rf.femaleVocalMinLowHz)
                c.instrument = Instrument::FemaleVocal;
            c.note = c.instrument == Instrument::Vocal ? "voice range not conclusive" : "from the voice's pitch range";
        }
        else if (c.instrument == Instrument::Vocal)
            c.note = "no steady pitch to judge the voice";
        else if (c.instrument == Instrument::Toms && hasPitch)
        {
            c.instrument = f.pitch.medianHz >= rf.rackFloorTomSplitHz ? Instrument::RackTom : Instrument::FloorTom;
            c.note = "from its pitch, " + juce::String (juce::roundToInt (f.pitch.medianHz)) + " Hz";
        }
        else if (c.instrument == Instrument::ElectricGuitar)
        {
            if (distortion >= rf.distortedGuitarScore)
                c.instrument = Instrument::DistortedGuitar, c.note = "distortion detected";
            else if (distortion <= rf.cleanGuitarScore)
                c.instrument = Instrument::CleanGuitar, c.note = "no distortion detected";
        }
        else if (c.instrument == Instrument::Synthesizer && hasPitch && f.pitch.medianHz <= rf.synthBassMaxMedianHz)
        {
            c.instrument = Instrument::SynthBass;
            c.note = "low register, " + juce::String (juce::roundToInt (f.pitch.medianHz)) + " Hz";
        }
        return c;
    };

    auto membersOf = [&] (int family)
    {
        std::vector<int> m;
        for (size_t i = 0; i < t.categories.size(); ++i)
            if (t.categories[i].family == family)
                m.push_back ((int) i);
        return ranked (categoryEvidence, m);
    };

    // --- Stage 1: family ---------------------------------------------------------------
    const auto families = ranked (familyEvidence, all (t.families.size()));
    const int bestFamily = families[0];
    const double fb = familyEvidence[(size_t) bestFamily];
    const double fs = families.size() > 1 ? familyEvidence[(size_t) families[1]] : 0.0;

    if (fb < dc.presentScore)
    {
        // Short synthetic drum hits often sound like nothing in particular to the model. If the analysis
        // measured regular hits whose sound points clearly to one drum, offer it as a possibility only.
        if (f.percussive)
        {
            const auto est = estimateDrumPiece (t.drumRules, f, rf.rackFloorTomSplitHz);
            std::vector<Instrument> pieces = est.candidates;
            if (est.piece != Instrument::Unknown)
                pieces = { est.piece };
            if (! pieces.empty())
            {
                d.status = S::Ambiguous;
                for (auto inst : pieces)
                {
                    InstrumentCandidate c;
                    c.instrument = inst;
                    c.categoryId = "drums";
                    c.score = fb;
                    c.note = "from the measurements only: " + est.note;
                    d.candidates.push_back (c);
                }
                d.message = "The model did not recognise the sound; regular hits were measured and their sound suggests the instrument shown.";
                return d;
            }
        }
        d.status = S::Unknown;
        d.message = "No instrument family had enough evidence; choose the instrument manually if needed.";
        return d;
    }

    const int drums = t.familyIndex ("drums");

    // The most likely instrument within a family (used for candidates when the family is unclear).
    auto representative = [&] (int family)
    {
        if (family == drums)
        {
            const auto est = estimateDrumPiece (t.drumRules, f, rf.rackFloorTomSplitHz);
            InstrumentCandidate c;
            c.instrument = est.piece != Instrument::Unknown ? est.piece : Instrument::Drums;
            c.categoryId = "drums";
            c.score = familyEvidence[(size_t) family];
            c.note = est.note;
            return c;
        }
        auto c = refine (membersOf (family)[0]);
        c.score = familyEvidence[(size_t) family];
        return c;
    };

    if (! (fb >= dc.familyConfidentScore && fb >= dc.familyDominanceRatio * std::max (fs, 1.0e-9)))
    {
        d.status = S::Ambiguous;
        for (int fam : families)
            if ((int) d.candidates.size() < dc.maxCandidates && familyEvidence[(size_t) fam] >= dc.presentScore
                && familyEvidence[(size_t) fam] >= dc.candidateRatio * fb)
                d.candidates.push_back (representative (fam));
        d.message = "The instrument family is not clear: " + t.families[(size_t) bestFamily].name + " is only slightly ahead.";
        return d;
    }

    const bool strongFamily = fb >= dc.highConfidenceScore && fb >= dc.highDominanceRatio * std::max (fs, 1.0e-9);
    const auto members = membersOf (bestFamily);
    const double mb = categoryEvidence[(size_t) members[0]];
    const double ms = members.size() > 1 ? categoryEvidence[(size_t) members[1]] : 0.0;
    const bool memberLeads = members.size() == 1
                          || (mb >= dc.memberConfidentScore && mb >= dc.memberDominanceRatio * std::max (ms, 1.0e-9));

    // --- Stage 2a: drums: the piece comes from the sound -------------------------------
    if (bestFamily == drums)
    {
        // Hand percussion and mallets are better recognised by the model than by the drum rules.
        const int perc = t.categoryIndex ("percussion");
        double kitPieces = 0.0;
        for (const char* id : { "kick", "snare", "toms" })
            if (const int i = t.categoryIndex (id); i >= 0)
                kitPieces = std::max (kitPieces, categoryEvidence[(size_t) i]);
        if (perc >= 0 && categoryEvidence[(size_t) perc] >= t.drumRules.modelPercussionScore
            && categoryEvidence[(size_t) perc] >= 2.0 * std::max (kitPieces, 1.0e-9))
        {
            d.status = S::Detected;
            d.confidence = strongFamily ? C::High : C::Medium;
            auto c = refine (perc);
            c.note = "the model hears hand percussion or mallets";
            d.candidates.push_back (c);
            return d;
        }

        const auto est = estimateDrumPiece (t.drumRules, f, rf.rackFloorTomSplitHz);
        if (est.piece != Instrument::Unknown)
        {
            InstrumentCandidate c;
            c.instrument = est.piece;
            c.categoryId = "drums";
            c.score = fb;
            c.note = est.note;
            d.status = S::Detected;
            // The model agreeing on the piece (e.g. cymbals) raises the confidence.
            const bool modelAgrees = memberLeads && instrumentForCategory (t.categories[(size_t) members[0]].id) == est.piece;
            d.confidence = modelAgrees && strongFamily ? C::High : C::Medium;
            d.candidates.push_back (c);
            return d;
        }

        d.status = S::Ambiguous;
        for (auto inst : est.candidates)
        {
            InstrumentCandidate c;
            c.instrument = inst;
            c.categoryId = "drums";
            c.score = fb;
            c.note = est.note;
            d.candidates.push_back (c);
        }
        if (d.candidates.empty())
        {
            InstrumentCandidate c;
            c.instrument = Instrument::Drums;
            c.categoryId = "drums";
            c.score = fb;
            d.candidates.push_back (c);
        }
        d.message = "Clearly drums, but the drum type is not certain" + (est.note.isNotEmpty() ? " (" + est.note + ")." : juce::String ("."));
        return d;
    }

    // --- Stage 2b: other families: the model's specific classes -------------------------
    if (memberLeads)
    {
        d.status = S::Detected;
        d.confidence = strongFamily && (members.size() == 1 || mb >= dc.highDominanceRatio * std::max (ms, 1.0e-9)) ? C::High : C::Medium;
        d.candidates.push_back (refine (members[0]));
        for (size_t i = 1; i < members.size() && (int) d.candidates.size() < dc.maxCandidates; ++i)
            if (categoryEvidence[(size_t) members[i]] >= dc.presentScore)
                d.candidates.push_back (refine (members[i]));
        return d;
    }

    d.status = S::Ambiguous;
    for (int m : members)
        if ((int) d.candidates.size() < dc.maxCandidates && (d.candidates.empty() || categoryEvidence[(size_t) m] >= dc.candidateRatio * mb))
            d.candidates.push_back (refine (m));
    d.message = "Clearly " + t.families[(size_t) bestFamily].name.toLowerCase() + ", but which instrument is not certain.";
    return d;
}

} // namespace eyra::intelligence
