// EyraEQAnalyze: runs the plug-in's real analysis and correction logic on audio files.
//
//   EyraEQAnalyze [--instrument "Snare drum"] [--strength Gentle|Moderate|Aggressive] [--polished] [--auto-highpass] file.wav ...
//
// Prints what the plug-in's analysis panel would show: the findings and the proposed EQ bands.

#include "Analysis/AudioAnalysis.h"
#include "Correction/CorrectionGenerator.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <iostream>

using namespace eyra;

namespace
{
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
    juce::ScopedJuceInitialiser_GUI juceInit;
    correction::Settings settings;
    juce::StringArray files;
    juce::File screenshot;
    bool applyInScreenshot = false;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        if (arg == "--instrument" && i + 1 < argc)
        {
            const juce::String name (argv[++i]);
            for (int k = 0; k < (int) correction::Instrument::NumInstruments; ++k)
                if (correction::instrumentName ((correction::Instrument) k).equalsIgnoreCase (name))
                    settings.instrument = (correction::Instrument) k;
        }
        else if (arg == "--strength" && i + 1 < argc)
            settings.strength = (correction::Strength) juce::jmax (0, correction::strengthNames().indexOf (juce::String (argv[++i]), true));
        else if (arg == "--polished")
            settings.mode = correction::Mode::Polished;
        else if (arg == "--auto-highpass")
            settings.autoHighPass = true;
        else if (arg == "--screenshot" && i + 1 < argc)
            screenshot = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]);
        else if (arg == "--apply")
            applyInScreenshot = true;
        else
            files.add (arg);
    }
    if (files.isEmpty())
    {
        std::cerr << "usage: EyraEQAnalyze [--instrument NAME] [--strength Gentle|Moderate|Aggressive] [--polished] [--auto-highpass] file.wav ...\n";
        return 2;
    }

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

        const auto r = analysis::analyseAudio (mono.data(), (int) mono.size(), rate);
        std::cout << file.getFileName() << " (" << correction::instrumentName (settings.instrument) << "): " << r.message << "\n";
        for (const auto& f : r.findings)
            std::cout << "  finding: " << f.title << " (severity " << juce::String (f.severity, 2) << ")\n";
        for (const auto& s : correction::generateCorrections (r, settings))
            std::cout << "  " << (s.band ? "EQ  " : "--  ") << s.title << "  [" << s.reason << "]\n";
    }

    // Developer aid: the plug-in's editor after analysing the first file, as an image.
    if (screenshot != juce::File() && ! files.isEmpty())
    {
        EyraEQAudioProcessor processor;
        processor.setCorrectionSettings (settings);
        processor.analysis.analyseFile (juce::File::getCurrentWorkingDirectory().getChildFile (files[0]));
        processor.analysis.waitUntilFinished (60000);
        if (applyInScreenshot)
        {
            std::vector<dsp::BandParams> bands;
            for (const auto& s : processor.getSuggestions())
                if (s.band)
                    bands.push_back (*s.band);
            processor.applyCorrections (bands);
        }
        std::unique_ptr<EyraEQAudioProcessorEditor> editor (dynamic_cast<EyraEQAudioProcessorEditor*> (processor.createEditor()));
        editor->setSize (1240, 760);
        editor->refreshNow();
        juce::PNGImageFormat png;
        juce::FileOutputStream out (screenshot);
        out.setPosition (0);
        out.truncate();
        png.writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds()), out);
        std::cout << "screenshot: " << screenshot.getFullPathName() << "\n";
    }
    return failures == 0 ? 0 : 1;
}
