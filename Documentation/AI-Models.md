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

`Models/instrument_taxonomy.json` maps AudioSet classes to EyraEQ's categories with weights (for
example *Snare drum* 1.0, *Rimshot* 0.9, *Drum roll* 0.6 for the snare category). The same file is
read by the plug-in and by the validation scripts, so both use identical logic.

1. Resample to 16 kHz (anti-aliased), compute log-mel patches (0.96 s every 0.48 s).
2. Keep patches above -60 dBFS and within 40 dB of the loudest one.
3. Category evidence = mean over patches of the strongest weighted class score.
4. Decision (thresholds in the taxonomy file):
   - under 3 s of signal: **more audio needed**
   - best evidence under `presentScore`: **not recognised**
   - best at or above `confidentScore` *and* `dominanceRatio` times the runner-up: **detected**
     (high or medium confidence)
   - otherwise: **not certain**, with up to three plausible candidates
5. Refinements the model cannot make, using the measured pitch from the frequency analysis. They are
   labelled as such in the UI:
   - male or female vocal: only when the pitch range is clearly in one range (YAMNet has no
     gender classes); otherwise "Vocal"
   - rack or floor tom: from the tom's pitch (split at 110 Hz)
   - synth bass: synthesizer with a median pitch below 140 Hz
   - clean or distorted electric guitar: from YAMNet's *Distortion* class

### Calibration and measured accuracy

Thresholds are tuned on NSynth's **valid** split (`Scripts/models/tune_thresholds.py`) for at least
90% precision of "detected", then reported once on the **test** split. Full table:
[Validation/nsynth-validation.md](Validation/nsynth-validation.md).

| NSynth test split, 1,177 single notes | Result |
|---|---|
| Correct when "detected" | **92%** |
| Detected with confidence | 7% |
| "Not certain" (candidate list contains the right instrument) | 61% (49%) |
| Not recognised | 32% |
| Top-ranked category correct (not shown to users as a fact) | 36% |

What this means: when EyraEQ says it has detected an instrument, it is usually right. But on
isolated single notes it rarely commits, and offers candidates instead. YAMNet is strongest on flute,
brass, vocals and electric bass, and weak on piano, organ and acoustic guitar notes (often heard as
percussion or electric guitar). Real performances are longer and richer than single notes, but that
has **not been measured yet**.

### Not yet validated

- **Drums** (kick, snare, toms, cymbals, overheads): NSynth has no drums, and suitable openly
  licensed multitrack drum datasets are either non-commercial or very large. The drum categories map
  directly to AudioSet classes, but their accuracy is unmeasured.
- **Vocal gender** and **rack / floor tom** refinements are rule-based on pitch and unvalidated on real
  recordings.
- **Real multitrack stems** in general.

Improving this is the main open item. The next step would be a small classifier head trained on YAMNet
embeddings with an openly licensed stem dataset; the training and evaluation pipeline above is
designed so that such a model drops in as an update.

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
