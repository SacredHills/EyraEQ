#pragma once

#include "Intelligence/InstrumentTypes.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace eyra::analysis
{

/** Kinds of observation the analysis can report. */
enum class FindingType
{
    Rumble,           // sub-sonic energy below the musical content
    DCOffset,         // constant offset in the signal
    Hum,              // mains hum (50 / 60 Hz series)
    Buildup,          // broad region standing out from the surrounding spectrum (mud, boxiness, harshness...)
    Resonance,        // persistent narrow peak not explained by the source's harmonics
    IntermittentResonance, // narrow peak present only part of the time
    Ringing           // narrow band that decays markedly slower than its surroundings after transients
};

enum class Severity
{
    Minor,
    Moderate,
    Significant
};

struct Finding
{
    FindingType type = FindingType::Resonance;
    juce::String title;        // e.g. "Low-mid buildup (mud)"
    juce::String detail;       // measured evidence in plain language

    double frequency = 0.0;    // centre / peak (Hz)
    double lowFrequency = 0.0; // extent for broad findings (Hz); equal to frequency for narrow ones
    double highFrequency = 0.0;
    double excessDb = 0.0;     // how far it stands out from its context
    double q = 0.0;            // estimated Q for narrow findings
    double persistence = 0.0;  // fraction of analysed frames in which it is present (0..1)
    double decayRatio = 0.0;   // ringing: neighbourhood decay rate / band decay rate (> 1 = rings longer)
    double ringTimeSeconds = 0.0;

    double severity = 0.0;     // 0..1
    double confidence = 0.0;   // 0..1

    Severity severityLabel() const noexcept
    {
        return severity >= 0.7 ? Severity::Significant : (severity >= 0.4 ? Severity::Moderate : Severity::Minor);
    }

    bool isNarrow() const noexcept
    {
        return type == FindingType::Resonance || type == FindingType::IntermittentResonance
            || type == FindingType::Ringing || type == FindingType::Hum;
    }
};

struct PitchSummary
{
    bool pitched = false;          // a single dominant pitch is present most of the time
    double voicedFraction = 0.0;   // share of active frames with a clear periodicity
    double medianHz = 0.0;
    double lowHz = 0.0, highHz = 0.0; // 10th / 90th percentile of the detected pitch
    double spreadCents = 0.0;      // inter-quartile spread of the pitch
};

struct AnalysisResult
{
    enum class Status
    {
        Ok,
        InsufficientAudio,
        Silent
    };

    Status status = Status::Silent;
    juce::String message;

    double sampleRate = 0.0;
    double totalSeconds = 0.0;     // length of audio given to the analysis
    double activeSeconds = 0.0;    // non-silent audio actually analysed
    double confidence = 0.0;       // overall, mostly driven by the amount of audio

    PitchSummary pitch;
    bool percussive = false;
    double onsetsPerSecond = 0.0;
    double spectralTiltDbPerOct = 0.0; // 100 Hz - 10 kHz
    double spectralCentroidHz = 0.0;

    /** Share of the energy (20 Hz - 20 kHz) in four regions: < 150 Hz, 150 Hz - 1 kHz, 1 - 5 kHz, > 5 kHz. */
    double lowShare = 0.0, lowMidShare = 0.0, midShare = 0.0, highShare = 0.0;
    /** Spectral flatness of the long-term spectrum, 150 Hz - 8 kHz (0 = tonal, 1 = white noise). */
    double spectralFlatness = 0.0;
    /** Median time for a hit to fall 20 dB below its peak (percussive material), seconds; 0 if not measured. */
    double medianDecaySeconds = 0.0;
    double lowestContentHz = 0.0, highestContentHz = 0.0;

    /** Problems, most severe first. */
    std::vector<Finding> findings;

    /** Instrument recognition (filled in by the AnalysisController when the model is available). */
    intelligence::InstrumentDetection instrument;

    /** Long-term average spectrum (dBFS per FFT bin of width ltasBinHz, 1/6-octave smoothed) at displayFrequencies. */
    std::vector<float> ltasDb;
    double ltasBinHz = 1.0;
    std::vector<double> displayFrequencies;
};

struct AnalysisSettings
{
    double minActiveSeconds = 2.0;    // below this, no findings are reported
    double fullConfidenceSeconds = 10.0;
    double resonanceProminenceDb = 6.0;
    double buildupThresholdDb = 2.5;
    int maxResonances = 8;
    int maxFindings = 14;
    int displayPoints = 512;
    double displayMinHz = 10.0, displayMaxHz = 30000.0;
};

/**
    Analyses a mono signal and reports frequency problems. Pure function: no global state, safe to call
    from any non-real-time thread. Cost: well under a second for 30 s of audio at 48 kHz.
*/
AnalysisResult analyseAudio (const float* mono, int numSamples, double sampleRate, const AnalysisSettings& settings = {});

juce::String describeFrequency (double hz);
juce::String noteName (double hz);

} // namespace eyra::analysis
