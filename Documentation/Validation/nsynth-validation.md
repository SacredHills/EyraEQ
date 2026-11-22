# Instrument recognition: NSynth validation (test split)

Model: yamnet 1.0.0 (sha256 e088acd8d281...). Taxonomy: instrument_taxonomy.json. Up to 100 random notes per group (seed 1).
Decision thresholds: present 0.12, confident 0.3, dominance 1.6.

NSynth (Google Magenta, CC BY 4.0): isolated single notes, 4 s at 16 kHz. This is harder than real
performances and contains no drums and no gender labels for vocals.

| Family / source | Expected | Notes | Top-1 correct | Detected (confident) | Correct when detected | Ambiguous (contains expected) | Unknown | Most common confusions |
|---|---|---:|---:|---:|---:|---:|---:|---|
| bass / electronic | electric_bass | 100 | 72% | 31% | 100% | 50% (84%) | 19% | electric_guitar (16), percussion (10) |
| bass / synthetic | synth | 100 | 53% | 17% | 94% | 26% (54%) | 57% | electric_bass (11), acoustic_other (10) |
| brass / acoustic | brass | 100 | 52% | 34% | 82% | 27% (74%) | 39% | acoustic_other (18), vocal (18) |
| flute / acoustic | acoustic_other | 55 | 78% | 58% | 97% | 20% (73%) | 22% | vocal (7), brass (5) |
| guitar / acoustic | acoustic_guitar | 100 | 0% | 10% | 0% | 27% (52%) | 63% | electric_guitar (28), percussion (27) |
| guitar / electronic | electric_guitar | 100 | 22% | 10% | 0% | 20% (25%) | 70% | synth (32), electric_bass (16) |
| keyboard / acoustic | piano | 100 | 17% | 1% | 0% | 16% (38%) | 83% | percussion (32), electric_guitar (23) |
| keyboard / electronic | piano | 100 | 13% | 13% | 15% | 26% (15%) | 61% | synth (25), percussion (23) |
| mallet / acoustic | percussion | 100 | 67% | 8% | 100% | 10% (100%) | 82% | synth (11), electric_bass (8) |
| organ / electronic | organ | 100 | 0% | 12% | 0% | 23% (0%) | 65% | synth (27), electric_guitar (16) |
| reed / acoustic | acoustic_other | 100 | 31% | 16% | 31% | 26% (42%) | 58% | synth (30), brass (25) |
| string / acoustic | strings | 100 | 34% | 16% | 31% | 45% (53%) | 39% | acoustic_bass (47), acoustic_other (4) |
| vocal / acoustic | vocal | 22 | 73% | 50% | 100% | 14% (67%) | 36% | synth (5), acoustic_other (1) |
| **All** | | 1177 | 36% | 18% | 65% | 26% (52%) | 56% | |

