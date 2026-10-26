#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Check every generated Pixel glyph's bitmap, spacing and role metrics against its drawn source."""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import gen_aa_font as gf


def main():
    text = Path(sys.argv[1] if len(sys.argv) > 1 else "build/gen/ui_fonts.h").read_text()
    assert not re.search(r"static const .*\bAF_", text), "firmware still contains AA font tables"
    def array(name):
        body = re.search(r"\b" + name + r"\[\d+\] = \{(.*?)\};", text, re.S).group(1)
        return [int(v.strip(), 0) for v in body.replace("\n", " ").split(",") if v.strip()]
    data, extra = array("AP_DATA"), array("AP_EX")
    regular, narrow = gf.pixel_glyphs(gf.PIXEL), gf.pixel_glyphs(gf.PIXEL, True)
    count = 0
    for role, scale, condensed in [("LB", 2, True), ("TX", 2, False), ("VL", 3, False), ("HR", 5, False)]:
        face = [x.strip() for x in re.search(r"AP_" + role + r" = \{([^}]*)\};", text).group(1).split(",")]
        assert tuple(map(int, face[:2])) == gf.PIXEL_METRICS[role]
        assert int(face[15]) == scale and face[8:11] == ["0"] * 3, "unexpected pixel scaling or kerning"
        entries = [tuple(map(int, row.split(","))) for row in re.findall(r"\{(-?\d+(?:, -?\d+){5})\}",
            re.search(r"\b" + face[6] + r"\[\d+\] = \{(.*?)\};", text, re.S).group(1))]
        codes = list(range(int(face[2]), int(face[3]) + 1)) + extra[:int(face[4])]
        for code, (off, adv, bx, by, w, h) in zip(codes, entries):
            ch = "\u2026" if code == 0x85 else chr(code)
            top, rows = (narrow.get(ch, regular[ch]) if condensed else regular[ch])
            assert adv == (len(rows[0]) + 1) * 16, (role, ch, "advance")
            lit = [y for y, row in enumerate(rows) if "#" in row]
            cols = [x for x in range(len(rows[0])) if any(row[x] == "#" for row in rows)]
            expected = [row[cols[0]:cols[-1] + 1] for row in rows[lit[0]:lit[-1] + 1]] if lit else []
            assert h == len(expected) and (not h or (w == len(expected[0]) and bx == cols[0] and by == top + 2 + lit[0]))
            actual = ["".join("#" if data[off + (y * w + x) // 8] & (128 >> ((y * w + x) % 8)) else "."
                              for x in range(w)) for y in range(h)]
            assert actual == expected, (role, ch, "bitmap")
            count += 1
        assert len(entries) == len(codes)
    print(f"Pixel fonts: {count} glyphs across four roles match source bitmaps, spacing and metrics; no Rubik tables")


if __name__ == "__main__":
    main()
