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
        case Instrument::MaleVocal: case Instrument::FemaleVocal: case Instrument::Vocal:
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

namespace
{
    using S = Enhancement::Shape;

    Enhancement bell (double hz, double db, double q, const char* what) { return { S::Bell, hz, db, q, what }; }
    Enhancement air (double hz, double db, const char* what = "air") { return { S::HighShelf, hz, db, 0.7, what }; }

    std::array<InstrumentProfile, (size_t) Instrument::NumInstruments> makeProfiles()
    {
        std::array<InstrumentProfile, (size_t) Instrument::NumInstruments> p;
        auto set = [&] (Instrument i, double hpf, bool fundamental, std::vector<CharacterRegion> character, std::vector<Enhancement> polish)
        {
            p[(size_t) i] = { i, hpf, fundamental, std::move (character), std::move (polish) };
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
