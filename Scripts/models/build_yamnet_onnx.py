#!/usr/bin/env python3
"""Builds Models/yamnet from Google's published YAMNet (Apache-2.0) and verifies it.

Runs in CI (.github/workflows/model.yml); it needs internet access to Google's servers.

Steps
  1. Download the YAMNet code and class map (tensorflow/models, pinned commit) and the official
     weights (yamnet.h5), and record their SHA-256.
  2. Build the reference TensorFlow model (waveform -> scores) with the official code.
  3. Build the network part only (log-mel patches -> scores, embeddings), copy the weights, and
     export it to ONNX.
  4. Export the exact mel filterbank TensorFlow uses, so the C++ front end is identical.
  5. Verify: a NumPy re-implementation of the front end (what the plug-in does in C++) feeding the
     ONNX model must reproduce the TensorFlow reference scores.
  6. Write manifest.json, NOTICE and reference vectors used by the C++ tests.

Usage: python build_yamnet_onnx.py <output_dir>
"""
import hashlib
import json
import sys
import urllib.request
from pathlib import Path

import numpy as np

YAMNET_COMMIT = "8f3b2e48fff92a7fa8d504908b51a6eb51e868a0"
CODE_URL = f"https://raw.githubusercontent.com/tensorflow/models/{YAMNET_COMMIT}/research/audioset/yamnet/"
WEIGHTS_URL = "https://storage.googleapis.com/audioset/yamnet.h5"
MODEL_VERSION = "1.0.0"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def download(url: str, dest: Path) -> None:
    print(f"downloading {url}")
    with urllib.request.urlopen(url, timeout=120) as r:
        dest.write_bytes(r.read())


def reference_waveform(sr=16000, seconds=3.0):
    """Deterministic test signal: tones, a chirp, decaying hits and noise (LCG, reproducible)."""
    n = int(sr * seconds)
    t = np.arange(n) / sr
    x = 0.3 * np.sin(2 * np.pi * 220 * t) + 0.15 * np.sin(2 * np.pi * 1250 * t + 0.3)
    x += 0.2 * np.sin(2 * np.pi * (300 * t + 900 * t * t / seconds))
    state = 12345
    noise = np.empty(n)
    for i in range(n):
        state = (1103515245 * state + 12345) % (2 ** 31)
        noise[i] = state / 2 ** 31 - 0.5
    hits = np.zeros(n)
    for start in range(0, n, sr // 2):
        k = np.arange(n - start)
        hits[start:] += 0.6 * noise[: n - start] * np.exp(-k / (0.03 * sr))
    return (x + 0.05 * noise + hits).astype(np.float32) * 0.5


def numpy_log_mel(waveform, mel_matrix, params):
    """The plug-in's front end, in NumPy (mirrors Source/Intelligence/FeatureExtractor.cpp)."""
    win, hop, nfft = 400, 160, 512
    window = (0.5 - 0.5 * np.cos(2 * np.pi * np.arange(win) / win)).astype(np.float64)  # periodic Hann
    frames = 1 + (len(waveform) - win) // hop
    out = np.empty((frames, mel_matrix.shape[1]), dtype=np.float64)
    for f in range(frames):
        seg = waveform[f * hop: f * hop + win].astype(np.float64) * window
        mag = np.abs(np.fft.rfft(seg, nfft))
        out[f] = np.log(mag @ mel_matrix + params.log_offset)
    patches = [out[i: i + 96] for i in range(0, frames - 96 + 1, 48)]
    return out.astype(np.float32), np.stack(patches).astype(np.float32)


def pad_like_yamnet(waveform, params):
    sr = int(params.sample_rate)
    min_samples = int((params.patch_window_seconds + params.stft_window_seconds - params.stft_hop_seconds) * sr)
    n = len(waveform)
    pad = max(0, min_samples - n)
    n = max(n, min_samples)
    hop = int(params.patch_hop_seconds * sr)
    after = n - min_samples
    hops = int(np.ceil(after / hop))
    pad += hop * hops - after
    return np.pad(waveform, (0, pad))


def main(out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    work = Path("yamnet_src")
    work.mkdir(exist_ok=True)
    for name in ("yamnet.py", "params.py", "features.py", "yamnet_class_map.csv"):
        download(CODE_URL + name, work / name)
    download(WEIGHTS_URL, work / "yamnet.h5")
    upstream = {name: sha256(work / name) for name in ("yamnet.py", "params.py", "features.py", "yamnet_class_map.csv", "yamnet.h5")}

    sys.path.insert(0, str(work))
    import tensorflow as tf  # noqa: E402
    import tf_keras  # noqa: E402  (the upstream YAMNet code uses the Keras 2 package)
    import params as yamnet_params  # noqa: E402
    import yamnet as yamnet_lib  # noqa: E402

    params = yamnet_params.Params()
    labels = list(yamnet_lib.class_names(str(work / "yamnet_class_map.csv")))
    assert len(labels) == params.num_classes

    # Reference model (official code, official weights)
    reference = yamnet_lib.yamnet_frames_model(params)
    reference.load_weights(str(work / "yamnet.h5"))

    # Network only: log-mel patches -> (scores, embeddings)
    patches_in = tf_keras.layers.Input(shape=(params.patch_frames, params.patch_bands), name="log_mel_patches")
    scores, embeddings = yamnet_lib.yamnet(patches_in, params)
    net = tf_keras.Model(inputs=patches_in, outputs=[scores, embeddings], name="yamnet_patches")
    ref_layers = [l for l in reference.layers if l.weights]
    net_layers = [l for l in net.layers if l.weights]
    assert len(ref_layers) == len(net_layers), (len(ref_layers), len(net_layers))
    for a, b in zip(ref_layers, net_layers):
        for wa, wb in zip(a.get_weights(), b.get_weights()):
            assert wa.shape == wb.shape, (a.name, b.name, wa.shape, wb.shape)
        b.set_weights(a.get_weights())

    # Exact mel filterbank used by TensorFlow
    mel = tf.signal.linear_to_mel_weight_matrix(
        num_mel_bins=params.mel_bands, num_spectrogram_bins=257, sample_rate=params.sample_rate,
        lower_edge_hertz=params.mel_min_hz, upper_edge_hertz=params.mel_max_hz).numpy().astype(np.float32)
    (out_dir / "mel_matrix.f32").write_bytes(mel.astype("<f4").tobytes())

    # ONNX export
    import tf2onnx  # noqa: E402
    spec = (tf.TensorSpec((None, params.patch_frames, params.patch_bands), tf.float32, name="log_mel_patches"),)
    onnx_model, _ = tf2onnx.convert.from_keras(net, input_signature=spec, opset=17,
                                              output_path=str(out_dir / "yamnet.onnx"))
    output_names = [o.name for o in onnx_model.graph.output]
    input_name = onnx_model.graph.input[0].name
    print("ONNX input", input_name, "outputs", output_names)

    import onnxruntime as ort  # noqa: E402
    session = ort.InferenceSession(str(out_dir / "yamnet.onnx"), providers=["CPUExecutionProvider"])
    out_shapes = {o.name: o.shape for o in session.get_outputs()}
    scores_name = next(n for n, s in out_shapes.items() if s[-1] == params.num_classes)
    embed_name = next(n for n, s in out_shapes.items() if s[-1] == 1024)

    # Verification on the deterministic waveform
    wave = reference_waveform()
    ref_scores, _, ref_logmel = [t.numpy() for t in reference(wave)]
    padded = pad_like_yamnet(wave, params)
    np_logmel, np_patches = numpy_log_mel(padded, mel.astype(np.float64), params)
    onnx_scores = session.run([scores_name], {input_name: np_patches})[0]

    logmel_err = float(np.max(np.abs(np_logmel - ref_logmel)))
    score_err = float(np.max(np.abs(onnx_scores - ref_scores)))
    print(f"log-mel max abs error {logmel_err:.2e}, score max abs error {score_err:.2e}")
    assert np_patches.shape[0] == ref_scores.shape[0]
    assert logmel_err < 1e-3, logmel_err
    assert score_err < 1e-3, score_err

    # Reference vectors for the C++ tests
    ref_dir = out_dir / "reference"
    ref_dir.mkdir(exist_ok=True)
    (ref_dir / "waveform_16k.f32").write_bytes(wave.astype("<f4").tobytes())
    (ref_dir / "patches.f32").write_bytes(np_patches.astype("<f4").tobytes())
    (ref_dir / "scores.f32").write_bytes(ref_scores.astype("<f4").tobytes())

    # Model card for the classes most relevant to instruments
    top = np.argsort(-ref_scores.mean(axis=0))[:5]
    print("reference waveform top classes:", [(labels[i], round(float(ref_scores.mean(axis=0)[i]), 3)) for i in top])

    manifest = {
        "name": "yamnet",
        "version": MODEL_VERSION,
        "task": "audio-event-classification",
        "description": "YAMNet (MobileNet v1) audio event classifier trained on AudioSet; network part only (log-mel patches in).",
        "license": "Apache-2.0",
        "labelsLicense": "AudioSet ontology, CC BY 4.0",
        "source": {
            "code": CODE_URL,
            "weights": WEIGHTS_URL,
            "tensorflowModelsCommit": YAMNET_COMMIT,
            "upstreamSha256": upstream,
        },
        "file": "yamnet.onnx",
        "sha256": sha256(out_dir / "yamnet.onnx"),
        "input": {"name": input_name, "shape": [-1, params.patch_frames, params.patch_bands]},
        "outputs": {"scores": scores_name, "embeddings": embed_name},
        "frontend": {
            "sampleRate": 16000, "windowSamples": 400, "hopSamples": 160, "fftSize": 512,
            "window": "periodic-hann", "melBands": params.mel_bands, "melMatrix": "mel_matrix.f32",
            "melMatrixSha256": sha256(out_dir / "mel_matrix.f32"), "logOffset": params.log_offset,
            "patchFrames": params.patch_frames, "patchHopFrames": 48,
        },
        "reference": {
            "waveform": "reference/waveform_16k.f32", "patches": "reference/patches.f32",
            "scores": "reference/scores.f32", "numPatches": int(np_patches.shape[0]),
            "tolerance": 2e-3, "verifiedLogMelError": logmel_err, "verifiedScoreError": score_err,
        },
        "labels": labels,
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")
    (out_dir / "NOTICE").write_text(
        "YAMNet model weights and code: Copyright Google LLC, licensed under the Apache License 2.0\n"
        "(https://www.apache.org/licenses/LICENSE-2.0). Converted to ONNX by Scripts/models/build_yamnet_onnx.py;\n"
        "the network is unchanged.\n"
        "Class names: AudioSet ontology (https://research.google.com/audioset/), Google, CC BY 4.0.\n")
    print("wrote", out_dir)


if __name__ == "__main__":
    main(Path(sys.argv[1] if len(sys.argv) > 1 else "Models/yamnet"))
