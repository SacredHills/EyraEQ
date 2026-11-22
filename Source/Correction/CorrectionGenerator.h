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
    Mode mode = Mode::Transparent;
    double amount = 1.0;          // 0..1.5, scales every gain
    int maxCorrections = 6;       // 1..12
    bool preserveCharacter = true;
    bool autoHighPass = false;    // also suggest a high-pass when no rumble was measured
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
    juce::String title;                  // e.g. "Cut 3.5 dB at 824 Hz"
    juce::String reason;                 // why, in plain language
    double priority = 0.0;               // higher first
};

/** Turns analysis results into EQ suggestions for the chosen instrument. Pure function, any thread. */
std::vector<Suggestion> generateCorrections (const analysis::AnalysisResult&, const Settings&);

/** "Cut 3.5 dB at 824 Hz, Q 8" etc. */
juce::String describeBand (const dsp::BandParams&);

} // namespace eyra::correction
