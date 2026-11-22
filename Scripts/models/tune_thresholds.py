#!/usr/bin/env python3
"""Chooses the decision thresholds from validation data (never from the test split).

Reads Documentation/Validation/nsynth-valid-evidence.json (written by validate_nsynth.py), searches
presentScore / confidentScore / dominanceRatio, and picks the setting that detects the most notes
correctly while "Detected" stays at least --precision correct on the valid split. Then it reports
the chosen setting once on the test split.

Usage: python tune_thresholds.py <validation_dir> <taxonomy.json> [--precision 0.9] [--write]
"""
import argparse
import itertools
import json
from collections import Counter
from pathlib import Path


def decide(evidence, present, confident, dominance, candidate_ratio=0.5, max_candidates=3):
    ranked = sorted(evidence.items(), key=lambda kv: -kv[1])
    (best, b), (_, second) = ranked[0], ranked[1]
    if b < present:
        return "unknown", []
    if b >= confident and b >= dominance * max(second, 1e-9):
        return "detected", [best]
    return "ambiguous", [c for c, v in ranked[:max_candidates] if v >= present and v >= candidate_ratio * b]


def score(records, present, confident, dominance):
    c = Counter()
    for r in records:
        status, cands = decide(r["evidence"], present, confident, dominance)
        c[status] += 1
        if status == "detected":
            c["correct"] += cands[0] == r["expected"]
        if status == "ambiguous":
            c["contains"] += r["expected"] in cands
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

    valid = json.loads((args.validation_dir / "nsynth-valid-evidence.json").read_text())
    test = json.loads((args.validation_dir / "nsynth-test-evidence.json").read_text())

    presents = [0.05, 0.08, 0.10, 0.12, 0.15, 0.20]
    confidents = [0.20, 0.25, 0.30, 0.35, 0.40, 0.45, 0.50, 0.60, 0.70]
    dominances = [1.2, 1.5, 1.6, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0]

    best = None
    for p, c, d in itertools.product(presents, confidents, dominances):
        if c < p:
            continue
        s = score(valid, p, c, d)
        if s["detected"] < 10 or s["precision"] < args.precision:
            continue
        # Most correct detections; among equals, the most notes that are not wrongly "unknown".
        key = (s["correct"], s["contains"] + s["correct"])
        if best is None or key > best[0]:
            best = (key, (p, c, d), s)

    if best is None:
        print(f"No setting reaches {args.precision:.0%} precision on the valid split.")
        return
    _, (p, c, d), s = best
    t = score(test, p, c, d)

    def line(name, s):
        return (f"{name}: detected {s['detected']}/{s['n']} ({s['detected'] / s['n']:.0%}), correct when detected {s['precision']:.0%}, "
                f"ambiguous {s['ambiguous']} (contains expected {s['contains']}), unknown {s['unknown']}")

    print(f"chosen: presentScore={p} confidentScore={c} dominanceRatio={d}")
    print(line("valid", s))
    print(line("test ", t))

    if args.write:
        tax = json.loads(args.taxonomy.read_text())
        tax["decision"].update({"presentScore": p, "confidentScore": c, "dominanceRatio": d})
        tax["decision"]["calibration"] = (f"Tuned on the NSynth valid split for >= {args.precision:.0%} precision of 'Detected' "
                                          f"(Scripts/models/tune_thresholds.py); test split: {t['precision']:.0%} correct when detected.")
        args.taxonomy.write_text(json.dumps(tax, indent=2) + "\n")
        print("wrote", args.taxonomy)


if __name__ == "__main__":
    main()
