// EyraEQAnalyze: runs the plug-in's real analysis and instrument recognition on audio files.
//
//   EyraEQAnalyze [--json] file.wav [more files...]
//
// The output is what the plug-in's analysis panel would show for the same audio, plus the measurements
// the decision used. Used to check recognition on real recordings (see Documentation/AI-Models.md).
// Needs ONNX Runtime and the model: set EYRA_ONNXRUNTIME_LIBRARY and EYRA_MODEL_DIR, or run it from a
// folder that contains onnxruntime and Models/ (the build copies them next to the executable).

#include "Analysis/AudioAnalysis.h"
#include "Intelligence/InstrumentClassifier.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <iostream>

using namespace eyra;

namespace
{
const char* statusName (intelligence::InstrumentDetection::Status s)
{
    using S = intelligence::InstrumentDetection::Status;
    switch (s)
    {
        case S::NotRun:        return "not run";
        case S::Unavailable:   return "unavailable";
        case S::NeedMoreAudio: return "need more audio";
        case S::Unknown:       return "unknown";
        case S::Ambiguous:     return "ambiguous";
        case S::Detected:      return "detected";
    }
    return "?";
}

const char* confidenceName (intelligence::InstrumentDetection::Confidence c)
{
    using C = intelligence::InstrumentDetection::Confidence;
    return c == C::High ? "high" : c == C::Medium ? "medium" : "low";
}

bool loadMono (const juce::File& file, std::vector<float>& mono, double& rate)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    const auto length = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 600.0));
    juce::AudioBuffer<float> buffer ((int) reader->numChannels, length);
    reader->read (&buffer, 0, length, 0, true, true);
    mono.assign ((size_t) length, 0.0f);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        juce::FloatVectorOperations::addWithMultiply (mono.data(), buffer.getReadPointer (ch), 1.0f / (float) buffer.getNumChannels(), length);
    rate = reader->sampleRate;
    return true;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit; // (juce_events)
    bool json = false;
    juce::StringArray files;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        if (arg == "--json")
            json = true;
        else
            files.add (arg);
    }
    if (files.isEmpty())
    {
        std::cerr << "usage: EyraEQAnalyze [--json] file.wav [more files...]\n";
        return 2;
    }

    auto classifier = intelligence::InstrumentClassifier::getShared();
    const bool ready = classifier->ensureInitialised();
    if (! ready)
        std::cerr << "instrument recognition unavailable: " << classifier->getStatus() << "\n";

    juce::Array<juce::var> results;
    int failures = 0;
    for (const auto& path : files)
    {
        const juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile (path);
        std::vector<float> mono;
        double rate = 0.0;
        if (! loadMono (file, mono, rate))
        {
            std::cerr << file.getFileName() << ": cannot read\n";
            ++failures;
            continue;
        }

        auto r = analysis::analyseAudio (mono.data(), (int) mono.size(), rate);
        if (ready && r.status != analysis::AnalysisResult::Status::Silent)
            r.instrument = classifier->classify (mono.data(), (int) mono.size(), rate, r);

        const auto& d = r.instrument;
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("file", file.getFileName());
        obj->setProperty ("status", statusName (d.status));
        obj->setProperty ("instrument", intelligence::instrumentName (d.instrument()));
        obj->setProperty ("confidence", confidenceName (d.confidence));
        obj->setProperty ("summary", intelligence::describe (d));
        juce::Array<juce::var> cands, labels;
        for (const auto& c : d.candidates)
        {
            auto* co = new juce::DynamicObject();
            co->setProperty ("instrument", intelligence::instrumentName (c.instrument));
            co->setProperty ("score", c.score);
            co->setProperty ("note", c.note);
            cands.add (juce::var (co));
        }
        juce::StringArray heard;
        for (const auto& [name, score] : d.topLabels)
        {
            labels.add (name);
            heard.add (name + " " + juce::String (score, 2));
        }
        obj->setProperty ("candidates", cands);
        obj->setProperty ("topLabels", labels);
        obj->setProperty ("lowShare", r.lowShare);
        obj->setProperty ("lowMidShare", r.lowMidShare);
        obj->setProperty ("midShare", r.midShare);
        obj->setProperty ("highShare", r.highShare);
        obj->setProperty ("centroidHz", r.spectralCentroidHz);
        obj->setProperty ("flatness", r.spectralFlatness);
        obj->setProperty ("decaySeconds", r.medianDecaySeconds);
        results.add (juce::var (obj));

        if (! json)
        {
            std::cout << file.getFileName() << ": " << intelligence::describe (d) << "\n"
                      << "    measured: " << juce::roundToInt (100 * r.lowShare) << "% <150 Hz, "
                      << juce::roundToInt (100 * r.highShare) << "% >5 kHz, centroid " << juce::roundToInt (r.spectralCentroidHz)
                      << " Hz, noisiness " << juce::String (r.spectralFlatness, 2) << ", decay "
                      << juce::roundToInt (1000 * r.medianDecaySeconds) << " ms\n";
            if (! heard.isEmpty())
                std::cout << "    model hears: " << heard.joinIntoString (", ") << "\n";
        }
    }

    if (json)
        std::cout << juce::JSON::toString (juce::var (results)) << "\n";
    return failures == 0 ? 0 : 1;
}
