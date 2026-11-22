#pragma once

#include "Analysis/AudioAnalysis.h"
#include "Correction/Instrument.h"
#include "DSP/EQBand.h"

#include <juce_data_structures/juce_data_structures.h>

#include <optional>
#include <vector>

namespace eyra::correction
{

enum class Strength { Gentle = 0, Moderate, Aggressive };
enum class Mode { Transparent = 0, Polished };

struct Settings
{
    Instrument instrument = Instrument::General;
    Strength strength = Strength::Moderate;
    Mode mode = Mode::Polished;
    double amount = 1.0;          // 0..1.5, scales every gain
    int maxCorrections = 8;       // 1..12
    bool preserveCharacter = true;
    bool autoHighPass = true;     // also suggest a high-pass suited to the instrument (none for General)
    bool applyAutomatically = false; // apply as soon as an analysis finishes (otherwise: suggestions)

    juce::ValueTree toValueTree() const;
    static Settings fromValueTree (const juce::ValueTree&);
    bool operator== (const Settings&) const = default;
};

juce::StringArray strengthNames();
juce::StringArray modeNames();

/**
    One proposed EQ move, or a finding that was deliberately left alone.
    Every suggestion with a band is a real filter that becomes a normal, editable EQ band when applied.
*/
struct Suggestion
{
    enum class Kind { Cut, HighPass, Enhancement, LeftAlone };

    Kind kind = Kind::Cut;
    std::optional<dsp::BandParams> band; // empty for LeftAlone
    int findingIndex = -1;               // index into AnalysisResult::findings, -1 if none
    juce::String heading;                // for suggestions without a finding, e.g. "Boxiness (cardboard)"
    juce::String title;                  // e.g. "Cut 3.5 dB at 824 Hz"
    juce::String reason;                 // why, in plain language
    double priority = 0.0;               // higher first
};

/**
    The measured long-term spectrum compared with the instrument's typical tonal balance, on a
    1/12-octave grid. Levels are per 1/3 octave (pink noise is flat), and the target is shifted to the
    measured level (median difference), so only the shape matters.
*/
struct TargetComparison
{
    bool valid = false;
    std::vector<double> frequencies, measuredDb, targetDb, deviationDb; // deviation smoothed over 1/3 octave
    double toleranceDb = 0.0;
    /** Main low peak: measured, and where the typical curve had it (equal if not followed). */
    double measuredPeakHz = 0.0, typicalPeakHz = 0.0;
};

TargetComparison compareToTarget (const analysis::AnalysisResult&, Instrument, Strength);

/** Allowed deviation from the target before a correction is made (dB). */
double targetTolerance (Strength);

/** Turns analysis results into EQ suggestions for the chosen instrument. Pure function, any thread. */
std::vector<Suggestion> generateCorrections (const analysis::AnalysisResult&, const Settings&);

/** "Cut 3.5 dB at 824 Hz, Q 8" etc. */
juce::String describeBand (const dsp::BandParams&);

} // namespace eyra::correction
