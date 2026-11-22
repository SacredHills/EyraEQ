# Instrument recognition: NSynth validation (test split)

Model: yamnet 1.0.0 (sha256 e088acd8d281...). Taxonomy: instrument_taxonomy.json. Up to 100 random notes per group (seed 1).
Decision thresholds: present 0.05, confident 0.4, dominance 4.0.

NSynth (Google Magenta, CC BY 4.0): isolated single notes, 4 s at 16 kHz. This is harder than real
performances and contains no drums and no gender labels for vocals.

| Family / source | Expected | Notes | Top-1 correct | Detected (confident) | Correct when detected | Ambiguous (contains expected) | Unknown | Most common confusions |
|---|---|---:|---:|---:|---:|---:|---:|---|
| bass / electronic | electric_bass | 100 | 72% | 0% | - | 97% (88%) | 3% | electric_guitar (16), percussion (10) |
| bass / synthetic | synth | 100 | 53% | 0% | - | 55% (69%) | 45% | electric_bass (11), acoustic_other (10) |
| brass / acoustic | brass | 100 | 52% | 23% | 96% | 48% (65%) | 29% | acoustic_other (18), vocal (18) |
| flute / acoustic | acoustic_other | 55 | 78% | 55% | 100% | 24% (69%) | 22% | vocal (7), brass (5) |
| guitar / acoustic | acoustic_guitar | 100 | 0% | 1% | 0% | 61% (48%) | 38% | electric_guitar (28), percussion (27) |
| guitar / electronic | electric_guitar | 100 | 22% | 0% | - | 59% (36%) | 41% | synth (32), electric_bass (16) |
| keyboard / acoustic | piano | 100 | 17% | 0% | - | 59% (22%) | 41% | percussion (32), electric_guitar (23) |
| keyboard / electronic | piano | 100 | 13% | 2% | 100% | 62% (29%) | 36% | synth (25), percussion (23) |
| mallet / acoustic | percussion | 100 | 67% | 1% | 100% | 41% (88%) | 58% | synth (11), electric_bass (8) |
| organ / electronic | organ | 100 | 0% | 3% | 0% | 65% (2%) | 32% | synth (27), electric_guitar (16) |
| reed / acoustic | acoustic_other | 100 | 31% | 5% | 80% | 66% (36%) | 29% | synth (30), brass (25) |
| string / acoustic | strings | 100 | 34% | 4% | 100% | 83% (55%) | 13% | acoustic_bass (47), acoustic_other (4) |
| vocal / acoustic | vocal | 22 | 73% | 45% | 100% | 41% (44%) | 14% | synth (5), acoustic_other (1) |
| **All** | | 1177 | 36% | 7% | 92% | 61% (49%) | 32% | |

