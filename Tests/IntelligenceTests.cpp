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
    for (const auto& c : *json.getProperty ("categories", {}).getArray())
        if (auto* obj = c.getProperty ("classes", {}).getDynamicObject())
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

int categoryIndex (const Taxonomy& t, const juce::String& id)
{
    for (size_t i = 0; i < t.categories.size(); ++i)
        if (t.categories[i].id == id)
            return (int) i;
    return -1;
}

std::vector<double> evidenceWith (const Taxonomy& t, std::initializer_list<std::pair<const char*, double>> values)
{
    std::vector<double> e (t.categories.size(), t.decision.presentScore * 0.2);
    for (auto [id, v] : values)
        e[(size_t) categoryIndex (t, id)] = v;
    return e;
}

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
        const PitchSummary noPitch;

        beginTest ("Taxonomy loads and rejects invalid definitions");
        {
            expectGreaterOrEqual ((int) t.categories.size(), 17, "taxonomy loaded from " + modelsRoot().getFullPathName());
            if (t.categories.size() < 17)
                return; // nothing below is meaningful without the taxonomy
            expect (t.refinements.distortionIndex >= 0);

            Taxonomy bad;
            juce::String error;
            auto json = juce::JSON::parse (R"({"categories":[{"id":"kick","name":"Kick","classes":{"Not a label":1.0}},{"id":"snare","name":"S","classes":{"Snare drum":1}}]})");
            expect (! bad.parse (json, labels, error));
            expect (error.contains ("unknown label"), error);
            expect (! bad.parse (juce::var(), labels, error));
        }

        beginTest ("Decision: detected, confidence levels, ambiguous, unknown, more audio needed");
        {
            // Values are derived from the thresholds in the taxonomy file, so recalibration keeps these meaningful.
            const auto& dc = t.decision;
            const double high = std::max (dc.highConfidenceScore, dc.confidentScore) + 0.1;
            auto d = InstrumentClassifier::decide (t, evidenceWith (t, { { "snare", high }, { "toms", high / (std::max (dc.highDominanceRatio, dc.dominanceRatio) + 0.5) } }), 0.0, 10.0, noPitch);
            expect (d.status == S::Detected);
            expect (d.instrument() == Instrument::SnareDrum);
            expect (d.confidence == InstrumentDetection::Confidence::High);

            const double medium = dc.confidentScore + 0.01;
            expect (medium < dc.highConfidenceScore);
            d = InstrumentClassifier::decide (t, evidenceWith (t, { { "snare", medium }, { "toms", medium / (dc.dominanceRatio + 0.2) } }), 0.0, 10.0, noPitch);
            expect (d.status == S::Detected);
            expect (d.confidence == InstrumentDetection::Confidence::Medium);

            const double close = dc.confidentScore + 0.05;
            d = InstrumentClassifier::decide (t, evidenceWith (t, { { "piano", close }, { "organ", close * 0.9 }, { "synth", close * 0.7 } }), 0.0, 10.0, noPitch);
            expect (d.status == S::Ambiguous, "close scores are not presented as a fact");
            expectEquals ((int) d.candidates.size(), 3);
            expect (d.instrument() == Instrument::Unknown);

            auto weak = evidenceWith (t, {});
            std::fill (weak.begin(), weak.end(), dc.presentScore * 0.5);
            d = InstrumentClassifier::decide (t, weak, 0.0, 10.0, noPitch);
            expect (d.status == S::Unknown, "weak evidence is not forced into a category");

            d = InstrumentClassifier::decide (t, evidenceWith (t, { { "snare", 0.9 } }), 0.0, dc.minActiveSeconds - 0.5, noPitch);
            expect (d.status == S::NeedMoreAudio, "a short fragment is not enough");
        }

        beginTest ("Refinements use measured evidence and say so");
        {
            auto vocal = evidenceWith (t, { { "vocal", 0.6 } });
            expect (InstrumentClassifier::decide (t, vocal, 0, 10, pitchAt (120, 95, 180)).instrument() == Instrument::MaleVocal);
            expect (InstrumentClassifier::decide (t, vocal, 0, 10, pitchAt (330, 240, 520)).instrument() == Instrument::FemaleVocal);
            auto unsure = InstrumentClassifier::decide (t, vocal, 0, 10, pitchAt (200, 150, 300));
            expect (unsure.instrument() == Instrument::Vocal, "overlapping range stays generic");
            expect (unsure.candidates.front().note.isNotEmpty());
            expect (InstrumentClassifier::decide (t, vocal, 0, 10, noPitch).instrument() == Instrument::Vocal);

            auto toms = evidenceWith (t, { { "toms", 0.5 } });
            expect (InstrumentClassifier::decide (t, toms, 0, 10, pitchAt (150, 140, 165)).instrument() == Instrument::RackTom);
            expect (InstrumentClassifier::decide (t, toms, 0, 10, pitchAt (85, 80, 95)).instrument() == Instrument::FloorTom);
            expect (InstrumentClassifier::decide (t, toms, 0, 10, noPitch).instrument() == Instrument::Toms);

            auto guitar = evidenceWith (t, { { "electric_guitar", 0.6 } });
            expect (InstrumentClassifier::decide (t, guitar, 0.4, 10, noPitch).instrument() == Instrument::DistortedGuitar);
            expect (InstrumentClassifier::decide (t, guitar, 0.01, 10, noPitch).instrument() == Instrument::CleanGuitar);
            expect (InstrumentClassifier::decide (t, guitar, 0.12, 10, noPitch).instrument() == Instrument::ElectricGuitar);

            auto synth = evidenceWith (t, { { "synth", 0.6 } });
            expect (InstrumentClassifier::decide (t, synth, 0, 10, pitchAt (55, 41, 82)).instrument() == Instrument::SynthBass);
            expect (InstrumentClassifier::decide (t, synth, 0, 10, pitchAt (440, 300, 600)).instrument() == Instrument::Synthesizer);
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
            auto d = c->classify (audio.data(), (int) audio.size(), 48000.0, noPitch);
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
            auto at16 = classifier->classify (wave16.data(), (int) wave16.size(), 16000.0, noPitch);
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
            auto at48 = classifier->classify (wave48.data(), (int) wave48.size(), 48000.0, noPitch);
            expect (! at48.topLabels.empty());
            if (! at16.topLabels.empty() && ! at48.topLabels.empty())
            {
                logMessage ("16 kHz top label: " + at16.topLabels[0].first + " " + juce::String (at16.topLabels[0].second, 3)
                            + "; 48 kHz: " + at48.topLabels[0].first + " " + juce::String (at48.topLabels[0].second, 3));
                expectEquals (at48.topLabels[0].first, at16.topLabels[0].first);
            }

            std::vector<float> silence (48000 * 5, 0.0f);
            auto s = classifier->classify (silence.data(), (int) silence.size(), 48000.0, noPitch);
            expect (s.status != S::Detected, "silence is never classified as an instrument");
        }
    }
};

static IntelligenceTests intelligenceTests;
} // namespace
