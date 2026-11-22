# Instrument recognition: models (Phase 3)

EyraEQ recognises the instrument on a track with a local model. Nothing is sent over the network.
Recognition runs after an analysis capture (or on an analysed file), on the analysis worker thread.
It is a suggestion: the user can always choose the instrument manually, and the EQ works the same
whether or not the model is available.

## The model

| | |
|---|---|
| Model | **YAMNet** (MobileNet v1, 3.7 M parameters), Google, trained on AudioSet (521 classes) |
| Licence | Apache License 2.0 (weights and code); class names CC BY 4.0 (AudioSet ontology) |
| Source | Official weights `storage.googleapis.com/audioset/yamnet.h5`, official code `tensorflow/models` at commit `8f3b2e4` (pinned; SHA-256 of every upstream file recorded in `Models/yamnet/manifest.json`) |
| Format | ONNX (opset 17), network only: input log-mel patches `[N, 96, 64]`, outputs 521 class scores and 1024-d embeddings; 14.9 MB |
| Runtime | ONNX Runtime 1.32.0 (MIT), Microsoft's official build, loaded at run time from inside the plug-in bundle. Available for Windows x64, Linux x64 and macOS arm64 (14+); not for Intel Macs |

YAMNet was chosen because its licence permits commercial use, it is small and fast on a CPU, and
AudioSet has classes for most of the instruments in the specification. Models with non-commercial
licences (for example Essentia's) and datasets with non-commercial terms (IRMAS, MedleyDB) were not
used.

### How the ONNX file is produced and verified

`Scripts/models/build_yamnet_onnx.py`, run by `.github/workflows/model.yml`:

1. Downloads the official code and weights and records their checksums.
2. Builds Google's reference TensorFlow model (waveform in, scores out).
3. Builds the network part only, copies the weights layer by layer (shapes checked), and exports it to ONNX.
4. Exports TensorFlow's exact mel filterbank (`mel_matrix.f32`), so the plug-in's front end is identical.
5. Verifies that a NumPy copy of the plug-in's front end feeding the ONNX model reproduces the TensorFlow
   reference: log-mel error 2.3e-5, score error 7.7e-7.
6. Writes `manifest.json` (checksums, input/output names, front-end parameters, labels, provenance),
   `NOTICE` and reference vectors.

The workflow commits the verified folder to the branch (`[publish-model]` in a commit message, or a
manual run). The plug-in's own C++ front end and ONNX Runtime are checked against the same reference
vectors in the unit tests (log-mel within 8e-6, scores within 1e-6) and by a **self-test at load time**:
if the scores differ from the reference, the model is not used.

## From model scores to an instrument

`Models/instrument_taxonomy.json` maps AudioSet classes to EyraEQ's instrument **families** (drums,
bass, guitar, vocal, keys, strings and brass, other acoustic) and **categories** within them (for
example *Snare drum* 1.0, *Rimshot* 0.9 for the snare). Generic labels such as *Percussion*, *Guitar*
or *Keyboard* count towards a family only. The same file is read by the plug-in and by the validation
scripts (`Scripts/models/taxonomy.py` mirrors the decision).

1. Resample to 16 kHz (anti-aliased), compute log-mel patches (0.96 s every 0.48 s).
2. Keep patches above -60 dBFS and within 40 dB of the loudest one.
3. Evidence = mean over patches of the strongest weighted class score, per family and per category.
   Generic impact sounds (*Knock*, *Thump*, *Whack*, *Tap*) count towards drums only when the analysis
   measured regular hits.
4. **Family stage** (thresholds in the taxonomy file):
   - under 3 s of signal: **more audio needed**
   - best family under `presentScore`: **not recognised** (but see the drum fallback below)
   - best family under `familyConfidentScore`, or less than `familyDominanceRatio` times the
     runner-up: **not certain**, with the likeliest instrument of each plausible family
5. **Instrument stage**:
   - pitched families: the best category must reach `memberConfidentScore` and be
     `memberDominanceRatio` times both the next member and the strongest category of any other
     family; otherwise **not certain** among the members.
   - drums: see below.
6. Refinements the model cannot make, from the measured pitch and YAMNet's *Distortion* class,
   labelled as such in the UI: male/female vocal (only when the pitch range is clearly one or the
   other; YAMNet has no gender classes), synth bass (synthesizer below 140 Hz median pitch), clean or
   distorted electric guitar.

### Drums

YAMNet recognises drum kits and cymbals well, but heard alone, a kick is often "Knock" or "Door" and
a snare "Sound effect"; it has no tom classes. So the model decides *that* it is drums, and the
frequency analysis decides *which* drum, from measurements shown with the result:

| Measurement | Decides |
|---|---|
| ≥ 40% of energy above 5 kHz, or a centroid above 2.5 kHz with < 5% below 150 Hz | cymbals / hi-hat |
| ≥ 20% below 150 Hz **and** ≥ 20% above 5 kHz | full kit (overheads) |
| ≥ 50% below 150 Hz | kick (decay < 220 ms) or floor tom (longer) |
| otherwise | snare (noisy: spectral flatness ≥ 0.1) or tom (tonal; rack/floor by pitch, 110 Hz) |

- Values within 25% of the decay threshold or 35% of the noisiness threshold give **both**
  possibilities instead of a guess.
- A piece is only stated as detected when the analysis also measured regular hits; it is medium
  confidence, high only when the model independently names the same piece (e.g. *Hi-hat*).
- The model's hand-percussion and mallet classes (wood block, marimba, ...) give "Percussion" when
  they clearly win, under the same thresholds as other instruments.
- If the model recognises nothing at all but regular hits were measured and the measurements point
  clearly to a drum, it is offered as **possibly** that drum ("from the measurements only"), never as
  a detection.

Measured on 30 drum-machine and sampled-kit one-shots (5 kits × kick, snare, hi-hat, 3 toms; looped
every 0.5 s for 8 s) with `EyraEQAnalyze`, the plug-in's own pipeline:

| 30 drum loops | Result |
|---|---|
| Detected | 14, all correct |
| Not certain, right drum among the possibilities | 15 (8 of them from the measurements only) |
| Not certain, "Drums" without a piece | 1 |
| Wrong | 0 |

**Caveat:** these samples were also used to choose the thresholds, and they cannot be redistributed
(no licence), so they are not in the repository. This shows the method works, not its accuracy on
your recordings. The rules are also tested in CI on synthetic drums through the real analysis.
Mixed drum buses or bleed-heavy close mics are not measured.

### Calibration and measured accuracy (pitched instruments)

Thresholds are tuned on NSynth's **valid** split (`Scripts/models/tune_thresholds.py`) for at least
90% precision of "detected", then reported once on the **test** split. Full table:
[Validation/nsynth-validation.md](Validation/nsynth-validation.md).

| NSynth, single notes | Valid split | Test split |
|---|---|---|
| Correct when "detected" | 92% | **84%** (44 notes) |
| Detected with confidence | 5% | 4% |
| "Not certain" (right instrument among candidates) | 58% (46%) | 59% (43%) |
| Not recognised | 37% | 38% |

What this means: when EyraEQ says it has detected a pitched instrument it is usually, not always,
right; on isolated single notes it mostly offers candidates instead. The precision on the test split
is lower than on the split the thresholds were tuned on, and with 44 detections it is only a rough
figure. YAMNet is strongest on flute, brass, vocals and synth bass, and weak on piano, organ and
guitar notes (often heard as synthesizer or percussion). Real performances are longer and richer than
single notes, but that has **not been measured yet**.

### Not yet validated

- **Real multitrack stems** in general, including vocal gender.
- **Drums** beyond the 30 local samples above.

Improving this is the main open item. The next step would be a small classifier head trained on YAMNet
embeddings with an openly licensed stem dataset; the training and evaluation pipeline above is
designed so that such a model drops in as an update.

### Checking your own audio

`EyraEQAnalyze` (built with the tests) prints what the plug-in would show for audio files, with the
measurements and the model's top classes:

```
EYRA_ONNXRUNTIME_LIBRARY=/path/to/libonnxruntime.so EYRA_MODEL_DIR=Models \
  build/linux-release/EyraEQAnalyze_artefacts/Release/EyraEQAnalyze kick.wav snare.wav
```

## Model updates

A newer model is installed by placing its folder (with `manifest.json`) in the user model folder:

| OS | Folder |
|---|---|
| Windows | `%APPDATA%\SacredHills\EyraEQ\Models\yamnet\` |
| macOS | `~/Library/SacredHills/EyraEQ/Models/yamnet/` |
| Linux | `~/.config/SacredHills/EyraEQ/Models/yamnet/` |

The highest valid version wins, from the user folder or the bundle. A model is valid only if the
manifest is complete, all checksums match and the load-time self-test passes; otherwise the next
candidate is used. A newer `instrument_taxonomy.json` can be placed in `Models/` the same way. There is
no automatic download.

## Failure behaviour

| Situation | What happens |
|---|---|
| ONNX Runtime missing, too old or not loadable | "Instrument recognition unavailable" with the reason; analysis and EQ work normally |
| Model missing, damaged (checksum) or failing the self-test | Same, with the reason; a valid bundled model is used if an update is damaged |
| Too little audio | "More audio needed" |
| Evidence too weak or too close | "Not recognised" / "Not certain", never a guess presented as fact |

The model is loaded once per process, on first use, and shared by all plug-in instances.
Inference uses one thread at low priority, off the audio thread.
