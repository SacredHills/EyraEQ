# Instrument recognition: NSynth validation (test split)

Model: yamnet 1.0.0 (sha256 e088acd8d281...). Taxonomy: instrument_taxonomy.json. Up to 100 random notes per group (seed 1).
Decision thresholds: present 0.08; family confident 0.2, dominance 2.0; member confident 0.6, dominance 1.5.

NSynth (Google Magenta, CC BY 4.0): isolated single notes, 4 s at 16 kHz. This is harder than real
performances and contains no drums and no gender labels for vocals. Drum recognition (model picks the
family, measurements pick the piece) is tested separately: see Documentation/AI-Models.md.

| Family / source | Expected | Notes | Family top-1 correct | Detected (confident) | Correct when detected | Ambiguous (contains expected) | Unknown | Wrong detections |
|---|---|---:|---:|---:|---:|---:|---:|---|
| bass / electronic | electric_bass | 100 | 47% | 0% | - | 95% (83%) | 5% |  |
| bass / synthetic | synth | 100 | 56% | 4% | 100% | 46% (63%) | 50% |  |
| brass / acoustic | brass | 100 | 51% | 13% | 100% | 54% (67%) | 33% |  |
| flute / acoustic | acoustic_other | 55 | 78% | 31% | 100% | 47% (85%) | 22% |  |
| guitar / acoustic | acoustic_guitar | 100 | 36% | 1% | 0% | 57% (21%) | 42% | synth (1) |
| guitar / electronic | electric_guitar | 100 | 27% | 0% | - | 55% (53%) | 45% |  |
| keyboard / acoustic | piano | 100 | 16% | 0% | - | 57% (16%) | 43% |  |
| keyboard / electronic | piano | 100 | 37% | 3% | 0% | 53% (13%) | 44% | synth (3) |
| mallet / acoustic | percussion | 100 | 85% | 0% | - | 37% (0%) | 63% |  |
| organ / electronic | organ | 100 | 37% | 2% | 0% | 61% (0%) | 37% | synth (1), brass (1) |
| reed / acoustic | acoustic_other | 100 | 29% | 0% | - | 58% (38%) | 42% |  |
| string / acoustic | strings | 100 | 36% | 2% | 50% | 79% (53%) | 19% | acoustic_bass (1) |
| vocal / acoustic | vocal | 22 | 64% | 9% | 100% | 59% (85%) | 32% |  |
| **All** | | 1177 | 44% | 4% | 84% | 59% (43%) | 38% | |

