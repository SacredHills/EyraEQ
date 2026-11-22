#!/usr/bin/env python3
"""Chooses the decision thresholds from validation data (never from the test split).

Reads <validation_dir>/nsynth-valid-evidence.json (written by validate_nsynth.py) and searches the
family-stage thresholds (presentScore, familyConfidentScore, familyDominanceRatio) and the member-stage
thresholds (memberConfidentScore, memberDominanceRatio). It picks the setting that detects the most notes
correctly while "Detected" stays at least --precision correct on the valid split, then reports that
setting once on the test split.

The drum-piece rules are not tuned here (NSynth has no drums); see Documentation/AI-Models.md.

Usage: python tune_thresholds.py <validation_dir> <taxonomy.json> [--precision 0.9] [--write]
"""
import argparse
import itertools
import json
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from taxonomy import matches  # noqa: E402


class Mirror:
    """Just enough of taxonomy.Taxonomy.decide to run without the model's label list."""

    def __init__(self, taxonomy_json):
        self.family_of = {c["id"]: c["family"] for c in taxonomy_json["categories"]}
        self.members = {}
        for cid, fam in self.family_of.items():
            self.members.setdefault(fam, []).append(cid)
        self.perc_score = taxonomy_json.get("drumRules", {}).get("modelPercussionScore", 0.3)

    def decide(self, families, categories, d):
        ranked = sorted(families.items(), key=lambda kv: -kv[1])
        (best, fb), fs = ranked[0], (ranked[1][1] if len(ranked) > 1 else 0.0)
        if fb < d["presentScore"]:
            return "unknown", []

        def sorted_members(fam):
            return sorted(self.members[fam], key=lambda c: -categories[c])

        if not (fb >= d["familyConfidentScore"] and fb >= d["familyDominanceRatio"] * max(fs, 1e-9)):
            cands = [("drums" if f == "drums" else sorted_members(f)[0]) for f, v in ranked
                     if v >= d["presentScore"] and v >= d["candidateRatio"] * fb][: d["maxCandidates"]]
            return "ambiguous", cands
        if best == "drums":
            # Hand percussion / mallets: the model's own classes, held to the member thresholds.
            perc = categories.get("percussion", 0.0)
            rival = max([categories.get(c, 0.0) for c in ("kick", "snare", "toms")]
                        + [v for c, v in categories.items() if self.family_of[c] != "drums"])
            if perc >= max(self.perc_score, d["memberConfidentScore"]) and perc >= d["memberDominanceRatio"] * max(rival, 1e-9):
                return "detected", ["percussion"]
            # Kit pieces come from measurements and need measured regular hits, which single notes never have.
            return "ambiguous", ["drums"]
        m = sorted_members(best)
        mb = categories[m[0]]
        # Runner-up: the next member, or any other family's category (as strong evidence elsewhere means doubt).
        ms = max([categories[c] for c in m[1:]] + [v for c, v in categories.items() if self.family_of[c] != best] + [0.0])
        if mb >= d["memberConfidentScore"] and mb >= d["memberDominanceRatio"] * max(ms, 1e-9):
            return "detected", [m[0]]
        return "ambiguous", [c for i, c in enumerate(m) if i == 0 or categories[c] >= d["candidateRatio"] * mb][: d["maxCandidates"]]


def score(mirror, records, d):
    c = Counter()
    for r in records:
        status, cands = mirror.decide(r["families"], r["evidence"], d)
        c[status] += 1
        if status == "detected":
            c["correct"] += matches(r["expected"], cands[0])
        if status == "ambiguous":
            c["contains"] += any(matches(r["expected"], x) for x in cands)
    c["n"] = len(records)
    c["precision"] = c["correct"] / c["detected"] if c["detected"] else 0.0
    return c


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("validation_dir", type=Path)
    ap.add_argument("taxonomy", type=Path)
    ap.add_argument("--precision", type=float, default=0.9)
    ap.add_argument("--write", action="store_true", help="write the chosen thresholds into the taxonomy file")
    args = ap.parse_args()

    tax = json.loads(args.taxonomy.read_text())
    mirror = Mirror(tax)
    valid = json.loads((args.validation_dir / "nsynth-valid-evidence.json").read_text())
    test = json.loads((args.validation_dir / "nsynth-test-evidence.json").read_text())

    grid = {
        "presentScore": [0.08, 0.12, 0.15, 0.20],
        "familyConfidentScore": [0.20, 0.25, 0.30, 0.40, 0.50],
        "familyDominanceRatio": [1.2, 1.6, 2.0, 3.0, 4.0],
        "memberConfidentScore": [0.15, 0.20, 0.30, 0.40, 0.50, 0.60],
        "memberDominanceRatio": [1.5, 2.0, 3.0, 4.0, 5.0, 6.0],
    }
    base = dict(tax["decision"])
    best = None
    for values in itertools.product(*grid.values()):
        d = dict(base, **dict(zip(grid.keys(), values)))
        if d["familyConfidentScore"] < d["presentScore"]:
            continue
        s = score(mirror, valid, d)
        if s["detected"] < 10 or s["precision"] < args.precision:
            continue
        # Most correct detections; among equals, the most notes that end with the right answer offered.
        key = (s["correct"], s["contains"] + s["correct"])
        if best is None or key > best[0]:
            best = (key, d, s)

    if best is None:
        print(f"No setting reaches {args.precision:.0%} precision on the valid split.")
        return
    _, d, s = best
    t = score(mirror, test, d)

    def line(name, s):
        return (f"{name}: detected {s['detected']}/{s['n']} ({s['detected'] / s['n']:.0%}), correct when detected {s['precision']:.0%}, "
                f"ambiguous {s['ambiguous']} (contains expected {s['contains']}), unknown {s['unknown']}")

    print("chosen:", ", ".join(f"{k}={d[k]}" for k in grid))
    print(line("valid", s))
    print(line("test ", t))

    if args.write:
        tax["decision"].update({k: d[k] for k in grid})
        tax["decision"]["calibration"] = (f"Tuned on the NSynth valid split for >= {args.precision:.0%} precision of 'Detected' "
                                          f"(Scripts/models/tune_thresholds.py); test split: {t['precision']:.0%} correct when detected.")
        args.taxonomy.write_text(json.dumps(tax, indent=2) + "\n")
        print("wrote", args.taxonomy)


if __name__ == "__main__":
    main()
