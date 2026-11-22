# DSP

## Filter design

All bands are cascades of normalised biquads (`a0 = 1`), designed in double precision from
analogue prototypes. Coefficients come from **magnitude-matched** designs, not the bilinear
transform:

- **Poles** are mapped by impulse invariance (`z = e^{sT}`), which preserves the analogue pole
  frequency and damping without frequency warping.
- **Zeros** are solved so that the digital magnitude equals the analogue magnitude at DC, at the band
  frequency and at Nyquist (M. Vicanek, *Matched Second Order Digital Filters*, 2016).
- **Cuts are inverted boosts.** For bells and shelves, the −g prototype is exactly the reciprocal of
  the +g prototype. A boost's lightly damped poles are matched much more accurately, so a cut is
  designed as the inverse of the corresponding boost (minimum phase, so the inverse is stable).
- **Fallbacks:** if a pole would alias past Nyquist, or the matched solution is not realisable, the
  section uses a pre-warped bilinear design instead. Every returned section is checked for stability.

Prototypes (s normalised to the band frequency, `A = 10^(gain/40)`):

| Type | H(s) |
|---|---|
| Bell | (s² + s·A/Q + 1) / (s² + s/(A·Q) + 1) |
| Low shelf | A·(s² + (√A/Q)s + A) / (A·s² + (√A/Q)s + 1) |
| High shelf | A·(A·s² + (√A/Q)s + 1) / (s² + (√A/Q)s + A) |
| High / low pass | Butterworth cascade of order slope/6; Q scales the highest-Q section (Q = 0.707 is pure Butterworth) |
| Notch | (s² + 1) / (s² + s/Q + 1) |
| Band pass | (s/Q) / (s² + s/Q + 1), 0 dB peak |
| Tilt | first order, (T·s + 1)/(s + T) with T = 10^(gain/40): −gain/2 at DC, +gain/2 at HF, 0 dB at the pivot |

Band frequencies are clamped to 0.49·fs for design. A 30 kHz band at 44.1 kHz is therefore designed
at 21.6 kHz.

### Measured accuracy (from the unit tests)

- Bell gain at the centre frequency: error < 0.0001 dB for all tested frequencies, gains, Qs and
  sample rates (44.1–192 kHz).
- Deviation from the analogue bell: ≤ 0.77 dB when the centre frequency is ≤ fs/6 (measured up to
  0.35·fs). Over the full range the worst case is 3.3 dB, compared with 12 dB for a bilinear
  design. The remaining error appears only where the analogue bell extends past Nyquist, which no
  second-order digital filter can reproduce exactly.
- High / low pass: −3.01 dB at the cutoff and the nominal asymptotic slope (±0.6 dB) for every slope
  from 6 to 96 dB/oct.
- Stability: 360,000 sections across all types, 10 Hz–30 kHz, Q 0.025–40, ±30 dB, all slopes and
  sample rates from 22.05 to 192 kHz: all stable.

## Processing

- Transposed direct form II biquads with double-precision state.
- Frequency, gain and Q are smoothed (40 ms; frequency and Q multiplicatively). Coefficients are
  recomputed every 16 samples while a value is moving.
- Discrete changes (type, slope, stereo placement) and enable / bypass fade the band's wet signal out
  over 12 ms with an S-curve, switch, and fade back in. Measured transients in the tests: −121 dBFS
  or lower for these on a −20 dBFS signal.
- Mid/side: M = (L+R)/2, S = (L−R)/2. Converted only around bands that need it; the conversion is exact.
- Mono input: Side bands do nothing (a mono signal has no side); all other placements process the channel.
- Solo: a band-pass at the soloed band's current frequency and Q (Q clamped to 0.5–12) is crossfaded
  in after the EQ.
- Master bypass crossfades to the untouched input over 15 ms. When fully bypassed, the output is
  bit-identical to the input. With no bands active, the EQ is also bit-transparent.
- `juce::ScopedNoDenormals` in the audio callback; a 24-band, high-Q impulse decays to below 1e-20
  without NaN or Inf in the tests.

## Analyser

- Mono sum of each tap, Blackman-Harris window, 2^11–2^15 point FFT, overlap 2×/4×/8× (speed).
- Calibrated so a full-scale sine reads 0 dBFS. Display slope pivots at 1 kHz.
- Display points are log-spaced. Each point takes the highest bin in its range, or interpolates
  between bins where bins are sparser than points.
- RMS mode uses exponential power averaging. Peak mode rises instantly and falls at the set rate.
  Time constants follow real elapsed time even when the UI skips frames.
