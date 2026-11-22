#include "Intelligence/InstrumentClassifier.h"
#include "PluginProcessor.h"
#include "TestHelpers.h"

#include <juce_cryptography/juce_cryptography.h>

#include <iostream>

using namespace eyra::intelligence;
using eyra::analysis::PitchSummary;

namespace
{
juce::File modelsRoot()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("EYRA_MODEL_DIR", {});
   #ifdef EYRA_TEST_MODELS_DIR
    return juce::File (env.isNotEmpty() ? env : juce::String (EYRA_TEST_MODELS_DIR));
   #else
    return juce::File (env);
   #endif
}

bool requireRecognition()
{
    return juce::SystemStats::getEnvironmentVariable ("EYRA_REQUIRE_INSTRUMENT_RECOGNITION", {}) == "1";
}

std::vector<float> readFloats (const juce::File& f)
{
    juce::MemoryBlock b;
    f.loadFileAsData (b);
    std::vector<float> v (b.getSize() / 4);
    for (size_t i = 0; i < v.size(); ++i)
    {
        const auto bits = juce::ByteOrder::littleEndianInt (static_cast<const char*> (b.getData()) + i * 4);
        std::memcpy (&v[i], &bits, 4);
    }
    return v;
}

/** The real taxonomy, resolved against a label list made of its own class names (no model needed). */
Taxonomy loadTaxonomyWithoutModel (juce::StringArray& labels)
{
    const auto json = juce::JSON::parse (modelsRoot().getChildFile ("instrument_taxonomy.json"));
    labels.clear();
    for (const char* key : { "families", "categories" })
        for (const auto& c : *json.getProperty (key, {}).getArray())
            for (const char* classes : { "classes", "percussiveClasses" })
                if (auto* obj = c.getProperty (classes, {}).getDynamicObject())
                    for (const auto& p : obj->getProperties())
                        labels.addIfNotAlreadyThere (p.name.toString());
    labels.add ("Distortion");
    Taxonomy t;
    juce::String error;
    const bool parsed = t.parse (json, labels, error); // (not inside jassert: it must also run in release builds)
    if (! parsed)
        std::cerr << "taxonomy failed to parse: " << error << "\n";
    return t;
}

std::vector<double> evidenceWith (const std::vector<Taxonomy::Group>& groups, double rest,
                                  std::initializer_list<std::pair<const char*, double>> values)
{
    std::vector<double> e (groups.size(), rest);
    for (auto [id, v] : values)
        for (size_t i = 0; i < groups.size(); ++i)
            if (groups[i].id == id)
                e[i] = v;
    return e;
}

/** Evidence for one family (and its members), everything else faint. */
struct Evidence
{
    std::vector<double> families, categories;
};

Evidence evidence (const Taxonomy& t, std::initializer_list<std::pair<const char*, double>> fam,
                   std::initializer_list<std::pair<const char*, double>> cat = {})
{
    const double faint = t.decision.presentScore * 0.2;
    return { evidenceWith (t.families, faint, fam), evidenceWith (t.categories, faint, cat) };
}

/** Measurements typical of each drum (from analysing real one-shots: see Documentation/AI-Models.md). */
SoundFeatures drumFeatures (double low, double lowMid, double mid, double high, double centroid, double flatness, double decay)
{
    SoundFeatures f;
    f.lowShare = low;
    f.lowMidShare = lowMid;
    f.midShare = mid;
    f.highShare = high;
    f.centroidHz = centroid;
    f.flatness = flatness;
    f.decaySeconds = decay;
    f.percussive = true;
    return f;
}

SoundFeatures kickFeatures()     { return drumFeatures (0.92, 0.06, 0.015, 0.005, 120, 0.02, 0.12); }
SoundFeatures floorTomFeatures() { return drumFeatures (0.80, 0.17, 0.02, 0.01, 180, 0.02, 0.38); }
SoundFeatures snareFeatures()    { return drumFeatures (0.08, 0.55, 0.30, 0.07, 1500, 0.22, 0.15); }
SoundFeatures rackTomFeatures()  { return drumFeatures (0.25, 0.70, 0.04, 0.01, 350, 0.03, 0.30); }
SoundFeatures hiHatFeatures()    { return drumFeatures (0.00, 0.02, 0.28, 0.70, 7500, 0.40, 0.08); }
SoundFeatures kitFeatures()      { return drumFeatures (0.35, 0.25, 0.15, 0.25, 2400, 0.20, 0.15); }

PitchSummary pitchAt (double median, double low, double high)
{
    PitchSummary p;
    p.pitched = true;
    p.voicedFraction = 0.9;
    p.medianHz = median;
    p.lowHz = low;
    p.highHz = high;
    return p;
}


/** Synthetic one-shot drums repeated every half second for 8 s (no samples needed). */
std::vector<float> drumLoop (const char* piece, double sr = 48000.0)
{
    juce::Random rng (1234);
    const int hit = (int) (0.5 * sr), n = (int) (8.0 * sr);
    std::vector<float> one ((size_t) hit, 0.0f);
    double phase = 0.0, lp = 0.0, prev = 0.0;
    for (int i = 0; i < hit; ++i)
    {
        const double t = i / sr;
        const double noise = rng.nextFloat() * 2.0 - 1.0;
        double v = 0.0;
        if (juce::String (piece) == "kick")          // pitch-dropping sine, short decay
        {
            phase += juce::MathConstants<double>::twoPi * (50.0 + 90.0 * std::exp (-t / 0.02)) / sr;
            v = std::sin (phase) * std::exp (-t / 0.045);
        }
        else if (juce::String (piece) == "floortom") // low sine, long decay
        {
            phase += juce::MathConstants<double>::twoPi * (85.0 + 20.0 * std::exp (-t / 0.03)) / sr;
            v = std::sin (phase) * std::exp (-t / 0.16);
        }
        else if (juce::String (piece) == "racktom")  // mid sine, medium decay
        {
            phase += juce::MathConstants<double>::twoPi * (190.0 + 30.0 * std::exp (-t / 0.03)) / sr;
            v = std::sin (phase) * std::exp (-t / 0.12);
        }
        else if (juce::String (piece) == "snare")    // body + band-limited wire noise
        {
            phase += juce::MathConstants<double>::twoPi * 200.0 / sr;
            lp += 0.35 * (noise - lp);
            v = 0.5 * std::sin (phase) * std::exp (-t / 0.04) + 0.9 * lp * std::exp (-t / 0.07);
        }
        else                                         // hi-hat: high-passed noise, very short
        {
            v = (noise - prev) * std::exp (-t / 0.025);
            prev = noise;
        }
        one[(size_t) i] = (float) (0.5 * v);
    }
    std::vector<float> loop ((size_t) n, 0.0f);
    for (int s = 0; s < n; s += hit)
        for (int i = 0; i < hit && s + i < n; ++i)
            loop[(size_t) (s + i)] = one[(size_t) i];
    return loop;
}

void writeFile (const juce::File& f, const juce::String& text) { f.replaceWithText (text); }

class IntelligenceTests final : public juce::UnitTest
{
public:
    IntelligenceTests() : juce::UnitTest ("Instrument recognition", "Intelligence") {}

    void runTest() override
    {
        using S = InstrumentDetection::Status;
        juce::StringArray labels;
        const auto t = loadTaxonomyWithoutModel (labels);
        const eyra::analysis::AnalysisResult noAnalysis {};

        beginTest ("Taxonomy loads and rejects invalid definitions");
        {
            expectGreaterOrEqual ((int) t.categories.size(), 17, "taxonomy loaded from " + modelsRoot().getFullPathName());
            if (t.categories.size() < 17)
                return; // nothing below is meaningful without the taxonomy
            expect (t.refinements.distortionIndex >= 0);

            Taxonomy bad;
            juce::String error;
            auto json = juce::JSON::parse (R"({"families":[{"id":"drums","name":"Drums","classes":{"Drum":1}},{"id":"keys","name":"Keys","classes":{"Piano":1}}],
                                               "categories":[{"id":"kick","family":"drums","name":"Kick","classes":{"Not a label":1.0}},{"id":"snare","family":"drums","name":"S","classes":{"Snare drum":1}}]})");
            expect (! bad.parse (json, labels, error));
            expect (error.contains ("unknown label"), error);
            expect (! bad.parse (juce::var(), labels, error));
            json = juce::JSON::parse (R"({"families":[{"id":"drums","name":"Drums","classes":{"Drum":1}},{"id":"keys","name":"Keys","classes":{"Piano":1}}],
                                         "categories":[{"id":"kick","family":"nope","name":"Kick","classes":{"Bass drum":1}},{"id":"snare","family":"drums","name":"S","classes":{"Snare drum":1}}]})");
            expect (! bad.parse (json, labels, error), "a category must belong to a known family");
            expectEquals ((int) t.families.size(), 7);
        }

        beginTest ("Decision: family stage (unknown, ambiguous, more audio needed)");
        {
            // Values are derived from the thresholds in the taxonomy file, so recalibration keeps these meaningful.
            const auto& dc = t.decision;
            const SoundFeatures none;

            auto weak = evidence (t, {});
            std::fill (weak.families.begin(), weak.families.end(), dc.presentScore * 0.5);
            auto d = InstrumentClassifier::decide (t, weak.families, weak.categories, 0.0, 10.0, none);
            expect (d.status == S::Unknown, "weak evidence is not forced into a category");

            const double close = dc.familyConfidentScore + 0.05;
            auto e = evidence (t, { { "keys", close }, { "guitar", close * 0.9 } }, { { "piano", close }, { "electric_guitar", close } });
            d = InstrumentClassifier::decide (t, e.families, e.categories, 0.0, 10.0, none);
            expect (d.status == S::Ambiguous, "close families are not presented as a fact");
            expectEquals ((int) d.candidates.size(), 2);
            expect (d.instrument() == Instrument::Unknown);

            e = evidence (t, { { "drums", 0.9 } });
            d = InstrumentClassifier::decide (t, e.families, e.categories, 0.0, dc.minActiveSeconds - 0.5, kickFeatures());
            expect (d.status == S::NeedMoreAudio, "a short fragment is not enough");
        }

        beginTest ("Decision: member stage for pitched families");
        {
            const auto& dc = t.decision;
            const SoundFeatures none;
            const double high = std::max ({ dc.highConfidenceScore, dc.familyConfidentScore, dc.memberConfidentScore }) + 0.1;
            auto e = evidence (t, { { "keys", high } }, { { "piano", high }, { "organ", high / (dc.highDominanceRatio + 0.5) } });
            auto d = InstrumentClassifier::decide (t, e.families, e.categories, 0.0, 10.0, none);
            expect (d.status == S::Detected);
            expect (d.instrument() == Instrument::Piano);
            expect (d.confidence == InstrumentDetection::Confidence::High);

            const double medium = std::max (dc.familyConfidentScore, dc.memberConfidentScore) + 0.01;
            expect (medium < dc.highConfidenceScore);
            e = evidence (t, { { "keys", medium } }, { { "piano", medium }, { "organ", medium / (dc.memberDominanceRatio + 0.2) } });
            d = InstrumentClassifier::decide (t, e.families, e.categories, 0.0, 10.0, none);
            expect (d.status == S::Detected);
            expect (d.confidence == InstrumentDetection::Confidence::Medium);

            e = evidence (t, { { "keys", high } }, { { "piano", high }, { "organ", high * 0.9 } });
            d = InstrumentClassifier::decide (t, e.families, e.categories, 0.0, 10.0, none);
            expect (d.status == S::Ambiguous, "the family is clear but the member is not");
            expectGreaterOrEqual ((int) d.candidates.size(), 2);
            expect (d.message.contains ("keys"), d.message);

            // A single-member family still needs its member to stand out from every other category.
            e = evidence (t, { { "vocal", high } }, { { "vocal", high } });
            d = InstrumentClassifier::decide (t, e.families, e.categories, 0.0, 10.0, none);
            expect (d.status == S::Detected);
            expect (d.instrument() == Instrument::Vocal);
            e = evidence (t, { { "vocal", high } }, { { "vocal", high }, { "synth", high * 0.9 } });
            d = InstrumentClassifier::decide (t, e.families, e.categories, 0.0, 10.0, none);
            expect (d.status == S::Ambiguous, "strong evidence for another instrument means doubt");
        }

        beginTest ("Drums: the family comes from the model, the piece from the sound");
        {
            auto e = evidence (t, { { "drums", 0.6 } }); // the model only hears "drum", nothing specific
            auto piece = [&] (const SoundFeatures& f) { return InstrumentClassifier::decide (t, e.families, e.categories, 0.0, 10.0, f); };

            auto d = piece (kickFeatures());
            expect (d.status == S::Detected);
            expect (d.instrument() == Instrument::KickDrum);
            expect (d.candidates.front().note.contains ("150 Hz"), d.candidates.front().note);
            expect (piece (floorTomFeatures()).instrument() == Instrument::FloorTom);
            expect (piece (snareFeatures()).instrument() == Instrument::SnareDrum);
            expect (piece (rackTomFeatures()).instrument() == Instrument::RackTom);
            expect (piece (hiHatFeatures()).instrument() == Instrument::Cymbals);
            expect (piece (kitFeatures()).instrument() == Instrument::Overheads);

            // Borderline decay: kick or floor tom, not a guess.
            auto borderline = kickFeatures();
            borderline.decaySeconds = t.drumRules.kickMaxDecaySeconds * 1.05;
            d = piece (borderline);
            expect (d.status == S::Ambiguous);
            expectEquals ((int) d.candidates.size(), 2);
            expect (d.candidates[0].instrument == Instrument::KickDrum && d.candidates[1].instrument == Instrument::FloorTom);

            auto noisyTom = rackTomFeatures();
            noisyTom.flatness = t.drumRules.snareMinFlatness;
            expect (piece (noisyTom).status == S::Ambiguous);

            // No measurements at all: drums, without inventing the piece.
            d = piece (SoundFeatures {});
            expect (d.status == S::Ambiguous);
            expect (d.candidates.size() == 1 && d.candidates[0].instrument == Instrument::Drums);

            // The model's own hand-percussion classes win over the kit rules.
            const double perc = std::max (t.drumRules.modelPercussionScore, t.decision.memberConfidentScore) + 0.1;
            e = evidence (t, { { "drums", 0.6 } }, { { "percussion", perc } });
            expect (piece (snareFeatures()).instrument() == Instrument::Percussion);

            // Without measured regular hits the piece is only a possibility.
            e = evidence (t, { { "drums", 0.6 } });
            auto sustained = kickFeatures();
            sustained.percussive = false;
            d = piece (sustained);
            expect (d.status == S::Ambiguous);
            expect (d.candidates.size() == 1 && d.candidates[0].instrument == Instrument::KickDrum);

            // When the model also names the piece, confidence rises.
            e = evidence (t, { { "drums", 0.9 } }, { { "cymbals", 0.8 } });
            d = piece (hiHatFeatures());
            expect (d.instrument() == Instrument::Cymbals);
            expect (d.confidence == InstrumentDetection::Confidence::High);

            // Pitch splits a tonal tom into rack or floor.
            auto lowTom = rackTomFeatures();
            lowTom.pitch = pitchAt (85, 80, 95);
            expect (estimateDrumPiece (t.drumRules, lowTom, t.refinements.rackFloorTomSplitHz).piece == Instrument::FloorTom);

            // The model hears nothing musical, but regular hits were measured: offered as a possibility only.
            auto silentModel = evidence (t, {});
            auto hits = kickFeatures();
            d = InstrumentClassifier::decide (t, silentModel.families, silentModel.categories, 0.0, 10.0, hits);
            expect (d.status == S::Ambiguous, "measurements alone never give a detection");
            expect (d.candidates.size() == 1 && d.candidates[0].instrument == Instrument::KickDrum);
            expect (d.candidates[0].note.contains ("measurements only"), d.candidates[0].note);
            hits.percussive = false;
            d = InstrumentClassifier::decide (t, silentModel.families, silentModel.categories, 0.0, 10.0, hits);
            expect (d.status == S::Unknown, "without measured hits nothing is suggested");

            // If the family is unclear, drums is still offered with its estimated piece.
            const double close = t.decision.familyConfidentScore + 0.05;
            e = evidence (t, { { "drums", close }, { "other", close * 0.9 } }, { { "acoustic_other", close } });
            d = piece (kickFeatures());
            expect (d.status == S::Ambiguous);
            expect (std::any_of (d.candidates.begin(), d.candidates.end(), [] (auto& c) { return c.instrument == Instrument::KickDrum; }));
        }

        beginTest ("Refinements use measured evidence and say so");
        {
            const SoundFeatures none;
            auto withPitch = [] (PitchSummary p) { SoundFeatures f; f.pitch = p; return f; };
            auto run = [&] (const Evidence& e, double distortion, const SoundFeatures& f)
            {
                return InstrumentClassifier::decide (t, e.families, e.categories, distortion, 10.0, f);
            };

            auto vocal = evidence (t, { { "vocal", 0.8 } }, { { "vocal", 0.8 } });
            expect (run (vocal, 0, withPitch (pitchAt (120, 95, 180))).instrument() == Instrument::MaleVocal);
            expect (run (vocal, 0, withPitch (pitchAt (330, 240, 520))).instrument() == Instrument::FemaleVocal);
            auto unsure = run (vocal, 0, withPitch (pitchAt (200, 150, 300)));
            expect (unsure.instrument() == Instrument::Vocal, "overlapping range stays generic");
            expect (unsure.candidates.front().note.isNotEmpty());
            expect (run (vocal, 0, none).instrument() == Instrument::Vocal);

            auto guitar = evidence (t, { { "guitar", 0.8 } }, { { "electric_guitar", 0.8 } });
            expect (run (guitar, 0.4, none).instrument() == Instrument::DistortedGuitar);
            expect (run (guitar, 0.01, none).instrument() == Instrument::CleanGuitar);
            expect (run (guitar, 0.12, none).instrument() == Instrument::ElectricGuitar);

            auto synth = evidence (t, { { "keys", 0.8 } }, { { "synth", 0.8 } });
            expect (run (synth, 0, withPitch (pitchAt (55, 41, 82))).instrument() == Instrument::SynthBass);
            expect (run (synth, 0, withPitch (pitchAt (440, 300, 600))).instrument() == Instrument::Synthesizer);
        }

        beginTest ("Drum measurements: the real analysis tells synthetic drums apart");
        {
            const std::pair<const char*, Instrument> pieces[] = { { "kick", Instrument::KickDrum }, { "floortom", Instrument::FloorTom },
                                                                  { "racktom", Instrument::RackTom }, { "snare", Instrument::SnareDrum },
                                                                  { "hihat", Instrument::Cymbals } };
            for (auto [name, expected] : pieces)
            {
                const auto audio = drumLoop (name);
                const auto analysis = eyra::analysis::analyseAudio (audio.data(), (int) audio.size(), 48000.0);
                const auto f = SoundFeatures::from (analysis);
                const auto est = estimateDrumPiece (t.drumRules, f, t.refinements.rackFloorTomSplitHz);
                expect (est.piece == expected, juce::String (name) + ": got " + instrumentName (est.piece) + " (" + est.note
                                                 + ", low " + juce::String (f.lowShare, 2) + ", high " + juce::String (f.highShare, 2)
                                                 + ", flatness " + juce::String (f.flatness, 2) + ", decay " + juce::String (f.decaySeconds, 3) + ")");
                expect (f.percussive, juce::String (name) + " is measured as regular hits");
            }
        }

        beginTest ("Model manager: validation, damage detection and update preference");
        {
            auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("eyra_model_test");
            root.deleteRecursively();
            auto bundled = root.getChildFile ("bundled"), user = root.getChildFile ("user");

            auto makeModel = [this] (const juce::File& r, const juce::String& version)
            {
                auto dir = r.getChildFile ("yamnet");
                dir.createDirectory();
                dir.getChildFile ("model.onnx").replaceWithText ("not really a model " + version);
                dir.getChildFile ("mel.f32").replaceWithText ("mel");
                auto obj = std::make_unique<juce::DynamicObject>();
                obj->setProperty ("name", "yamnet");
                obj->setProperty ("version", version);
                obj->setProperty ("file", "model.onnx");
                obj->setProperty ("sha256", ModelManager::sha256Of (dir.getChildFile ("model.onnx")));
                obj->setProperty ("labels", juce::Array<juce::var> { "a", "b" });
                auto input = std::make_unique<juce::DynamicObject>();
                input->setProperty ("name", "in");
                obj->setProperty ("input", input.release());
                auto outputs = std::make_unique<juce::DynamicObject>();
                outputs->setProperty ("scores", "out");
                obj->setProperty ("outputs", outputs.release());
                auto fe = std::make_unique<juce::DynamicObject>();
                fe->setProperty ("melMatrix", "mel.f32");
                fe->setProperty ("melMatrixSha256", ModelManager::sha256Of (dir.getChildFile ("mel.f32")));
                obj->setProperty ("frontend", fe.release());
                writeFile (dir.getChildFile ("manifest.json"), juce::JSON::toString (juce::var (obj.release())));
                expect (ModelManager::inspect (dir).valid, ModelManager::inspect (dir).message);
                return dir;
            };

            makeModel (bundled, "1.0.0");
            auto userDir = makeModel (user, "1.2.0");
            ModelManager::setSearchRootsForTesting ({ user, bundled });
            auto best = ModelManager::findBestModel ("yamnet");
            expect (best.valid && best.version == "1.2.0", "newer installed model is preferred");

            userDir.getChildFile ("model.onnx").replaceWithText ("tampered");
            expect (! ModelManager::inspect (userDir).valid, "checksum mismatch is detected");
            best = ModelManager::findBestModel ("yamnet");
            expect (best.valid && best.version == "1.0.0", "a damaged update falls back to the bundled model");

            userDir.getChildFile ("model.onnx").deleteFile();
            expect (ModelManager::inspect (userDir).message.contains ("missing"));

            ModelManager::setSearchRootsForTesting ({ root.getChildFile ("empty") });
            expect (! ModelManager::findBestModel ("yamnet").valid);
            ModelManager::setSearchRootsForTesting ({});

            expectEquals (ModelManager::compareVersions ("1.10.0", "1.9.2"), 1);
            expectEquals (ModelManager::compareVersions ("2.0", "2.0.0"), 0);
            root.deleteRecursively();
        }

        beginTest ("Missing model: recognition reports unavailable, nothing else breaks");
        {
            ModelManager::setSearchRootsForTesting ({ juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("eyra_no_models") });
            auto c = InstrumentClassifier::createForTesting();
            expect (! c->ensureInitialised());
            expect (c->getStatus().contains ("unavailable"), c->getStatus());
            std::vector<float> audio (48000 * 5, 0.1f);
            auto d = c->classify (audio.data(), (int) audio.size(), 48000.0, noAnalysis);
            expect (d.status == S::Unavailable);
            ModelManager::setSearchRootsForTesting ({});
        }

        beginTest ("Manual instrument choice is saved with the session");
        {
            EyraEQAudioProcessor a;
            a.instrumentOverride = (int) Instrument::FloorTom;
            juce::MemoryBlock mb;
            a.getStateInformation (mb);
            EyraEQAudioProcessor b;
            b.setStateInformation (mb.getData(), (int) mb.getSize());
            expectEquals (b.instrumentOverride.load(), (int) Instrument::FloorTom);
            expect (b.getEffectiveInstrument() == Instrument::FloorTom);
        }

        // --- Tests with the real model and runtime ------------------------------------------
        auto classifier = InstrumentClassifier::createForTesting();
        const bool ready = classifier->ensureInitialised();
        if (! ready)
        {
            beginTest ("Model and runtime availability");
            logMessage ("SKIPPED model tests: " + classifier->getStatus());
            expect (! requireRecognition(), "instrument recognition must be available in this build: " + classifier->getStatus());
            return;
        }

        const auto& info = classifier->getModelInfo();
        const auto ref = info.manifest.getProperty ("reference", {});
        const auto dir = info.directory;

        beginTest ("Front end reproduces TensorFlow's log-mel patches");
        {
            const auto wave = readFloats (dir.getChildFile (ref.getProperty ("waveform", "").toString()));
            const auto expected = readFloats (dir.getChildFile (ref.getProperty ("patches", "").toString()));
            const auto& fe = classifier->getFeatureExtractor();
            auto padded = fe.padForPatches (wave);
            int frames = 0, patches = 0;
            const auto logMel = fe.logMel (padded, frames);
            const auto p = fe.patches (logMel, frames, patches);
            expectEquals (patches, (int) ref.getProperty ("numPatches", 0));
            double maxErr = 0.0;
            for (size_t i = 0; i < std::min (p.size(), expected.size()); ++i)
                maxErr = std::max (maxErr, (double) std::abs (p[i] - expected[i]));
            logMessage ("max log-mel difference " + juce::String (maxErr, 6));
            expectLessThan (maxErr, 2.0e-3);
        }

        beginTest ("ONNX inference reproduces TensorFlow's scores (self-test)");
        {
            const auto patches = readFloats (dir.getChildFile (ref.getProperty ("patches", "").toString()));
            const auto expected = readFloats (dir.getChildFile (ref.getProperty ("scores", "").toString()));
            std::vector<float> scores;
            juce::String error;
            expect (classifier->runModel (patches, (int) ref.getProperty ("numPatches", 0), scores, error), error);
            double maxErr = 0.0;
            for (size_t i = 0; i < std::min (scores.size(), expected.size()); ++i)
                maxErr = std::max (maxErr, (double) std::abs (scores[i] - expected[i]));
            logMessage ("max score difference " + juce::String (maxErr, 6) + " (" + classifier->getStatus() + ")");
            expectLessThan (maxErr, 2.0e-3);
        }

        beginTest ("Classification runs at any sample rate and gives consistent results");
        {
            const auto wave16 = readFloats (dir.getChildFile (ref.getProperty ("waveform", "").toString()));
            auto at16 = classifier->classify (wave16.data(), (int) wave16.size(), 16000.0, noAnalysis);
            expect (at16.status != S::Unavailable, at16.message);
            expect (! at16.topLabels.empty());

            // Upsample 3x by linear interpolation and classify at 48 kHz.
            std::vector<float> wave48 (wave16.size() * 3);
            for (size_t i = 0; i < wave48.size(); ++i)
            {
                const double pos = i / 3.0;
                const size_t k = std::min ((size_t) pos, wave16.size() - 2);
                wave48[i] = (float) (wave16[k] + (pos - k) * (wave16[k + 1] - wave16[k]));
            }
            auto at48 = classifier->classify (wave48.data(), (int) wave48.size(), 48000.0, noAnalysis);
            expect (! at48.topLabels.empty());
            if (! at16.topLabels.empty() && ! at48.topLabels.empty())
            {
                logMessage ("16 kHz top label: " + at16.topLabels[0].first + " " + juce::String (at16.topLabels[0].second, 3)
                            + "; 48 kHz: " + at48.topLabels[0].first + " " + juce::String (at48.topLabels[0].second, 3));
                expectEquals (at48.topLabels[0].first, at16.topLabels[0].first);
            }

            std::vector<float> silence (48000 * 5, 0.0f);
            auto s = classifier->classify (silence.data(), (int) silence.size(), 48000.0, noAnalysis);
            expect (s.status != S::Detected, "silence is never classified as an instrument");
        }
    }
};

static IntelligenceTests intelligenceTests;
} // namespace
