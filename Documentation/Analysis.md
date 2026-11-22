# Audio analysis (Phase 2)

EyraEQ can listen to the track it is inserted on and report frequency problems. Phase 2 *detects
and explains* problems. It does not change any EQ setting; generating corrective bands is Phase 4.
Instrument recognition (Phase 3) is not available yet, so all findings are based on the audio alone,
and the panel says so.

## Workflow

1. Press **Analyze** in the Audio Analysis panel (toggle the panel with the **Analysis** button).
2. Play the track, ideally looping the section that matters.
3. EyraEQ listens to the input signal (before the EQ) and shows how much real signal it has heard.
   Silence does not count.
4. It stops on its own after 15 s of signal (or 45 s of total capture), or press **Stop & analyze**
   once at least 2 s of signal has been heard.
5. Findings appear in the panel, most important first. Click one to highlight it on the spectrum.
   The dashed line is the average spectrum of the analysed audio.

In the standalone app, **Analyze file...** analyses a WAV, AIFF, FLAC, Ogg or MP3 file directly
(up to the first 5 minutes).

Less audio means less confidence: below 2 s of signal nothing is reported, and confidence reaches
100% at 10 s.

## What is detected

Every finding is measured relative to *this* signal. A frequency is never reported just because of
where it lies; ranges only decide the name.

| Finding | How it is detected |
|---|---|
| **Resonance** | A narrow peak (Q ≥ 2) in the long-term spectrum at least 6 dB above the median of its 2/3-octave neighbourhood, present in ≥ 60% of the audio (judged on ~0.6 s groups of frames, so random noise peaks don't count). |
| **Intermittent resonance** | Same, but present in 20–60% of the audio. It is noted that a dynamic band may suit it better. |
| **Ringing** | A narrow band that decays at least 1.5× slower than the frequencies around it after transients (decay rates measured per hit on a 10.7 ms time grid, median over hits). Found from the average spectrum and from the spectrum of the decays alone. |
| **Mains hum** | Two or more stationary narrow peaks at multiples of 50 or 60 Hz (within 0.4%), present in ≥ 80% of the audio. |
| **Build-up** | A region at least 2/3 octave wide whose third-octave level is ≥ 2.5 dB above the average of the bands 2/3 to 4/3 octave away. Named by its centre frequency: low-end build-up, low-mid build-up (mud), boxiness, honky midrange, harshness, excess high frequencies. |
| **Sub-sonic rumble** | ≥ 1% of the energy below 25 Hz (or 0.7× the lowest pitch for very low sources), when that octave is not just the tail of the musical low end. |
| **DC offset** | Offset above −46 dBFS and within 30 dB of the signal level. |

## Protecting the natural sound

- **Harmonics are not problems.** For a source with a steady pitch, peaks at harmonics of the
  fundamental are never reported as resonances.
- **The fundamental region is natural.** A build-up overlapping the source's own pitch range is not
  reported. This covers the case in the specification: a tom's strong 150–250 Hz body is not called mud.
  A source's fundamental and harmonics are also never reported as ringing.
- **Harmonic sources are measured by their envelope.** For pitched material, band levels follow the
  harmonic envelope rather than raw bands, which would alternate between holding a harmonic and
  holding none. Real formant boosts (e.g. boxiness on a voice-like tone) are still found.
- **No double counting.** Narrow peaks are removed before measuring broad balance, so a resonance is
  reported once, not also as a build-up.
- **Edges are treated with caution.** At the bottom and top of a source's range the comparison is
  one-sided, so more evidence is required there (≥ 6 dB) and confidence is reduced.
- **Confidence is reported, not hidden.** It reflects the amount of audio, whether the finding is
  consistent across the capture (thirds), and pitch ambiguity (for example, a changing pitch makes an
  intermittent peak possibly a played note).

## Also reported (information, not problems)

Pitch (note and frequency, or "no steady pitch"), percussive or sustained, spectral tilt (dB/oct,
100 Hz to 10 kHz), and spectral centroid.

## Signal processing

| Stage | Details |
|---|---|
| Gate | 20 ms blocks; active if above −65 dBFS and within 45 dB of the loudest block |
| Short-time analysis | Hann, ~85 ms window (4096 at 48 kHz), 1/8 hop; 1/24-octave band energies, spectral flux |
| Onsets | Flux peaks above an adaptive median threshold, with a real rise in level (≥ 4 dB) |
| Percussive | ≥ 0.2 onsets/s and a median drop ≥ 6 dB, 200 ms after the energy peak of each hit |
| Pitch | Normalised, taper-corrected autocorrelation per frame (30–1500 Hz), octave-error guard; median and spread over voiced frames; refined onto the nearest spectral peak |
| Long-term spectrum | Hann, ~0.34 s window (16384 at 48 kHz, 2.9 Hz resolution), 1/4 hop, active frames only, at most 240 frames |

Typical cost: about 0.7 s for 30 s of 48 kHz audio on one core (measured in the tests), on the
worker thread.

## Threading

The audio thread writes the pre-EQ signal into a lock-free FIFO only while listening (an atomic check
otherwise). A low-priority worker thread allocates the capture buffer when listening starts, drains
the FIFO, tracks progress, and runs the analysis. The UI polls the state and picks up results through
a lock-protected shared pointer. The audio thread never allocates, locks or waits; this is verified
by a test that counts allocations during `processBlock` while listening.

## Known limitations

- Instrument-agnostic: what counts as "too much" can depend on the instrument (a kick's low end, a
  cymbal's top). Phase 3 adds the instrument context; until then, edge-of-range findings carry lower
  confidence and the wording says they may be natural.
- Polyphonic, changing-pitch material (chords, melodies) can produce intermittent peaks that are
  really played notes; these are marked with lower confidence and say so.
- Results are not saved with the session; analyse again after reopening.
- Mono analysis (L+R). Side-only problems are not analysed separately.
