#include "Correction/CorrectionGenerator.h"
#include "PluginProcessor.h"
#include "TestHelpers.h"

#include <numbers>

using namespace eyra;
using namespace eyra::correction;
using analysis::AnalysisResult;
using analysis::Finding;
using analysis::FindingType;

namespace
{
using K = Suggestion::Kind;

AnalysisResult okResult()
{
    AnalysisResult r;
    r.status = AnalysisResult::Status::Ok;
    r.activeSeconds = 10.0;
    r.confidence = 1.0;
    r.lowestContentHz = 30.0;
    r.highestContentHz = 18000.0;
    return r;
}

Finding narrow (FindingType type, double hz, double excess, double q, double severity = 0.7)
{
    Finding f;
    f.type = type;
    f.title = "narrow";
    f.frequency = f.lowFrequency = f.highFrequency = hz;
    f.excessDb = excess;
    f.q = q;
    f.severity = severity;
    f.confidence = 1.0;
    return f;
}

Finding broad (double lo, double peak, double hi, double excess, double severity = 0.6)
{
    Finding f;
    f.type = FindingType::Buildup;
    f.title = "buildup";
    f.lowFrequency = lo;
    f.frequency = peak;
    f.highFrequency = hi;
    f.excessDb = excess;
    f.severity = severity;
    f.confidence = 1.0;
    return f;
}

std::vector<Suggestion> withBands (const std::vector<Suggestion>& all)
{
    std::vector<Suggestion> out;
    for (const auto& s : all)
        if (s.band)
            out.push_back (s);
    return out;
}

const Suggestion* forFinding (const std::vector<Suggestion>& all, int index)
{
    for (const auto& s : all)
        if (s.findingIndex == index)
            return &s;
    return nullptr;
}

double cutAt (const std::vector<Suggestion>& all, int index)
{
    const auto* s = forFinding (all, index);
    return s != nullptr && s->band ? s->band->gainDb : 0.0;
}

/** Band-limited pink noise at roughly -20 dBFS RMS. */
std::vector<float> pinkNoise (double sr, double seconds, int seed = 1)
{
    juce::Random rng (seed);
    std::vector<float> x ((size_t) (sr * seconds));
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (auto& v : x)
    {
        const double w = rng.nextFloat() * 2.0 - 1.0;
        b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520; b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
        v = (float) ((b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362) * 0.03);
        b6 = w * 0.115926;
    }
    return x;
}

void filter (std::vector<float>& x, const test::FilterSpec& spec, double sr) { test::applyFilter (x, spec, sr); }

std::vector<float> processThrough (EyraEQAudioProcessor& p, const std::vector<float>& in)
{
    std::vector<float> out (in.size());
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos + 512 <= in.size(); pos += 512)
    {
        for (int i = 0; i < 512; ++i)
            buf.setSample (0, i, in[pos + (size_t) i]), buf.setSample (1, i, in[pos + (size_t) i]);
        p.processBlock (buf, midi);
        for (int i = 0; i < 512; ++i)
            out[pos + (size_t) i] = buf.getSample (0, i);
    }
    return out;
}

const Finding* findNear (const AnalysisResult& r, FindingType type, double hz)
{
    for (const auto& f : r.findings)
        if (f.type == type && std::abs (std::log2 (f.frequency / hz)) < 1.0 / 12.0)
            return &f;
    return nullptr;
}

int usedBands (EyraEQAudioProcessor& p)
{
    int n = 0;
    for (int b = 0; b < params::kNumBands; ++b)
        n += p.apvts.getRawParameterValue (params::bandParamID (b, params::BandField::Used))->load() >= 0.5f ? 1 : 0;
    return n;
}


/** A result whose measured spectrum is the instrument's typical balance, plus optional bumps (dB over an octave). */
AnalysisResult resultShapedLike (Instrument instrument, std::vector<std::pair<double, double>> bumps = {}, double frequencyScale = 1.0)
{
    auto r = okResult();
    r.sampleRate = 48000.0;
    const auto& P = profileFor (instrument).target;
    auto targetAt = [&] (double f)
    {
        if (f <= P.front().first) return P.front().second;
        if (f >= P.back().first) return P.back().second;
        for (size_t i = 1; i < P.size(); ++i)
            if (f <= P[i].first)
                return P[i - 1].second + std::log (f / P[i - 1].first) / std::log (P[i].first / P[i - 1].first) * (P[i].second - P[i - 1].second);
        return P.back().second;
    };
    for (int i = 0; i < 512; ++i)
    {
        const double f = 10.0 * std::pow (3000.0, i / 511.0);
        double db = targetAt (f / frequencyScale) - 10.0 * std::log10 (f / 1000.0) - 40.0;
        for (auto [centre, gain] : bumps)
        {
            const double oct = std::abs (std::log2 (f / centre));
            if (oct < 0.5)
                db += gain * std::cos (oct * juce::MathConstants<double>::pi); // smooth one-octave bump
        }
        r.displayFrequencies.push_back (f);
        r.ltasDb.push_back ((float) db);
    }
    return r;
}

class CorrectionTests final : public juce::UnitTest
{
public:
    CorrectionTests() : juce::UnitTest ("Corrective EQ", "Correction") {}

    void runTest() override
    {
        beginTest ("Narrow problems become narrow cuts, scaled by strength and amount");
        {
            auto r = okResult();
            r.findings = { narrow (FindingType::Resonance, 824.0, 8.0, 12.0) };
            Settings s;
            auto moderate = generateCorrections (r, s);
            const auto* sg = forFinding (moderate, 0);
            expect (sg != nullptr && sg->band.has_value() && sg->kind == K::Cut);
            if (sg != nullptr && sg->band)
            {
                expect (sg->band->type == dsp::FilterType::Bell);
                expectWithinAbsoluteError (sg->band->frequency, 824.0, 0.01);
                expectWithinAbsoluteError (sg->band->q, 12.0, 0.01);
                expect (sg->band->gainDb < -3.0 && sg->band->gainDb > -8.0, juce::String (sg->band->gainDb));
                expect (sg->title.startsWith ("Cut"), sg->title);
            }

            s.strength = Strength::Gentle;
            const double gentle = cutAt (generateCorrections (r, s), 0);
            s.strength = Strength::Aggressive;
            const double aggressive = cutAt (generateCorrections (r, s), 0);
            expect (gentle > cutAt (moderate, 0) && cutAt (moderate, 0) > aggressive, "gentle < moderate < aggressive");

            s.strength = Strength::Moderate;
            s.amount = 0.5;
            expectWithinAbsoluteError (cutAt (generateCorrections (r, s), 0), 0.5 * cutAt (moderate, 0), 0.01);
            s.amount = 0.0;
            expect (withBands (generateCorrections (r, s)).empty(), "amount 0: nothing is applied");

            // A huge excess is capped.
            r.findings = { narrow (FindingType::Resonance, 824.0, 40.0, 12.0) };
            s.amount = 1.0;
            s.strength = Strength::Aggressive;
            expect (cutAt (generateCorrections (r, s), 0) >= -12.0);
        }

        beginTest ("Hum, ringing, intermittent resonance");
        {
            auto r = okResult();
            r.findings = { narrow (FindingType::Hum, 60.0, 6.0, 4.0), narrow (FindingType::Ringing, 1130.0, 6.0, 30.0),
                           narrow (FindingType::IntermittentResonance, 2880.0, 8.0, 12.0), narrow (FindingType::Resonance, 4000.0, 8.0, 12.0) };
            const auto all = generateCorrections (r, {});
            expect (forFinding (all, 0)->band->q >= 10.0, "hum: narrow");
            expect (forFinding (all, 1)->band->q <= 10.0, "ringing: Q limited");
            expect (cutAt (all, 2) > cutAt (all, 3), "intermittent: lighter than a constant resonance");
        }

        beginTest ("Harmonics of held notes are not resonances");
        {
            auto r = okResult();
            r.findings.clear();
            for (double f : { 330.0, 440.0, 660.0, 881.0, 1980.0, 3960.0 })
                r.findings.push_back (narrow (FindingType::Resonance, f, 10.0, 12.0, 0.9));
            Settings s;
            s.instrument = Instrument::Strings;
            const auto all = generateCorrections (r, s);
            for (const auto& sg : all)
                expect (sg.kind != K::Cut, "a chord's harmonic series is not cut");
            s.preserveCharacter = false;
            {
                bool anyCut = false;
                for (const auto& sg : generateCorrections (r, s))
                    anyCut = anyCut || sg.kind == K::Cut;
                expect (anyCut, "unless Preserve character is off");
            }

            // A real resonance among unrelated frequencies is still cut.
            r.findings = { narrow (FindingType::Resonance, 824.0, 10.0, 12.0, 0.9), narrow (FindingType::Resonance, 1370.0, 10.0, 12.0, 0.9),
                           narrow (FindingType::Resonance, 3170.0, 10.0, 12.0, 0.9) };
            s.preserveCharacter = true;
            {
                int cuts = 0;
                for (const auto& sg : generateCorrections (r, s))
                    cuts += sg.kind == K::Cut ? 1 : 0;
                expect (cuts >= 2, "unrelated peaks are still cut");
            }
        }

        beginTest ("Minor findings are left alone, more so at Gentle");
        {
            auto r = okResult();
            r.findings = { narrow (FindingType::Resonance, 1000.0, 6.0, 8.0, 0.2) };
            Settings s;
            s.strength = Strength::Gentle;
            auto all = generateCorrections (r, s);
            expect (forFinding (all, 0)->kind == K::LeftAlone);
            expect (forFinding (all, 0)->reason.contains ("Minor"), forFinding (all, 0)->reason);
            s.strength = Strength::Moderate;
            expect (forFinding (generateCorrections (r, s), 0)->band.has_value());

            // Left alone, but still available to choose.
            s.strength = Strength::Gentle;
            const auto gentle = generateCorrections (r, s);
            const auto* alone = forFinding (gentle, 0);
            expect (alone->optionalBand.has_value(), "a left-alone finding can still be applied");
            if (alone->optionalBand)
                expect (alone->optionalBand->gainDb < 0.0 && std::abs (alone->optionalBand->frequency - 1000.0) < 1.0);
        }

        beginTest ("Instrument character: the same buildup is treated differently");
        {
            auto r = okResult();
            r.findings = { broad (50.0, 75.0, 110.0, 3.5) };   // low-end buildup
            Settings s;
            auto general = generateCorrections (r, s);
            expect (forFinding (general, 0)->band.has_value(), "general source: low buildup is cut");

            s.instrument = Instrument::KickDrum;
            auto kick = generateCorrections (r, s);
            expect (forFinding (kick, 0)->kind == K::LeftAlone, "kick: mild low end is its body");
            expect (forFinding (kick, 0)->reason.contains ("kick body"), forFinding (kick, 0)->reason);

            r.findings = { broad (50.0, 75.0, 110.0, 8.0) };
            const double kickCut = cutAt (generateCorrections (r, s), 0);
            s.instrument = Instrument::General;
            const double generalCut = cutAt (generateCorrections (r, s), 0);
            expect (kickCut < 0.0 && kickCut > generalCut, "kick: a strong excess is cut, but less");

            s.instrument = Instrument::KickDrum;
            s.preserveCharacter = false;
            expectWithinAbsoluteError (cutAt (generateCorrections (r, s), 0), generalCut, 0.01);

            // Mud on a vocal is not protected: it is outside the voice's character regions.
            r.findings = { broad (200.0, 280.0, 400.0, 4.0) };
            s.instrument = Instrument::FemaleVocal;
            s.preserveCharacter = true;
            expect (forFinding (generateCorrections (r, s), 0)->band.has_value());
        }

        beginTest ("Broad shapes: shelves at the extremes, bells in between");
        {
            auto r = okResult();
            r.findings = { broad (8000.0, 11000.0, 16000.0, 5.0), broad (40.0, 60.0, 100.0, 5.0), broad (400.0, 600.0, 900.0, 5.0) };
            const auto all = generateCorrections (r, {});
            expect (forFinding (all, 0)->band->type == dsp::FilterType::HighShelf);
            expect (forFinding (all, 1)->band->type == dsp::FilterType::LowShelf);
            const auto& bell = *forFinding (all, 2)->band;
            expect (bell.type == dsp::FilterType::Bell && bell.q >= 0.5 && bell.q <= 3.0);
        }

        beginTest ("High-pass: for rumble, or for the instrument, never into the notes");
        {
            auto r = okResult();
            Finding rumble;
            rumble.type = FindingType::Rumble;
            rumble.frequency = 20.0;
            rumble.lowFrequency = 10.0;
            rumble.highFrequency = 50.0;
            rumble.severity = 0.8;
            rumble.confidence = 1.0;
            r.findings = { rumble };
            auto all = generateCorrections (r, {});
            const auto* hp = forFinding (all, 0);
            expect (hp != nullptr && hp->kind == K::HighPass && hp->band->type == dsp::FilterType::HighPass);
            expectWithinAbsoluteError (hp->band->frequency, 40.0, 0.5); // 0.8 x where the content starts
            expect (all.front().kind == K::HighPass, "rumble first");

            Settings s;
            s.mode = Mode::Transparent;
            s.autoHighPass = false;
            s.instrument = Instrument::MaleVocal;
            r.findings.clear();
            expect (withBands (generateCorrections (r, s)).empty(), "no rumble, no auto high-pass: nothing");
            s.autoHighPass = true;
            s.instrument = Instrument::MaleVocal;
            r.lowestContentHz = 0.0;
            r.pitch.pitched = true;
            r.pitch.lowHz = 98.0;
            r.pitch.medianHz = 130.0;
            r.pitch.highHz = 200.0;
            auto vocal = withBands (generateCorrections (r, s));
            expect (vocal.size() == 1 && vocal[0].kind == K::HighPass);
            if (! vocal.empty())
                expect (vocal[0].band->frequency <= 98.0 * 0.7 + 0.01, "kept below the lowest note");
            r.pitch.lowHz = 25.0;
            expect (withBands (generateCorrections (r, s)).empty(), "notes reach too low: no high-pass");
        }

        beginTest ("Transparent never boosts; Polished adds subtle enhancement only where nothing is wrong");
        {
            auto r = okResult();
            r.findings = { narrow (FindingType::Resonance, 500.0, 8.0, 10.0) };
            Settings s;
            s.mode = Mode::Transparent;
            s.instrument = Instrument::Vocal;
            for (const auto& sg : withBands (generateCorrections (r, s)))
                expect (sg.band->gainDb <= 0.0 || sg.band->type == dsp::FilterType::HighPass);

            s.mode = Mode::Polished;
            int boosts = 0;
            for (const auto& sg : withBands (generateCorrections (r, s)))
                if (sg.kind == K::Enhancement)
                {
                    ++boosts;
                    expect (sg.band->gainDb > 0.0 && sg.band->gainDb <= 2.5, juce::String (sg.band->gainDb));
                }
            expectEquals (boosts, 2, "vocal: presence and air");

            r.findings.push_back (broad (2500.0, 4000.0, 6000.0, 4.0)); // harshness: no presence or air boost
            boosts = 0;
            for (const auto& sg : withBands (generateCorrections (r, s)))
                boosts += sg.kind == K::Enhancement ? 1 : 0;
            expectEquals (boosts, 0, "no boost next to a measured problem");
        }

        beginTest ("Number of corrections and overlaps");
        {
            auto r = okResult();
            for (int i = 0; i < 8; ++i)
                r.findings.push_back (narrow (FindingType::Resonance, 300.0 * std::pow (1.6, i), 8.0, 10.0, 0.9 - 0.05 * i));
            r.findings.push_back (narrow (FindingType::Resonance, 305.0, 6.0, 10.0, 0.5)); // next to the first
            Settings s;
            s.maxCorrections = 3;
            auto all = generateCorrections (r, s);
            expectEquals ((int) withBands (all).size(), 3);
            expect (withBands (all)[0].findingIndex == 0, "most severe first");
            expect (forFinding (all, 8)->kind == K::LeftAlone);
            expect (forFinding (all, 8)->reason.contains ("covered"), forFinding (all, 8)->reason);
            expect (forFinding (all, 7)->title == "Not used");
        }

        beginTest ("Instrument balance: compared with a typical instance of the instrument");
        {
            Settings s;
            s.instrument = Instrument::KickDrum;
            s.autoHighPass = false;
            s.mode = Mode::Transparent;

            // A kick that looks like a typical kick: its huge low end is not a problem; it gets only the
            // usual boxiness cut, at the usual size.
            auto typical = resultShapedLike (Instrument::KickDrum);
            const auto cmp = compareToTarget (typical, Instrument::KickDrum, Strength::Moderate);
            expect (cmp.valid);
            for (auto d : cmp.deviationDb)
                expect (std::abs (d) < 0.5, "deviation of a typical kick: " + juce::String (d, 2));
            {
                const auto bands = withBands (generateCorrections (typical, s));
                expectEquals ((int) bands.size(), 1, "typical kick: only the usual boxiness cut");
                if (! bands.empty())
                {
                    expect (bands[0].reason.contains ("usual move"), bands[0].reason);
                    expect (bands[0].band->frequency >= 250.0 && bands[0].band->frequency <= 600.0);
                    expectWithinAbsoluteError (bands[0].band->gainDb, -3.0, 0.6);
                }
            }

            // The same kick with 8 dB too much around 400 Hz: a boxiness cut there.
            auto boxy = resultShapedLike (Instrument::KickDrum, { { 400.0, 8.0 } });
            auto cuts = withBands (generateCorrections (boxy, s));
            expectEquals ((int) cuts.size(), 1);
            if (! cuts.empty())
            {
                expect (cuts[0].band->gainDb < -1.5 && cuts[0].band->type == dsp::FilterType::Bell, cuts[0].title);
                expect (std::abs (std::log2 (cuts[0].band->frequency / 400.0)) < 0.25, cuts[0].title);
                expect (cuts[0].heading.containsIgnoreCase ("boxiness"), cuts[0].heading);
                expect (cuts[0].reason.contains ("typical kick drum balance"), cuts[0].reason);
            }

            // On a vocal, a mild hump the generic analysis calls mud is normal: only the usual mud move.
            s.instrument = Instrument::MaleVocal;
            auto vocal = resultShapedLike (Instrument::MaleVocal);
            vocal.findings = { broad (200.0, 300.0, 450.0, 3.0) }; // the generic analysis calls it mud
            auto all = generateCorrections (vocal, s);
            for (const auto& sg : withBands (all))
                expect (sg.reason.contains ("usual move"), sg.reason);
            expect (forFinding (all, 0)->kind == K::LeftAlone);
            expect (forFinding (all, 0)->reason.contains ("Normal for this instrument (male vocal)"), forFinding (all, 0)->reason);

            // Drums: steady narrow peaks are the drum's own tones; only ringing is cut.
            s.instrument = Instrument::SnareDrum;
            s.mode = Mode::Transparent;
            auto snare = resultShapedLike (Instrument::SnareDrum);
            snare.findings = { narrow (FindingType::Resonance, 600.0, 10.0, 16.0), narrow (FindingType::Ringing, 900.0, 10.0, 8.0) };
            all = generateCorrections (snare, s);
            expect (forFinding (all, 0)->kind == K::LeftAlone && forFinding (all, 0)->reason.contains ("Drum heads"));
            expect (forFinding (all, 1)->band.has_value(), "excessive ringing is still cut");
            s.instrument = Instrument::MaleVocal;
            s.mode = Mode::Polished;

            // ... but harsh 3 kHz and dull top are acted on (Polished adds a little back where it is lacking).
            s.mode = Mode::Polished;
            auto harsh = resultShapedLike (Instrument::MaleVocal, { { 3500.0, 8.0 }, { 11000.0, -9.0 } });
            int cutsFound = 0, boosts = 0;
            for (const auto& sg : withBands (generateCorrections (harsh, s)))
            {
                cutsFound += sg.kind == K::Cut && sg.band->frequency > 2000.0 ? 1 : 0;
                boosts += sg.kind == K::Enhancement ? 1 : 0;
                if (sg.kind == K::Enhancement)
                    expect (sg.band->gainDb > 0.0 && sg.band->gainDb <= 3.0, sg.title);
            }
            expectEquals (cutsFound, 1, "harshness cut");
            expect (boosts >= 1, "presence / air in Polished");
            s.mode = Mode::Transparent;
            for (const auto& sg : withBands (generateCorrections (harsh, s)))
                expect (sg.band->gainDb <= 0.0, "Transparent never boosts");

            // Instruments without a typical balance (synthesizer) use the generic analysis.
            expect (! compareToTarget (typical, Instrument::Synthesizer, Strength::Moderate).valid);
            expect (! compareToTarget (typical, Instrument::General, Strength::Moderate).valid);
        }

        beginTest ("The typical curve follows the track's own note: a low-tuned tom keeps its fundamental");
        {
            Settings s;
            s.instrument = Instrument::RackTom;
            s.strength = Strength::Aggressive;
            s.mode = Mode::Polished;
            s.amount = 1.5;
            auto lowTom = resultShapedLike (Instrument::RackTom, {}, 0.65); // tuned about 0.6 octave lower than typical
            const auto cmp = compareToTarget (lowTom, Instrument::RackTom, Strength::Aggressive);
            expect (cmp.valid);
            expectWithinAbsoluteError (cmp.measuredPeakHz / cmp.typicalPeakHz, 0.65, 0.08);
            for (const auto& sg : withBands (generateCorrections (lowTom, s)))
                expect (sg.band->type == dsp::FilterType::HighPass || sg.band->gainDb > 0.0 || sg.band->frequency > 250.0,
                        "the tom's note is not cut: " + sg.title);

            // Even a genuinely boomy note is left alone with Preserve character, and cut when it is off.
            auto boomy = resultShapedLike (Instrument::RackTom, { { 104.0, 10.0 } }, 0.65);
            for (const auto& sg : withBands (generateCorrections (boomy, s)))
                expect (sg.band->type == dsp::FilterType::HighPass || sg.band->gainDb > 0.0 || sg.band->frequency > 250.0, sg.title);
            s.preserveCharacter = false;
            bool cutNote = false;
            for (const auto& sg : withBands (generateCorrections (boomy, s)))
                cutNote |= sg.band->type != dsp::FilterType::HighPass && sg.band->frequency < 200.0;
            expect (cutNote, "Preserve character off: the boom is cut");
        }

        beginTest ("Screaming vocal: grit kept, ice-pick harshness and fizz tamed");
        {
            Settings s;
            s.instrument = Instrument::ScreamingVocal;
            expect (instrumentGroup (Instrument::ScreamingVocal) == "Vocals");
            // A scream with extra 3.5 kHz edge.
            auto scream = resultShapedLike (Instrument::ScreamingVocal, { { 3500.0, 7.0 } });
            bool harshCut = false, gritCut = false;
            for (const auto& sg : withBands (generateCorrections (scream, s)))
            {
                harshCut |= sg.band->gainDb < 0.0 && sg.band->frequency >= 2500.0 && sg.band->frequency <= 5000.0;
                gritCut |= sg.band->gainDb < -0.1 && sg.band->frequency >= 1000.0 && sg.band->frequency < 2000.0;
            }
            expect (harshCut, "harshness cut");
            expect (! gritCut, "the 1-2 kHz grit is not cut");
            // Its normal upper-mid weight is not a problem the way it would be on a clean vocal.
            auto typical = resultShapedLike (Instrument::ScreamingVocal);
            const auto cmp = compareToTarget (typical, Instrument::ScreamingVocal, Strength::Moderate);
            for (auto d : cmp.deviationDb)
                expect (std::abs (d) < 0.5);
        }

        beginTest ("No analysis, no corrections; settings round-trip");
        {
            AnalysisResult r;
            r.status = AnalysisResult::Status::InsufficientAudio;
            expect (generateCorrections (r, {}).empty());

            Settings s;
            s.instrument = Instrument::FloorTom;
            s.strength = Strength::Aggressive;
            s.mode = Mode::Polished;
            s.amount = 0.7;
            s.maxCorrections = 8;
            s.preserveCharacter = false;
            s.autoHighPass = true;
            s.applyAutomatically = true;
            expect (Settings::fromValueTree (s.toValueTree()) == s);
        }

        beginTest ("Applying: real EQ bands in free slots, replace, reset, undo, saved with the session");
        {
            EyraEQAudioProcessor p;
            const double sr = 48000.0;
            p.setPlayConfigDetails (2, 2, sr, 512);
            p.prepareToPlay (sr, 512);

            // A band the user made first must survive.
            params::setParameterValue (p.apvts, params::bandParamID (0, params::BandField::Used), 1.0f);
            params::setParameterValue (p.apvts, params::bandParamID (0, params::BandField::Frequency), 5000.0f);

            auto r = okResult();
            r.findings = { narrow (FindingType::Resonance, 824.0, 8.0, 12.0), broad (200.0, 280.0, 400.0, 5.0) };
            std::vector<dsp::BandParams> bands;
            for (const auto& sg : withBands (generateCorrections (r, {})))
                bands.push_back (*sg.band);

            expectEquals (p.applyCorrections (bands), 2);
            expectEquals (usedBands (p), 3);
            const auto slots = p.getCorrectionSlots();
            expect (slots.size() == 2 && std::find (slots.begin(), slots.end(), 0) == slots.end(), "user band untouched");
            const auto b = params::readBand (p.apvts, slots[0]);
            expect (b.used && b.type == dsp::FilterType::Bell);
            expectWithinAbsoluteError (b.frequency, 824.0, 1.0);
            expectWithinAbsoluteError (b.gainDb, bands[0].gainDb, 0.05);

            // Applying again replaces instead of piling up.
            expectEquals (p.applyCorrections (bands), 2);
            expectEquals (usedBands (p), 3);

            // Saved with the session.
            auto settings = p.getCorrectionSettings();
            settings.instrument = Instrument::SnareDrum;
            p.setCorrectionSettings (settings);
            juce::MemoryBlock state;
            p.getStateInformation (state);
            {
                EyraEQAudioProcessor q;
                q.setStateInformation (state.getData(), (int) state.getSize());
                expect (q.getCorrectionSettings().instrument == Instrument::SnareDrum);
                expect (q.getCorrectionSlots() == p.getCorrectionSlots());
                q.resetCorrections();
                expectEquals (usedBands (q), 1);
            }

            p.resetCorrections();
            expectEquals (usedBands (p), 1, "reset removes only the corrections");
            expect (p.apvts.getRawParameterValue (params::bandParamID (0, params::BandField::Used))->load() >= 0.5f);
            p.history.undo();
            expectEquals (usedBands (p), 3, "reset is undoable");

            // EQ full: as many as fit.
            for (int i = 0; i < params::kNumBands; ++i)
                params::setParameterValue (p.apvts, params::bandParamID (i, params::BandField::Used), 1.0f);
            p.resetCorrections();
            for (int i = 0; i < params::kNumBands; ++i)
                params::setParameterValue (p.apvts, params::bandParamID (i, params::BandField::Used), i < params::kNumBands - 1 ? 1.0f : 0.0f);
            expectEquals (p.applyCorrections (bands), 1, "only one free slot");
        }

        beginTest ("End to end: analyse, apply, the problems are reduced in the output");
        {
            const double sr = 48000.0;
            auto x = pinkNoise (sr, 12.0, 4);
            filter (x, { dsp::FilterType::Bell, 2400.0, 12.0, 10.0, 1 }, sr);

            const auto before = analysis::analyseAudio (x.data(), (int) x.size(), sr);
            const auto* resonance = findNear (before, FindingType::Resonance, 2400.0);
            expect (resonance != nullptr, "resonance found before");

            Settings s;
            s.strength = Strength::Aggressive;
            std::vector<dsp::BandParams> bands;
            for (const auto& sg : withBands (generateCorrections (before, s)))
                bands.push_back (*sg.band);
            expect (! bands.empty());

            EyraEQAudioProcessor p;
            p.setPlayConfigDetails (2, 2, sr, 512);
            p.prepareToPlay (sr, 512);
            p.applyCorrections (bands);
            const auto y = processThrough (p, x);
            const auto after = analysis::analyseAudio (y.data(), (int) y.size(), sr);
            const auto* left = findNear (after, FindingType::Resonance, 2400.0);
            if (resonance != nullptr)
                expect (left == nullptr || left->excessDb < resonance->excessDb - 4.0,
                        "resonance reduced: " + juce::String (resonance->excessDb, 1) + " dB -> "
                            + (left != nullptr ? juce::String (left->excessDb, 1) + " dB" : juce::String ("gone")));
        }
    }
};

static CorrectionTests correctionTests;
} // namespace
