#!/usr/bin/env python3
"""Validates instrument recognition on the NSynth test set (Google Magenta, CC BY 4.0).

Uses the same front end, ONNX model and taxonomy/decision logic as the plug-in and writes a Markdown
report with per-family results. NSynth contains single notes (4 s, 16 kHz), which is harder than a
real performance, and has no drums or vocals of known gender: those are not covered here.

Usage: python validate_nsynth.py <model_dir> <taxonomy.json> <report.md> [examples_per_group]
"""
import json
import random
import sys
import tarfile
import urllib.request
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np
import onnxruntime as ort
import soundfile as sf

sys.path.insert(0, str(Path(__file__).parent))
from taxonomy import Taxonomy  # noqa: E402

NSYNTH_URL = "http://download.magenta.tensorflow.org/datasets/nsynth/nsynth-test.jsonwav.tar.gz"

# NSynth (family, source) -> expected EyraEQ category. Synthetic sources are mostly excluded because
# their "instrument" is a synthesiser imitating something else.
EXPECTED = {
    ("guitar", "acoustic"): "acoustic_guitar",
    ("guitar", "electronic"): "electric_guitar",
    ("bass", "acoustic"): "acoustic_bass",
    ("bass", "electronic"): "electric_bass",
    ("bass", "synthetic"): "synth",
    ("keyboard", "acoustic"): "piano",
    ("keyboard", "electronic"): "piano",
    ("brass", "acoustic"): "brass",
    ("string", "acoustic"): "strings",
    ("organ", "electronic"): "organ",
    ("flute", "acoustic"): "acoustic_other",
    ("reed", "acoustic"): "acoustic_other",
    ("mallet", "acoustic"): "percussion",
    ("vocal", "acoustic"): "vocal",
    ("synth_lead", "synthetic"): "synth",
}


def log_mel_patches(wave, mel, log_offset):
    win, hop, nfft = 400, 160, 512
    window = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(win) / win)
    frames = 1 + (len(wave) - win) // hop
    out = np.empty((frames, mel.shape[1]))
    for f in range(frames):
        mag = np.abs(np.fft.rfft(wave[f * hop: f * hop + win] * window, nfft))
        out[f] = np.log(mag @ mel + log_offset)
    return np.stack([out[i: i + 96] for i in range(0, frames - 96 + 1, 48)]).astype(np.float32)


def main(model_dir: Path, taxonomy_path: Path, report: Path, per_group: int):
    manifest = json.loads((model_dir / "manifest.json").read_text())
    fe = manifest["frontend"]
    mel = np.frombuffer((model_dir / fe["melMatrix"]).read_bytes(), dtype="<f4").reshape(257, fe["melBands"]).astype(np.float64)
    session = ort.InferenceSession(str(model_dir / manifest["file"]), providers=["CPUExecutionProvider"])
    taxonomy = Taxonomy(taxonomy_path, manifest["labels"])

    archive = Path("nsynth-test.tar.gz")
    if not archive.exists():
        print("downloading NSynth test set")
        urllib.request.urlretrieve(NSYNTH_URL, archive)
    root = Path("nsynth-test")
    if not root.exists():
        with tarfile.open(archive) as t:
            t.extractall(".")
    examples = json.loads((root / "examples.json").read_text())

    groups = defaultdict(list)
    for name, ex in examples.items():
        key = (ex["instrument_family_str"], ex["instrument_source_str"])
        if key in EXPECTED:
            groups[key].append(name)

    rng = random.Random(1)
    rows, totals = [], Counter()
    for key in sorted(groups):
        names = sorted(groups[key])
        rng.shuffle(names)
        names = names[:per_group]
        counts = Counter()
        confusions = Counter()
        for name in names:
            wave, sr = sf.read(root / "audio" / f"{name}.wav", dtype="float32")
            assert sr == 16000
            patches = log_mel_patches(wave.astype(np.float64), mel, fe["logOffset"])
            scores = session.run([manifest["outputs"]["scores"]], {manifest["input"]["name"]: patches})[0]
            active = taxonomy.active_patches(wave, len(patches))
            active_seconds = 0.96 + 0.48 * (int(active.sum()) - 1) if active.any() else 0.0
            evidence, _ = taxonomy.evidence(scores, active)
            # Single notes are short; judge them on whatever is active (the plug-in requires 3 s).
            status, cands = taxonomy.decide(evidence, max(active_seconds, taxonomy.decision["minActiveSeconds"]))
            expected = EXPECTED[key]
            top = max(evidence, key=evidence.get)
            counts["n"] += 1
            counts["top1"] += top == expected
            counts[status] += 1
            if status == "detected":
                counts["detected_correct"] += cands[0] == expected
            if status == "ambiguous":
                counts["ambiguous_contains"] += expected in cands
            if top != expected:
                confusions[top] += 1
        totals.update(counts)
        rows.append((key, counts, confusions.most_common(2)))

    def pct(a, b):
        return f"{100.0 * a / b:.0f}%" if b else "-"

    lines = [
        "# Instrument recognition: NSynth validation",
        "",
        f"Model: {manifest['name']} {manifest['version']} (sha256 {manifest['sha256'][:12]}...). "
        f"Taxonomy: {taxonomy_path.name}. Up to {per_group} random notes per group (seed 1).",
        "",
        "NSynth test set (Google Magenta, CC BY 4.0): isolated single notes, 4 s at 16 kHz. This is harder than",
        "real performances and contains no drums and no gender labels for vocals.",
        "",
        "| Family / source | Expected | Notes | Top-1 correct | Detected (confident) | Correct when detected | Ambiguous (contains expected) | Unknown | Most common confusions |",
        "|---|---|---:|---:|---:|---:|---:|---:|---|",
    ]
    for key, c, conf in rows:
        det = c["detected"]
        lines.append(
            f"| {key[0]} / {key[1]} | {EXPECTED[key]} | {c['n']} | {pct(c['top1'], c['n'])} | {pct(det, c['n'])} | "
            f"{pct(c['detected_correct'], det)} | {pct(c['ambiguous'], c['n'])} ({pct(c['ambiguous_contains'], c['ambiguous'])}) | "
            f"{pct(c['unknown'], c['n'])} | {', '.join(f'{k} ({v})' for k, v in conf)} |")
    t = totals
    lines += [
        f"| **All** | | {t['n']} | {pct(t['top1'], t['n'])} | {pct(t['detected'], t['n'])} | {pct(t['detected_correct'], t['detected'])} | "
        f"{pct(t['ambiguous'], t['n'])} ({pct(t['ambiguous_contains'], t['ambiguous'])}) | {pct(t['unknown'], t['n'])} | |",
        "",
    ]
    report.write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main(Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]), int(sys.argv[4]) if len(sys.argv) > 4 else 60)
