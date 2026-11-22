"""Python mirror of Source/Intelligence/InstrumentClassifier.cpp (evidence and two-stage decision).

Both read Models/instrument_taxonomy.json. Keep the two in sync.

Not mirrored here: the drum-piece rules (they need the plug-in's audio measurements, which NSynth's
single notes cannot exercise; they are tested in C++ on synthetic drums) and the pitch/distortion
refinements. A drums-family result is reported as "drums" unless the model's hand-percussion classes win.
"""
import json
from pathlib import Path

import numpy as np


def _pairs(classes, index, owner):
    out = []
    for name, weight in classes.items():
        if name not in index:
            raise ValueError(f"unknown class '{name}' in {owner}")
        out.append((index[name], float(weight)))
    return out


class Taxonomy:
    def __init__(self, path: Path, labels):
        data = json.loads(Path(path).read_text())
        self.decision = data["decision"]
        self.drum_rules = data.get("drumRules", {})
        self.refinements = data["refinements"]
        index = {name: i for i, name in enumerate(labels)}
        self.families = [(f["id"], _pairs(f["classes"], index, f["id"]), _pairs(f.get("percussiveClasses", {}), index, f["id"]))
                         for f in data["families"]]
        self.categories = [(c["id"], c["family"], _pairs(c["classes"], index, c["id"])) for c in data["categories"]]
        self.family_of = {cid: fam for cid, fam, _ in self.categories}
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

    def evidence(self, scores, active, percussive=False):
        """Mean over active patches of the strongest weighted class score, per family and per category."""
        s = scores[active]

        def mean_max(pairs):
            if not pairs or not len(s):
                return 0.0
            return float(np.max(np.stack([s[:, i] * w for i, w in pairs]), axis=0).mean())

        families = {fid: mean_max(pairs + (perc if percussive else [])) for fid, pairs, perc in self.families}
        categories = {cid: mean_max(pairs) for cid, _, pairs in self.categories}
        distortion = float(s[:, self.distortion_index].mean()) if len(s) else 0.0
        return families, categories, distortion

    def decide(self, families, categories, active_seconds, d=None):
        """Returns (status, candidates). Candidates are category ids, or "drums" for a drums-family result
        whose piece the plug-in decides from measurements."""
        d = d or self.decision
        if active_seconds < d["minActiveSeconds"]:
            return "need-more-audio", []
        ranked = sorted(families.items(), key=lambda kv: -kv[1])
        (best, fb), fs = ranked[0], (ranked[1][1] if len(ranked) > 1 else 0.0)
        if fb < d["presentScore"]:
            return "unknown", []

        def members(fam):
            return sorted([c for c, f in self.family_of.items() if f == fam], key=lambda c: -categories[c])

        def representative(fam):
            return "drums" if fam == "drums" else members(fam)[0]

        if not (fb >= d["familyConfidentScore"] and fb >= d["familyDominanceRatio"] * max(fs, 1e-9)):
            cands = [representative(f) for f, v in ranked
                     if v >= d["presentScore"] and v >= d["candidateRatio"] * fb][: d["maxCandidates"]]
            return "ambiguous", cands

        if best == "drums":
            # Hand percussion / mallets: the model's own classes, held to the member thresholds.
            perc = categories.get("percussion", 0.0)
            rival = max([categories.get(c, 0.0) for c in ("kick", "snare", "toms")]
                        + [v for c, v in categories.items() if self.family_of[c] != "drums"])
            if perc >= max(self.drum_rules.get("modelPercussionScore", 0.3), d["memberConfidentScore"]) and perc >= d["memberDominanceRatio"] * max(rival, 1e-9):
                return "detected", ["percussion"]
            # Kit pieces come from measurements and need measured regular hits, which single notes never have.
            return "ambiguous", ["drums"]

        m = members(best)
        mb = categories[m[0]]
        # Runner-up: the next member, or any other family's category (as strong evidence elsewhere means doubt).
        ms = max([categories[c] for c in m[1:]] + [v for c, v in categories.items() if self.family_of[c] != best] + [0.0])
        if mb >= d["memberConfidentScore"] and mb >= d["memberDominanceRatio"] * max(ms, 1e-9):
            return "detected", [m[0]]
        cands = [c for i, c in enumerate(m) if i == 0 or categories[c] >= d["candidateRatio"] * mb][: d["maxCandidates"]]
        return "ambiguous", cands


KIT_PIECES = {"kick", "snare", "toms", "cymbals", "overheads"}


def matches(expected, candidate):
    """A drums-family result counts as correct for an expected kit piece only (the piece itself comes from
    measurements, not covered here); hand percussion and mallets must be recognised as "percussion"."""
    return candidate == expected or (candidate == "drums" and expected in KIT_PIECES)
