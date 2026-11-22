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
    /** Highest high-pass that is normally safe (Hz); 0 = do not suggest one unless rumble is measured. */
    double highPassHz = 0.0;
    /** Use the measured pitch to protect the fundamental region (pitched instruments, toms). */
    bool protectFundamental = false;
    std::vector<CharacterRegion> character;
    std::vector<Enhancement> polish;
};

const InstrumentProfile& profileFor (Instrument);

} // namespace eyra::correction
