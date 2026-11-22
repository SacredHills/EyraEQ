# Instrument recognition: NSynth validation (test split)

Model: yamnet 1.0.0 (sha256 e088acd8d281...). Taxonomy: instrument_taxonomy.json. Up to 100 random notes per group (seed 1).
Decision thresholds: present 0.12; family confident 0.25, dominance 1.6; member confident 0.2, dominance 2.0.

NSynth (Google Magenta, CC BY 4.0): isolated single notes, 4 s at 16 kHz. This is harder than real
performances and contains no drums and no gender labels for vocals. Drum recognition (model picks the
family, measurements pick the piece) is tested separately: see Documentation/AI-Models.md.

| Family / source | Expected | Notes | Family top-1 correct | Detected (confident) | Correct when detected | Ambiguous (contains expected) | Unknown | Wrong detections |
|---|---|---:|---:|---:|---:|---:|---:|---|
| bass / electronic | electric_bass | 100 | 47% | 7% | 71% | 81% (74%) | 12% | drums (2) |
| bass / synthetic | synth | 100 | 56% | 19% | 100% | 26% (42%) | 55% |  |
| brass / acoustic | brass | 100 | 51% | 42% | 79% | 21% (71%) | 37% | acoustic_other (7), synth (2) |
| flute / acoustic | acoustic_other | 55 | 78% | 64% | 91% | 15% (88%) | 22% | brass (3) |
| guitar / acoustic | acoustic_guitar | 100 | 36% | 16% | 0% | 29% (24%) | 55% | synth (12), percussion (3) |
| guitar / electronic | electric_guitar | 100 | 27% | 10% | 0% | 29% (59%) | 61% | synth (5), electric_bass (4) |
| keyboard / acoustic | piano | 100 | 16% | 3% | 33% | 29% (17%) | 68% | electric_bass (1), drums (1) |
| keyboard / electronic | piano | 100 | 37% | 13% | 15% | 32% (6%) | 55% | synth (7), percussion (2) |
| mallet / acoustic | percussion | 100 | 85% | 13% | 62% | 16% (0%) | 71% | drums (5) |
| organ / electronic | organ | 100 | 37% | 16% | 0% | 32% (0%) | 52% | synth (9), acoustic_other (4) |
| reed / acoustic | acoustic_other | 100 | 29% | 17% | 29% | 26% (42%) | 57% | synth (7), brass (5) |
| string / acoustic | strings | 100 | 36% | 23% | 26% | 42% (52%) | 35% | acoustic_bass (17) |
| vocal / acoustic | vocal | 22 | 64% | 50% | 100% | 18% (50%) | 32% |  |
| **All** | | 1177 | 44% | 19% | 54% | 32% (42%) | 49% | |

