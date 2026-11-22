#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace eyra::correction
{

/** Instruments the user can choose. The order is the order of the menu and of saved sessions; append only. */
enum class Instrument : int
{
    General = 0,   // no instrument chosen: corrections use the measurements alone
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
    NumInstruments
};

juce::String instrumentName (Instrument);

/** Menu section an instrument belongs to ("Drums", "Bass", ...); empty for General. */
juce::String instrumentGroup (Instrument);

/** A frequency region that belongs to the instrument's sound. */
struct CharacterRegion
{
    double lowHz = 0.0, highHz = 0.0;
    juce::String what; // e.g. "kick body"
};

/** A subtle tonal enhancement offered in Polished mode, only where the recording lacks it. */
struct Enhancement
{
    enum class Shape { Bell, HighShelf };
    Shape shape = Shape::Bell;
    double frequency = 1000.0;
    double gainDb = 1.0;     // at Moderate strength, 100% amount
    double q = 1.0;
    juce::String what;       // e.g. "beater definition"
};

/**
    What EyraEQ knows about an instrument. Character regions are where an instrument naturally has a
    lot of energy: a broad excess there is treated with more care (less cut, higher threshold) instead
    of being assumed to be a problem.
*/
struct InstrumentProfile
{
    Instrument instrument = Instrument::General;

    /**
        Typical tonal balance of a well-recorded, mix-ready instance of the instrument: relative level
        (dB) at control frequencies, interpolated on a log-frequency axis and compared with the measured
        long-term spectrum (both 1/6-octave smoothed). Empty for sources too varied to have one
        (synthesizer, other acoustic, General): those use the generic analysis only.
        These are engineering rules of thumb, not measurements: see Documentation/Correction.md.
    */
    std::vector<std::pair<double, double>> target;
    double fitLowHz = 0.0, fitHighHz = 0.0;   // range compared with the target
    /** Names for parts of the spectrum ("boxiness", "sibilance"...), used to label corrections. */
    std::vector<CharacterRegion> zones;

    /** Highest high-pass that is normally safe (Hz); 0 = do not suggest one unless rumble is measured. */
    double highPassHz = 0.0;
    /** Use the measured pitch to protect the fundamental region (pitched instruments, toms). */
    bool protectFundamental = false;
    /** Drums and cymbals: their heads and shells ring at their own (inharmonic) frequencies, which
        are part of the tone, so steady narrow peaks are not cut; only excessive ringing is. */
    bool drum = false;
    std::vector<CharacterRegion> character;
    std::vector<Enhancement> polish;
};

const InstrumentProfile& profileFor (Instrument);

} // namespace eyra::correction
