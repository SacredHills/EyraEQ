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

Each instrument has a profile (`Source/Correction/Instrument.cpp`):

- **Character regions**: where the instrument naturally has a lot of energy (kick body 40–110 Hz and
  beater 2–5 kHz, snare body 150–260 Hz, a tom's body, a vocal's presence region, ...). Pitched
  instruments also protect the measured range of the played notes. With **Preserve character** on, a
  broad excess there is left alone below 4 dB and otherwise cut by half, and a narrow peak on the
  note's fundamental is left alone. This is why a floor tom's 100 Hz body is not "mud".
- **Safe high-pass**: with **Auto high-pass**, a 12 dB/oct high-pass at the instrument's usual limit
  (kick 30 Hz, snare 80 Hz, male vocal 80 Hz, cymbals 250 Hz, ...), always kept below 0.7 × the lowest
  measured note and 0.8 × the lowest measured content. A rumble finding gets a high-pass regardless.
- **Polished enhancements**: subtle boosts typical for the instrument (snare crack at 5 kHz, vocal
  presence and air, ...), 1–1.5 dB at Moderate. Each is offered only when no problem was measured
  within about half an octave and the recording has content there.

**Transparent** mode never boosts; **Polished** adds the enhancements.

## Testing

`Tests/CorrectionTests.cpp` checks each mapping above, strength and amount scaling, character
protection (the same low-end build-up is cut on a general source, left alone on a kick when mild, and
halved when strong), high-pass placement relative to the notes, that Transparent never boosts and
Polished avoids problem regions, the band limit and overlaps, and the processor side: free slots,
replace, reset, undo, session save. An end-to-end test analyses noise with a 12 dB resonance,
applies the corrections through the plug-in and analyses the output again: the resonance must drop
by at least 4 dB or disappear.

`EyraEQAnalyze --instrument "Snare drum" file.wav` prints the findings and proposed bands for a file.
