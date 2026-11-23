# DSP

## Processing

The EQ is the built-in EQ of the Ravenyr DAW (`DSP/EqCore`), ported unchanged in behaviour.

- Float RBJ biquads (`Biquad`). Cuts are Butterworth cascades of order slope / 6 (one first-order
  section if odd); Q adds resonance to the sharpest section.
- Dynamic bands follow the level in the band and move the band's gain toward its range (a 4:1-like
  move, never past the range). Spectral bands do this per frequency in a 2048-point STFT at 75 %
  overlap, so latency is 2048 samples while any band is Spectral and zero otherwise.
- Listen replaces the output with a band-pass at the band's frequency.
- Master bypass crossfades to the (latency-aligned) dry signal.
- `juce::ScopedNoDenormals` in the audio callback.

## Analyser

- Mono sum of each tap, Blackman-Harris window, 2^11–2^15 point FFT, overlap 2×/4×/8× (speed).
- Calibrated so a full-scale sine reads 0 dBFS. Display slope pivots at 1 kHz.
- Display points are log-spaced. Each point takes the highest bin in its range, or interpolates
  between bins where bins are sparser than points.
- RMS mode uses exponential power averaging. Peak mode rises instantly and falls at the set rate.
  Time constants follow real elapsed time even when the UI skips frames.
