#include "Instrument.h"

#include <array>

namespace eyra::correction
{

juce::String instrumentName (Instrument i)
{
    switch (i)
    {
        case Instrument::General:            return "General (no instrument)";
        case Instrument::KickDrum:           return "Kick drum";
        case Instrument::SnareDrum:          return "Snare drum";
        case Instrument::RackTom:            return "Rack tom";
        case Instrument::FloorTom:           return "Floor tom";
        case Instrument::Toms:               return "Toms";
        case Instrument::Cymbals:            return "Cymbals / hi-hat";
        case Instrument::Overheads:          return "Overheads / drum room";
        case Instrument::Percussion:         return "Percussion";
        case Instrument::ElectricBass:       return "Electric bass";
        case Instrument::AcousticBass:       return "Acoustic bass";
        case Instrument::SynthBass:          return "Synth bass";
        case Instrument::ElectricGuitar:     return "Electric guitar";
        case Instrument::CleanGuitar:        return "Clean electric guitar";
        case Instrument::DistortedGuitar:    return "Distorted guitar";
        case Instrument::AcousticGuitar:     return "Acoustic guitar";
        case Instrument::MaleVocal:          return "Male vocal";
        case Instrument::FemaleVocal:        return "Female vocal";
        case Instrument::Vocal:              return "Vocal";
        case Instrument::Piano:              return "Piano";
        case Instrument::Organ:              return "Organ";
        case Instrument::Synthesizer:        return "Synthesizer";
        case Instrument::Strings:            return "Strings";
        case Instrument::Brass:              return "Brass";
        case Instrument::AcousticInstrument: return "Other acoustic instrument";
        case Instrument::ScreamingVocal:     return "Screaming vocal";
        case Instrument::NumInstruments:     break;
    }
    return "General (no instrument)";
}

juce::String instrumentGroup (Instrument i)
{
    switch (i)
    {
        case Instrument::KickDrum: case Instrument::SnareDrum: case Instrument::RackTom: case Instrument::FloorTom:
        case Instrument::Toms: case Instrument::Cymbals: case Instrument::Overheads: case Instrument::Percussion:
            return "Drums";
        case Instrument::ElectricBass: case Instrument::AcousticBass: case Instrument::SynthBass:
            return "Bass";
        case Instrument::ElectricGuitar: case Instrument::CleanGuitar: case Instrument::DistortedGuitar: case Instrument::AcousticGuitar:
            return "Guitar";
        case Instrument::MaleVocal: case Instrument::FemaleVocal: case Instrument::Vocal: case Instrument::ScreamingVocal:
            return "Vocals";
        case Instrument::Piano: case Instrument::Organ: case Instrument::Synthesizer:
            return "Keys";
        case Instrument::Strings: case Instrument::Brass: case Instrument::AcousticInstrument:
            return "Other";
        case Instrument::General: case Instrument::NumInstruments:
            break;
    }
    return {};
}

juce::StringArray instrumentGroups() { return { "Drums", "Bass", "Guitar", "Vocals", "Keys", "Other" }; }

namespace
{
    using S = Enhancement::Shape;

    Enhancement bell (double hz, double db, double q, const char* what) { return { S::Bell, hz, db, q, what }; }
    Enhancement air (double hz, double db, const char* what = "air") { return { S::HighShelf, hz, db, 0.7, what }; }


    using Points = std::vector<std::pair<double, double>>;

    void target (std::array<InstrumentProfile, (size_t) Instrument::NumInstruments>& p, Instrument i, double lo, double hi,
                 Points points, std::vector<CharacterRegion> zones)
    {
        auto& profile = p[(size_t) i];
        profile.target = std::move (points);
        profile.fitLowHz = lo;
        profile.fitHighHz = hi;
        profile.zones = std::move (zones);
    }

    void addTargets (std::array<InstrumentProfile, (size_t) Instrument::NumInstruments>& p)
    {
        using I = Instrument;
        // Drums
        target (p, I::KickDrum, 30, 10000,
                { { 25, -6 }, { 40, -1 }, { 55, 0 }, { 70, 0 }, { 90, -2 }, { 120, -6 }, { 180, -11 }, { 300, -15 }, { 450, -17 },
                  { 700, -18 }, { 1000, -18 }, { 1500, -17 }, { 2500, -15 }, { 4000, -15 }, { 6000, -19 }, { 10000, -26 }, { 16000, -34 } },
                { { 20, 45, "sub boom" }, { 120, 220, "low-mid thickness" }, { 220, 700, "boxiness (cardboard)" }, { 700, 1800, "honk" },
                  { 1800, 5000, "beater click" }, { 5000, 20000, "hiss and bleed" } });
        target (p, I::SnareDrum, 80, 14000,
                { { 60, -18 }, { 100, -10 }, { 150, -3 }, { 200, 0 }, { 250, -1 }, { 400, -5 }, { 600, -7 }, { 800, -8 }, { 1000, -8 },
                  { 2000, -8 }, { 3000, -8 }, { 5000, -9 }, { 8000, -12 }, { 12000, -17 }, { 16000, -24 } },
                { { 20, 120, "kick bleed and boom" }, { 120, 300, "body" }, { 300, 1000, "boxiness and ring" }, { 1000, 2000, "honk" },
                  { 2000, 6000, "crack" }, { 6000, 20000, "sizzle" } });
        target (p, I::RackTom, 70, 10000,
                { { 50, -20 }, { 80, -10 }, { 120, -3 }, { 160, 0 }, { 220, -2 }, { 300, -6 }, { 500, -12 }, { 800, -15 }, { 1500, -17 },
                  { 3000, -17 }, { 5000, -19 }, { 8000, -24 }, { 12000, -30 } },
                { { 20, 80, "boom and kick bleed" }, { 80, 250, "body" }, { 250, 900, "boxiness" }, { 900, 2000, "honk" },
                  { 2000, 6000, "stick attack" }, { 6000, 20000, "cymbal bleed" } });
        target (p, I::FloorTom, 40, 10000,
                { { 35, -16 }, { 60, -4 }, { 90, 0 }, { 120, -1 }, { 180, -6 }, { 300, -11 }, { 500, -14 }, { 800, -16 }, { 1500, -18 },
                  { 3000, -18 }, { 5000, -20 }, { 8000, -25 }, { 12000, -31 } },
                { { 20, 50, "sub boom" }, { 50, 160, "body" }, { 160, 700, "boxiness" }, { 700, 2000, "honk" },
                  { 2000, 6000, "stick attack" }, { 6000, 20000, "cymbal bleed" } });
        target (p, I::Toms, 50, 10000,
                { { 40, -16 }, { 70, -5 }, { 110, 0 }, { 150, 0 }, { 220, -4 }, { 300, -8 }, { 500, -13 }, { 800, -15 }, { 1500, -17 },
                  { 3000, -17 }, { 5000, -19 }, { 8000, -24 }, { 12000, -30 } },
                { { 20, 60, "sub boom" }, { 60, 220, "body" }, { 220, 800, "boxiness" }, { 800, 2000, "honk" },
                  { 2000, 6000, "stick attack" }, { 6000, 20000, "cymbal bleed" } });
        target (p, I::Cymbals, 300, 18000,
                { { 200, -30 }, { 400, -22 }, { 800, -14 }, { 1500, -8 }, { 3000, -3 }, { 5000, 0 }, { 8000, 0 }, { 12000, -2 }, { 16000, -6 } },
                { { 20, 400, "drum bleed" }, { 400, 1500, "clang" }, { 1500, 5000, "harshness" }, { 5000, 10000, "shimmer" },
                  { 10000, 20000, "air" } });
        target (p, I::Overheads, 40, 16000,
                { { 50, -10 }, { 100, -6 }, { 200, -5 }, { 400, -7 }, { 800, -8 }, { 1500, -8 }, { 3000, -7 }, { 6000, -7 }, { 10000, -9 },
                  { 15000, -14 } },
                { { 20, 120, "low end" }, { 120, 350, "mud" }, { 350, 1000, "boxiness (room)" }, { 1000, 2500, "honk" },
                  { 2500, 6000, "harshness" }, { 6000, 20000, "cymbal sizzle" } });
        target (p, I::Percussion, 80, 16000,
                { { 100, -14 }, { 200, -8 }, { 400, -4 }, { 800, -2 }, { 1500, 0 }, { 3000, 0 }, { 6000, -2 }, { 10000, -6 }, { 15000, -12 } },
                { { 20, 200, "low end" }, { 200, 600, "mud" }, { 600, 2000, "honk" }, { 2000, 6000, "attack" }, { 6000, 20000, "air" } });

        // Bass
        target (p, I::ElectricBass, 35, 5000,
                { { 30, -8 }, { 40, -3 }, { 60, 0 }, { 100, 0 }, { 150, -2 }, { 250, -6 }, { 400, -10 }, { 700, -13 }, { 1000, -14 },
                  { 2000, -18 }, { 4000, -24 }, { 8000, -34 } },
                { { 20, 40, "sub rumble" }, { 40, 150, "fundamentals" }, { 150, 400, "mud" }, { 400, 1200, "growl" },
                  { 1200, 4000, "string click" }, { 4000, 20000, "fret noise and hiss" } });
        target (p, I::AcousticBass, 35, 6000,
                { { 40, -6 }, { 60, -1 }, { 90, 0 }, { 150, -2 }, { 250, -6 }, { 400, -10 }, { 800, -14 }, { 1500, -17 }, { 3000, -22 },
                  { 6000, -30 } },
                { { 20, 45, "sub rumble" }, { 45, 180, "fundamentals" }, { 180, 450, "boom and mud" }, { 450, 1200, "woodiness" },
                  { 1200, 5000, "finger attack" }, { 5000, 20000, "noise" } });
        target (p, I::SynthBass, 25, 6000,
                { { 25, -6 }, { 35, -1 }, { 50, 0 }, { 80, 0 }, { 150, -4 }, { 300, -10 }, { 600, -15 }, { 1200, -20 }, { 3000, -27 },
                  { 6000, -34 } },
                { { 20, 30, "sub rumble" }, { 30, 150, "fundamentals" }, { 150, 500, "mud" }, { 500, 2000, "growl" },
                  { 2000, 20000, "fizz" } });

        // Guitars
        target (p, I::ElectricGuitar, 80, 8000,
                { { 60, -20 }, { 90, -10 }, { 150, -4 }, { 250, -1 }, { 400, 0 }, { 800, 0 }, { 1500, -1 }, { 2500, -2 }, { 4000, -6 },
                  { 6000, -12 }, { 10000, -24 } },
                { { 20, 100, "boom" }, { 100, 300, "mud" }, { 300, 1000, "body and boxiness" }, { 1000, 2500, "honk" },
                  { 2500, 5000, "bite and harshness" }, { 5000, 20000, "fizz" } });
        target (p, I::CleanGuitar, 80, 12000,
                { { 60, -20 }, { 100, -9 }, { 200, -3 }, { 400, -1 }, { 800, -1 }, { 1500, -1 }, { 3000, -3 }, { 5000, -6 }, { 8000, -11 },
                  { 12000, -18 } },
                { { 20, 100, "boom" }, { 100, 300, "mud" }, { 300, 1000, "boxiness" }, { 1000, 2500, "honk" },
                  { 2500, 6000, "clarity and harshness" }, { 6000, 20000, "sparkle" } });
        target (p, I::DistortedGuitar, 80, 8000,
                { { 60, -22 }, { 90, -12 }, { 150, -5 }, { 250, -2 }, { 400, -1 }, { 800, -1 }, { 1500, 0 }, { 2500, 0 }, { 4000, -4 },
                  { 6000, -12 }, { 9000, -22 } },
                { { 20, 100, "boom" }, { 100, 300, "mud and chug" }, { 300, 900, "boxiness" }, { 900, 2000, "honk" },
                  { 2000, 5000, "bite and harshness" }, { 5000, 20000, "fizz" } });
        target (p, I::AcousticGuitar, 70, 15000,
                { { 60, -16 }, { 100, -6 }, { 150, -2 }, { 250, -2 }, { 400, -4 }, { 800, -6 }, { 1500, -7 }, { 3000, -7 }, { 6000, -9 },
                  { 10000, -13 }, { 15000, -20 } },
                { { 20, 120, "boom" }, { 120, 300, "body and mud" }, { 300, 900, "boxiness" }, { 900, 2000, "honk" },
                  { 2000, 6000, "string detail" }, { 6000, 20000, "sparkle" } });

        // Vocals
        target (p, I::MaleVocal, 90, 14000,
                { { 60, -24 }, { 100, -10 }, { 150, -2 }, { 250, 0 }, { 400, -2 }, { 700, -5 }, { 1200, -8 }, { 2500, -9 }, { 4000, -10 },
                  { 6000, -13 }, { 9000, -17 }, { 13000, -22 } },
                { { 20, 90, "rumble and plosives" }, { 90, 200, "chest" }, { 200, 500, "mud" }, { 500, 1500, "nasal honk" },
                  { 1500, 5000, "presence and harshness" }, { 5000, 10000, "sibilance" }, { 10000, 20000, "air" } });
        target (p, I::FemaleVocal, 120, 14000,
                { { 100, -20 }, { 160, -8 }, { 250, -1 }, { 400, 0 }, { 700, -3 }, { 1200, -6 }, { 2500, -8 }, { 4000, -9 }, { 6000, -12 },
                  { 9000, -16 }, { 13000, -21 } },
                { { 20, 120, "rumble and plosives" }, { 120, 300, "chest" }, { 300, 700, "mud" }, { 700, 1800, "nasal honk" },
                  { 1800, 5500, "presence and harshness" }, { 5500, 11000, "sibilance" }, { 11000, 20000, "air" } });
        target (p, I::Vocal, 100, 14000,
                { { 70, -22 }, { 120, -9 }, { 200, -2 }, { 320, 0 }, { 500, -2 }, { 900, -5 }, { 1500, -7 }, { 2500, -8 }, { 4000, -9 },
                  { 6000, -12 }, { 9000, -16 }, { 13000, -21 } },
                { { 20, 100, "rumble and plosives" }, { 100, 250, "chest" }, { 250, 600, "mud" }, { 600, 1600, "nasal honk" },
                  { 1600, 5000, "presence and harshness" }, { 5000, 10000, "sibilance" }, { 10000, 20000, "air" } });

        // Screamed vocals: energy sits in the upper mids (grit), little chest, a lot of 2-5 kHz edge.
        target (p, I::ScreamingVocal, 120, 14000,
                { { 100, -22 }, { 150, -12 }, { 250, -5 }, { 400, -2 }, { 700, 0 }, { 1200, 0 }, { 2000, 0 }, { 3000, -1 }, { 4500, -4 },
                  { 6500, -8 }, { 9000, -13 }, { 13000, -19 } },
                { { 20, 120, "rumble and plosives" }, { 120, 300, "chest" }, { 300, 700, "boxiness" }, { 700, 2000, "aggression" },
                  { 2000, 5000, "harshness (ice pick)" }, { 5000, 10000, "fizz and sibilance" }, { 10000, 20000, "air" } });

        // Keys and others
        target (p, I::Piano, 30, 13000,
                { { 30, -14 }, { 60, -5 }, { 120, -1 }, { 250, 0 }, { 500, 0 }, { 1000, -3 }, { 2000, -7 }, { 4000, -11 }, { 8000, -17 },
                  { 13000, -24 } },
                { { 20, 60, "rumble" }, { 60, 200, "low end" }, { 200, 600, "mud" }, { 600, 1500, "honk" },
                  { 1500, 5000, "hammer and harshness" }, { 5000, 20000, "brightness" } });
        target (p, I::Organ, 40, 10000,
                { { 40, -8 }, { 80, -2 }, { 150, 0 }, { 300, 0 }, { 600, -2 }, { 1200, -6 }, { 2500, -11 }, { 5000, -17 }, { 10000, -25 } },
                { { 20, 80, "low end" }, { 80, 250, "mud" }, { 250, 800, "boxiness" }, { 800, 2000, "honk" }, { 2000, 20000, "brightness" } });
        target (p, I::Strings, 40, 14000,
                { { 40, -18 }, { 80, -8 }, { 150, -3 }, { 300, -1 }, { 600, 0 }, { 1200, -2 }, { 2500, -6 }, { 5000, -10 }, { 9000, -16 },
                  { 14000, -24 } },
                { { 20, 80, "rumble" }, { 80, 250, "low end" }, { 250, 600, "mud" }, { 600, 1500, "nasal honk" },
                  { 1500, 5000, "bow scratch and harshness" }, { 5000, 20000, "air" } });
        target (p, I::Brass, 60, 12000,
                { { 60, -20 }, { 100, -12 }, { 200, -5 }, { 400, -1 }, { 800, 0 }, { 1500, 0 }, { 2500, -3 }, { 4000, -7 }, { 7000, -13 },
                  { 12000, -22 } },
                { { 20, 120, "rumble" }, { 120, 350, "mud" }, { 350, 900, "boxiness" }, { 900, 2000, "honk" },
                  { 2000, 6000, "blare and harshness" }, { 6000, 20000, "air" } });
    }


    void addMoves (std::array<InstrumentProfile, (size_t) Instrument::NumInstruments>& p)
    {
        using I = Instrument;
        using M = TypicalMove;
        const auto bell = M::Shape::Bell, shelf = M::Shape::HighShelf, peak = M::Shape::AtMainPeak;
        auto set = [&] (I i, std::vector<TypicalMove> m) { p[(size_t) i].moves = std::move (m); };

        set (I::KickDrum, { { bell, 250, 600, -3.0, 1.4, "boxiness" }, { bell, 45, 90, 2.0, 1.2, "low-end punch" },
                            { bell, 2500, 5000, 2.5, 1.5, "beater click" } });
        set (I::SnareDrum, { { bell, 150, 260, 2.0, 1.4, "body" }, { bell, 400, 900, -3.0, 1.6, "boxiness and ring" },
                             { bell, 4000, 6500, 2.0, 1.0, "crack" } });
        const std::vector<TypicalMove> tom { { peak, 0, 0, 1.5, 1.5, "body (the drum's note)" }, { bell, 280, 700, -3.5, 1.5, "boxiness" },
                                             { bell, 3000, 5500, 2.5, 1.2, "stick attack" } };
        set (I::RackTom, tom);
        set (I::FloorTom, tom);
        set (I::Toms, tom);
        set (I::Cymbals, { { bell, 2000, 4500, -1.5, 1.2, "harshness" }, { shelf, 10000, 10000, 1.5, 0.7, "air" } });
        set (I::Overheads, { { bell, 300, 600, -2.0, 1.0, "boxiness (room)" }, { shelf, 10000, 10000, 1.5, 0.7, "air" } });
        set (I::Percussion, { { bell, 300, 600, -2.0, 1.2, "boxiness" }, { shelf, 10000, 10000, 1.5, 0.7, "air" } });

        set (I::ElectricBass, { { bell, 200, 400, -3.0, 1.2, "mud" }, { bell, 60, 100, 1.5, 1.0, "weight" },
                                { bell, 700, 1200, 2.0, 1.4, "note definition" } });
        set (I::AcousticBass, { { bell, 200, 400, -2.5, 1.2, "mud and boom" }, { bell, 1500, 3000, 1.5, 1.2, "finger attack" } });
        set (I::SynthBass, { { bell, 200, 400, -2.5, 1.2, "mud" } });

        set (I::ElectricGuitar, { { bell, 200, 400, -2.5, 1.2, "mud" }, { bell, 2500, 4000, 1.5, 1.0, "presence" } });
        set (I::CleanGuitar, { { bell, 200, 400, -2.0, 1.2, "mud" }, { bell, 3000, 5000, 1.5, 1.0, "clarity" },
                               { shelf, 10000, 10000, 1.0, 0.7, "air" } });
        set (I::DistortedGuitar, { { bell, 200, 400, -2.5, 1.2, "mud" }, { bell, 3000, 5000, -2.0, 2.0, "fizz and harshness" },
                                   { bell, 1400, 2500, 1.5, 1.2, "mid push" } });
        set (I::AcousticGuitar, { { bell, 150, 300, -3.0, 1.2, "boom" }, { bell, 600, 1000, -1.5, 1.4, "boxiness" },
                                  { shelf, 10000, 10000, 2.0, 0.7, "sparkle" } });

        set (I::MaleVocal, { { bell, 200, 400, -3.0, 1.2, "mud" }, { bell, 2500, 4000, 2.0, 1.0, "presence" },
                             { shelf, 10000, 10000, 2.0, 0.7, "air" } });
        set (I::FemaleVocal, { { bell, 250, 500, -2.5, 1.2, "mud" }, { bell, 3000, 5000, 2.0, 1.0, "presence" },
                               { shelf, 11000, 11000, 2.0, 0.7, "air" } });
        set (I::ScreamingVocal, { { bell, 300, 600, -3.0, 1.2, "boxiness" }, { bell, 1000, 2000, 1.5, 1.0, "aggression and cut-through" },
                                  { bell, 2500, 5000, -2.5, 2.0, "harshness (ice pick)" }, { bell, 5500, 9000, -2.0, 2.0, "fizz" },
                                  { shelf, 11000, 11000, 1.0, 0.7, "air" } });
        set (I::Vocal, { { bell, 220, 450, -3.0, 1.2, "mud" }, { bell, 2500, 4500, 2.0, 1.0, "presence" },
                         { shelf, 10000, 10000, 2.0, 0.7, "air" } });

        set (I::Piano, { { bell, 200, 400, -2.0, 1.2, "mud" }, { shelf, 8000, 8000, 1.5, 0.7, "brightness" } });
        set (I::Organ, { { bell, 200, 500, -2.0, 1.2, "mud" } });
        set (I::Strings, { { bell, 250, 500, -2.0, 1.2, "mud" }, { shelf, 9000, 9000, 1.5, 0.7, "air" } });
        set (I::Brass, { { bell, 300, 600, -2.0, 1.2, "boxiness" }, { bell, 2000, 3000, 1.5, 1.2, "brilliance" } });
    }

    std::array<InstrumentProfile, (size_t) Instrument::NumInstruments> makeProfiles()
    {
        std::array<InstrumentProfile, (size_t) Instrument::NumInstruments> p;
        auto set = [&] (Instrument i, double hpf, bool fundamental, std::vector<CharacterRegion> character, std::vector<Enhancement> polish)
        {
            auto& profile = p[(size_t) i];
            profile.instrument = i;
            profile.highPassHz = hpf;
            profile.protectFundamental = fundamental;
            profile.character = std::move (character);
            profile.polish = std::move (polish);
        };

        set (Instrument::General, 0.0, false, {}, {});

        // Drums
        set (Instrument::KickDrum, 30.0, false,
             { { 40.0, 110.0, "kick body" }, { 2000.0, 5000.0, "beater click" } },
             { bell (3500.0, 1.5, 1.2, "beater definition") });
        set (Instrument::SnareDrum, 80.0, false,
             { { 150.0, 260.0, "snare body" }, { 2000.0, 6000.0, "snare crack and wires" } },
             { bell (5000.0, 1.5, 1.0, "crack"), air (10000.0, 1.0) });
        set (Instrument::RackTom, 60.0, true,
             { { 90.0, 300.0, "tom body" } },
             { bell (4000.0, 1.0, 1.0, "stick attack") });
        set (Instrument::FloorTom, 40.0, true,
             { { 60.0, 180.0, "tom body" } },
             { bell (4000.0, 1.0, 1.0, "stick attack") });
        set (Instrument::Toms, 40.0, true,
             { { 60.0, 300.0, "tom body" } },
             { bell (4000.0, 1.0, 1.0, "stick attack") });
        set (Instrument::Cymbals, 250.0, false,
             { { 3000.0, 16000.0, "cymbal shimmer" } },
             { air (12000.0, 1.0) });
        set (Instrument::Overheads, 60.0, false,
             { { 3000.0, 14000.0, "cymbals and kit detail" } },
             { air (11000.0, 1.0) });
        set (Instrument::Percussion, 80.0, true,
             { { 2000.0, 8000.0, "attack" } },
             { air (10000.0, 1.0) });

        // Bass
        set (Instrument::ElectricBass, 30.0, true,
             { { 40.0, 150.0, "bass fundamentals" }, { 600.0, 1200.0, "growl and note definition" } },
             { bell (900.0, 1.0, 1.2, "note definition") });
        set (Instrument::AcousticBass, 30.0, true,
             { { 40.0, 180.0, "bass fundamentals" } },
             { bell (1500.0, 1.0, 1.2, "finger definition") });
        set (Instrument::SynthBass, 25.0, true,
             { { 30.0, 150.0, "bass fundamentals" } },
             {});

        // Guitars
        set (Instrument::ElectricGuitar, 80.0, true,
             { { 1500.0, 4000.0, "pick attack and bite" } },
             { bell (3000.0, 1.0, 1.0, "presence") });
        set (Instrument::CleanGuitar, 80.0, true,
             { { 2000.0, 6000.0, "clarity" } },
             { bell (3000.0, 1.0, 1.0, "presence"), air (10000.0, 1.0) });
        set (Instrument::DistortedGuitar, 90.0, true,
             { { 1000.0, 3500.0, "distortion character" } },
             { bell (2500.0, 1.0, 1.0, "presence") });
        set (Instrument::AcousticGuitar, 70.0, true,
             { { 2000.0, 7000.0, "string detail" } },
             { air (10000.0, 1.5, "sparkle") });

        // Vocals
        set (Instrument::MaleVocal, 80.0, true,
             { { 2000.0, 5000.0, "presence and intelligibility" } },
             { bell (3000.0, 1.0, 1.0, "presence"), air (10000.0, 1.5) });
        set (Instrument::FemaleVocal, 100.0, true,
             { { 2000.0, 6000.0, "presence and intelligibility" } },
             { bell (3500.0, 1.0, 1.0, "presence"), air (11000.0, 1.5) });
        set (Instrument::Vocal, 90.0, true,
             { { 2000.0, 5000.0, "presence and intelligibility" } },
             { bell (3000.0, 1.0, 1.0, "presence"), air (10000.0, 1.5) });

        set (Instrument::ScreamingVocal, 110.0, true,
             { { 1000.0, 2500.0, "grit and aggression" } },
             {});

        // Keys
        set (Instrument::Piano, 30.0, true,
             { { 2000.0, 5000.0, "hammer attack" } },
             { air (8000.0, 1.0, "brightness") });
        set (Instrument::Organ, 35.0, true, {}, {});
        set (Instrument::Synthesizer, 30.0, true, {}, {});

        // Other
        set (Instrument::Strings, 40.0, true,
             { { 1500.0, 5000.0, "bow detail" } },
             { air (9000.0, 1.0) });
        set (Instrument::Brass, 70.0, true,
             { { 1000.0, 4000.0, "brilliance" } },
             { bell (2000.0, 1.0, 1.0, "brilliance") });
        set (Instrument::AcousticInstrument, 60.0, true,
             {},
             { air (10000.0, 1.0) });
        addTargets (p);
        addMoves (p);
        for (auto i : { Instrument::KickDrum, Instrument::SnareDrum, Instrument::RackTom, Instrument::FloorTom, Instrument::Toms,
                        Instrument::Cymbals, Instrument::Percussion })
            p[(size_t) i].drum = true;
        for (auto i : { Instrument::KickDrum, Instrument::SnareDrum, Instrument::RackTom, Instrument::FloorTom, Instrument::Toms,
                        Instrument::Percussion, Instrument::ElectricBass, Instrument::AcousticBass, Instrument::SynthBass,
                        Instrument::ElectricGuitar, Instrument::CleanGuitar, Instrument::DistortedGuitar, Instrument::AcousticGuitar,
                        Instrument::MaleVocal, Instrument::FemaleVocal, Instrument::Vocal, Instrument::ScreamingVocal })
            p[(size_t) i].followsFundamental = true;
        return p;
    }
} // namespace

const InstrumentProfile& profileFor (Instrument i)
{
    static const auto profiles = makeProfiles();
    const auto index = (size_t) juce::jlimit (0, (int) Instrument::NumInstruments - 1, (int) i);
    return profiles[index];
}

} // namespace eyra::correction
