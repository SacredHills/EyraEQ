"""Python mirror of Source/Intelligence/InstrumentClassifier.cpp (category evidence and decision).

Both read Models/instrument_taxonomy.json. Keep the two in sync; the C++ tests check the result for
the reference waveform against the value this module writes into the validation report.
"""
import json
from pathlib import Path

import numpy as np


class Taxonomy:
    def __init__(self, path: Path, labels):
        data = json.loads(Path(path).read_text())
        self.decision = data["decision"]
        self.refinements = data["refinements"]
        index = {name: i for i, name in enumerate(labels)}
        self.categories = []
        for c in data["categories"]:
            pairs = []
            for name, weight in c["classes"].items():
                if name not in index:
                    raise ValueError(f"unknown class '{name}' in category {c['id']}")
                pairs.append((index[name], float(weight)))
            self.categories.append((c["id"], c["name"], pairs))
        self.distortion_index = index[self.refinements["distortionClass"]]

    def active_patches(self, waveform_16k, num_patches):
        """Patches whose audio is above -60 dBFS and within 40 dB of the loudest patch."""
        levels = []
        for p in range(num_patches):
            start = p * 48 * 160
            seg = waveform_16k[start: start + 96 * 160 + 240]
            rms = np.sqrt(np.mean(np.square(seg.astype(np.float64)))) if len(seg) else 0.0
            levels.append(20 * np.log10(max(rms, 1e-10)))
        levels = np.array(levels)
        gate = max(-60.0, levels.max() - 40.0)
        return levels > gate

    def evidence(self, scores, active):
        """Mean over active patches of the strongest weighted class score per category."""
        s = scores[active]
        out = {}
        for cid, _, pairs in self.categories:
            per_patch = np.max(np.stack([s[:, i] * w for i, w in pairs]), axis=0)
            out[cid] = float(per_patch.mean()) if len(per_patch) else 0.0
        distortion = float(s[:, self.distortion_index].mean()) if len(s) else 0.0
        return out, distortion

    def decide(self, evidence, active_seconds):
        d = self.decision
        if active_seconds < d["minActiveSeconds"]:
            return "need-more-audio", []
        ranked = sorted(evidence.items(), key=lambda kv: -kv[1])
        (best, b), (_, second) = ranked[0], ranked[1]
        if b < d["presentScore"]:
            return "unknown", []
        if b >= d["confidentScore"] and b >= d["dominanceRatio"] * max(second, 1e-9):
            return "detected", [best]
        candidates = [cid for cid, v in ranked[: d["maxCandidates"]]
                      if v >= d["presentScore"] and v >= d["candidateRatio"] * b]
        return "ambiguous", candidates
