#!/usr/bin/env python3
"""Generates Resources/FactoryPresets/*.xml.

Factory presets are manual EQ starting points (they are not used by, and are not a substitute for,
the intelligent analysis planned for later phases). Edit the table below and re-run:

    python3 Scripts/generate_factory_presets.py
"""
from pathlib import Path
from xml.sax.saxutils import quoteattr

# (type, freq Hz, gain dB, Q, slope dB/oct)
PRESETS = [
    ("Default", []),
    ("Clean Vocal", [("HighPass", 80, 0, 0.707, 18), ("Bell", 250, -2.0, 1.2, 12),
                     ("Bell", 3000, 1.5, 0.8, 12), ("HighShelf", 10000, 2.0, 0.707, 12)]),
    ("Vocal Presence", [("HighPass", 90, 0, 0.707, 12), ("Bell", 400, -1.5, 1.0, 12),
                        ("Bell", 4500, 3.0, 0.9, 12), ("HighShelf", 12000, 2.5, 0.707, 12)]),
    ("Kick Tightening", [("HighPass", 30, 0, 0.707, 24), ("Bell", 350, -4.0, 1.4, 12),
                         ("Bell", 3500, 3.0, 1.5, 12)]),
    ("Kick Weight", [("HighPass", 25, 0, 0.707, 24), ("Bell", 60, 3.0, 1.1, 12),
                     ("Bell", 400, -3.0, 1.2, 12)]),
    ("Snare Clarity", [("HighPass", 80, 0, 0.707, 12), ("Bell", 200, 1.5, 1.0, 12),
                       ("Bell", 900, -2.5, 1.5, 12), ("Bell", 5000, 2.0, 0.9, 12)]),
    ("Rack Tom Control", [("HighPass", 60, 0, 0.707, 12), ("Bell", 400, -3.0, 1.3, 12),
                          ("Bell", 4000, 2.0, 1.0, 12)]),
    ("Floor Tom Control", [("HighPass", 40, 0, 0.707, 12), ("Bell", 300, -3.0, 1.2, 12),
                           ("Bell", 3000, 1.5, 1.0, 12)]),
    ("Overhead Cleanup", [("HighPass", 150, 0, 0.707, 12), ("Bell", 400, -2.0, 0.9, 12),
                          ("HighShelf", 10000, 1.5, 0.707, 12)]),
    ("Bass Clarity", [("HighPass", 30, 0, 0.707, 24), ("Bell", 250, -2.0, 1.0, 12),
                      ("Bell", 1000, 2.0, 1.2, 12), ("LowPass", 8000, 0, 0.707, 12)]),
    ("Bass Mud Removal", [("HighPass", 35, 0, 0.707, 18), ("Bell", 200, -3.5, 1.3, 12),
                          ("Bell", 400, -2.0, 1.5, 12)]),
    ("Acoustic Guitar Clarity", [("HighPass", 90, 0, 0.707, 12), ("Bell", 200, -3.0, 1.0, 12),
                                 ("Bell", 5000, 2.0, 0.9, 12), ("HighShelf", 12000, 1.5, 0.707, 12)]),
    ("Electric Guitar Cleanup", [("HighPass", 100, 0, 0.707, 18), ("Bell", 300, -2.0, 1.0, 12),
                                 ("Bell", 2500, -1.5, 2.0, 12), ("LowPass", 12000, 0, 0.707, 12)]),
    ("Piano Clarity", [("HighPass", 40, 0, 0.707, 12), ("Bell", 300, -2.0, 0.9, 12),
                       ("Bell", 3000, 1.5, 0.8, 12), ("HighShelf", 10000, 1.0, 0.707, 12)]),
    ("General Mix Cleanup", [("HighPass", 25, 0, 0.707, 24), ("Bell", 300, -1.0, 0.7, 12),
                             ("HighShelf", 12000, 1.0, 0.707, 12)]),
]


def main() -> None:
    out_dir = Path(__file__).resolve().parent.parent / "Resources" / "FactoryPresets"
    out_dir.mkdir(parents=True, exist_ok=True)
    for old in out_dir.glob("*.xml"):
        old.unlink()

    for number, (name, bands) in enumerate(PRESETS):
        lines = [f'<?xml version="1.0" encoding="UTF-8"?>',
                 f'<EyraEQPreset name={quoteattr(name)} version="1" inputGain="0" outputGain="0">']
        for index, (ftype, freq, gain, q, slope) in enumerate(bands, start=1):
            lines.append(f'  <Band index="{index}" type="{ftype}" freq="{freq}" gain="{gain}" '
                         f'q="{q}" slope="{slope}" placement="Stereo" bypass="0"/>')
        lines.append("</EyraEQPreset>")
        filename = f"{number:02d}_{name.replace(' ', '_')}.xml"
        (out_dir / filename).write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"wrote {filename}")


if __name__ == "__main__":
    main()
