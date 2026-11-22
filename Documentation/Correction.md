# Corrective EQ (Phase 4)

EyraEQ turns the problems found by the [analysis](Analysis.md) into real EQ bands. The corrections are
ordinary bands on the main display: they can be adjusted, bypassed or deleted, are saved with the
session, and every apply or reset is a single undo step. Nothing changes the audio until the user
applies (or turns on *Apply automatically*).

## Workflow

1. Choose the instrument in the panel (or leave **General**).
2. Play the track and press **Analyze**.
3. Each finding shows the proposed move, e.g. *Cut 3.5 dB at 824 Hz, Q 8*, or why it is left alone.
4. Tick the moves you want and press **Apply corrections**. Applying again (after changing a setting
   or reanalysing) replaces the previous corrections instead of adding more. **Reset** removes them.

Bands the user created are never touched; corrections go into free band slots. If the EQ runs out of
free bands, the panel says how many were applied.

## From finding to band

| Finding | Band | Gain |
|---|---|---|
| Resonance | bell, Q from the measured peak (2–16) | part of the measured excess (see Strength) |
| Intermittent resonance | bell | 60% of a constant resonance's cut (a dynamic band would follow it better: Phase 5) |
| Ringing | bell, Q 2–10 | as resonance |
| Mains hum | bell, Q ≥ 10 | at least 3 dB |
| Broad build-up (mud, boxiness, honk, harshness) | bell, Q from the measured width (0.5–3); low shelf if it lies entirely below 120 Hz, high shelf above 7 kHz | part of the measured excess |
| Rumble, DC offset | high-pass (18 / 12 dB/oct) at 0.8 × where the content starts | — |

Cuts below 0.5 dB are not made. Overlapping cuts (within 1/6 octave) are merged into the stronger.
The most important moves come first; **Max bands** limits how many are used.

### Strength

| | Share of the excess removed | Largest broad cut | Largest narrow cut | Findings corrected |
|---|---|---|---|---|
| Gentle | 45% | 3 dB | 5 dB | severity ≥ 0.30 |
| Moderate (default) | 65% | 5 dB | 8 dB | severity ≥ 0.18 |
| Aggressive | 85% | 8 dB | 12 dB | severity ≥ 0.08 |

**Amount** (0–150%) then scales every gain.

### Instrument awareness

The generic analysis compares each part of the spectrum with the rest of the same track. That suits a
mix bus, whose balance should be fairly smooth, but not a single instrument: a kick is *supposed* to
have a huge 60 Hz bump and little at 400 Hz. So when an instrument is chosen, its broad tonal balance
is compared with the **typical balance of that instrument** instead:

1. The measured long-term spectrum (1/6-octave smoothed, expressed per 1/3 octave so pink noise is
   flat) is compared with the instrument's typical curve over its useful range, on a 1/12-octave grid.
2. The curve is shifted to the measured level (median difference), so only the shape matters.
3. Where the track is more than the tolerance above the curve (Gentle 6 dB, Moderate 4.5, Aggressive
   3.5) over at least 1/3 octave, a broad cut is proposed and named after the region ("boxiness
   (cardboard)" on a kick, "sibilance" on a vocal). In Polished mode, regions more than tolerance + 1 dB
   *below* the curve over at least half an octave get a small boost.
4. The generic broad findings are then shown as "normal for this instrument" or "handled by the
   balance correction", instead of being cut on their own.

The typical curve and its tolerance band are drawn on the spectrum ("typical floor tom" etc.).

**These curves are engineering rules of thumb** (typical spectra of well-recorded, mix-ready sources
as commonly described in mixing practice), written by hand in `Instrument.cpp`. They are not measured
from a dataset, the tolerances are deliberately wide, and they can be refined from real stems. Synthesizer,
"other acoustic instrument" and General have no curve and use the generic analysis.

The low main peak (a drum's note, a bass's or voice's register) differs from track to track, so for
drums, basses, guitars and vocals the low part of the curve is moved to the track's measured main peak
(up to an octave either way) before comparing. With **Preserve character** on, that peak itself is
never cut: a tom tuned to 105 Hz keeps its note even if the typical rack tom sits near 160 Hz.

### The usual moves for each instrument

On top of fixing what is measured as wrong, each instrument gets the moves engineers commonly make on
it, for example:

| Instrument | Usual moves |
|---|---|
| Kick | cut boxiness 250–600 Hz; add punch 45–90 Hz and beater click 2.5–5 kHz |
| Snare | add body 150–260 Hz and crack 4–6.5 kHz; cut boxiness / ring 400–900 Hz |
| Toms | a little more of the drum's own note; cut boxiness 280–700 Hz; add stick attack 3–5.5 kHz |
| Bass | cut mud 200–400 Hz; add weight 60–100 Hz and note definition 0.7–1.2 kHz |
| Vocals | cut mud 200–450 Hz; add presence 2.5–5 kHz and air (shelf at 10–11 kHz) |
| Screaming vocal | cut boxiness 300–600 Hz; add aggression 1–2 kHz; tame ice-pick harshness 2.5–5 kHz and fizz 5.5–9 kHz; a little air. Its typical curve has the weight in the upper mids, so normal grit is not treated as harshness |
| Guitars | cut mud / boom; add presence or sparkle (distorted: tame fizz 3–5 kHz) |

(Full list in `Instrument.cpp`.) Each move is **placed** where the track needs it most within its range
(the most excess for a cut, the most lacking for a boost) and **sized** from the measurement: the usual
amount on a track that matches the typical balance, more where the track has too much (cut) or too
little (boost), and dropped when the track does not need it. A usual cut is also dropped where a
measured problem already has a cut. Cuts are made in both modes, boosts only in **Polished** (the default).

Narrow peaks on a single instrument are treated more carefully than on a mix:

- **Drums and cymbals**: heads and shells ring at their own inharmonic frequencies, which are part of
  the drum's tone, so steady narrow peaks are left alone; only *ringing* (a band that decays much more
  slowly than the rest of the hit) is cut.
- Narrow peaks in parts of the spectrum that are quiet for the instrument (more than 18 dB below its
  loudest region on the typical curve) are left alone.
- At most three narrow cuts, 25% smaller and no narrower than Q 10.

Each instrument profile also has:

- **Character regions**: where the instrument naturally has a lot of energy (kick body 40–110 Hz and
  beater 2–5 kHz, snare body 150–260 Hz, a tom's body, a vocal's presence region, ...). Pitched
  instruments also protect the measured range of the played notes. With **Preserve character** on, a
  broad excess there is left alone below 4 dB and otherwise cut by half, and a narrow peak on the
  note's fundamental is left alone. This is why a floor tom's 100 Hz body is not "mud".
- **Safe high-pass**: with **Auto high-pass** (on by default), a 12 dB/oct high-pass at the instrument's usual limit
  (kick 30 Hz, snare 80 Hz, male vocal 80 Hz, cymbals 250 Hz, ...), always kept below 0.7 × the lowest
  measured note and 0.8 × the lowest measured content. A rumble finding gets a high-pass regardless.
- **Polished enhancements** for instruments without a typical curve: fixed subtle boosts, offered
  only when no problem was measured nearby and the recording has content there.

**Transparent** mode never boosts; **Polished** (default) adds the usual boosts and enhancements.

## Testing

`Tests/CorrectionTests.cpp` checks each mapping above, strength and amount scaling, character
protection (the same low-end build-up is cut on a general source, left alone on a kick when mild, and
halved when strong), the typical-balance comparison (a typical kick gets no corrections; the same kick
with 8 dB too much at 400 Hz gets one boxiness cut there; a mild hump that the generic analysis calls
mud is normal on a vocal; harsh 3.5 kHz is cut and a dull top gets air only in Polished), drums keeping
their shell tones while excessive ringing is still cut, high-pass placement relative to the notes, that Transparent never boosts and
Polished avoids problem regions, the band limit and overlaps, and the processor side: free slots,
replace, reset, undo, session save. An end-to-end test analyses noise with a 12 dB resonance,
applies the corrections through the plug-in and analyses the output again: the resonance must drop
by at least 4 dB or disappear.

`EyraEQAnalyze --instrument "Snare drum" file.wav` prints the findings and proposed bands for a file.
