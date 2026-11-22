#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace eyra::intelligence
{

/** Instruments EyraEQ knows about. The order is the order of the manual selection menu; append only. */
enum class Instrument : int
{
    Unknown = 0,
    KickDrum,
    SnareDrum,
    RackTom,
    FloorTom,
    Toms,
    Cymbals,
    Overheads,
    Percussion,
    ElectricBass,
    AcousticBass,
    SynthBass,
    ElectricGuitar,
    CleanGuitar,
    DistortedGuitar,
    AcousticGuitar,
    MaleVocal,
    FemaleVocal,
    Vocal,
    Piano,
    Organ,
    Synthesizer,
    Strings,
    Brass,
    AcousticInstrument,
    Drums,              // drums / percussion, piece not determined
    NumInstruments
};

juce::String instrumentName (Instrument);

/** Base instrument for a taxonomy category id ("kick", "toms", "electric_guitar", ...). */
Instrument instrumentForCategory (const juce::String& categoryId);

struct InstrumentCandidate
{
    Instrument instrument = Instrument::Unknown;
    juce::String categoryId;
    double score = 0.0;   // category evidence, 0..1 (mean of the strongest weighted class score)
    juce::String note;    // how a refinement was decided, e.g. "pitch 142 Hz"
};

struct InstrumentDetection
{
    enum class Status
    {
        NotRun,
        Unavailable,     // runtime or model missing / invalid
        NeedMoreAudio,
        Unknown,         // nothing had enough evidence
        Ambiguous,       // several plausible candidates
        Detected
    };

    enum class Confidence
    {
        Low,
        Medium,
        High
    };

    Status status = Status::NotRun;
    Confidence confidence = Confidence::Low;
    juce::String message;

    /** Detected: the instrument first, then runners-up. Ambiguous: the plausible candidates. */
    std::vector<InstrumentCandidate> candidates;

    /** What the model heard: the strongest raw labels (mean score over active audio). */
    std::vector<std::pair<juce::String, double>> topLabels;

    double activeSeconds = 0.0;
    juce::String model; // e.g. "yamnet 1.0.0"

    Instrument instrument() const noexcept
    {
        return status == Status::Detected && ! candidates.empty() ? candidates.front().instrument : Instrument::Unknown;
    }
};

juce::String describe (const InstrumentDetection&);

} // namespace eyra::intelligence
