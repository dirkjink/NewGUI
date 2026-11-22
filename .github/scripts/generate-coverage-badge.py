#!/usr/bin/env python3
# Copyright 2026, The NewGUI Authors. All rights reserved.
# Distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT
"""Generate a self-hosted coverage badge (SVG) from gcovr's JSON summary.

Reads the summary of the coverage run (build/docs/coverage-summary.json,
written by gcovr in the Docs workflow) and writes the total line coverage as
a shields-style flat badge. The badge is published on the docs site next to
the HTML report it is derived from, so the two cannot disagree, and the
number the README shows is always generated from a real test run, never
hand-maintained.

Usage: python3 .github/scripts/generate-coverage-badge.py \
           [output_svg] [gcovr_summary_json]
"""

import json
import sys
from pathlib import Path

OUT_PATH = Path(sys.argv[1] if len(sys.argv) > 1 else "site/coverage/badge.svg")
SUMMARY = Path(sys.argv[2] if len(sys.argv) > 2 else "build/docs/coverage-summary.json")

# shields.io flat-style colors, by coverage threshold.
COLORS = [(90, "#4c1"), (80, "#97ca00"), (70, "#a4a61d"), (60, "#dfb317"), (50, "#fe7d37"), (0, "#e05d44")]

BADGE = """<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="20" role="img" aria-label="coverage: {pct}%">
  <title>coverage: {pct}%</title>
  <linearGradient id="s" x2="0" y2="100%">
    <stop offset="0" stop-color="#bbb" stop-opacity=".1"/><stop offset="1" stop-opacity=".1"/>
  </linearGradient>
  <clipPath id="r"><rect width="{width}" height="20" rx="3" fill="#fff"/></clipPath>
  <g clip-path="url(#r)">
    <rect width="{label_w}" height="20" fill="#555"/>
    <rect x="{label_w}" width="{value_w}" height="20" fill="{color}"/>
    <rect width="{width}" height="20" fill="url(#s)"/>
  </g>
  <g fill="#fff" text-anchor="middle" font-family="Verdana,Geneva,DejaVu Sans,sans-serif" font-size="110" text-rendering="geometricPrecision">
    <text x="{label_x}" y="150" transform="scale(.1)" fill="#010101" fill-opacity=".3" textLength="{label_tl}">coverage</text>
    <text x="{label_x}" y="140" transform="scale(.1)" textLength="{label_tl}">coverage</text>
    <text x="{value_x}" y="150" transform="scale(.1)" fill="#010101" fill-opacity=".3" textLength="{value_tl}">{pct}%</text>
    <text x="{value_x}" y="140" transform="scale(.1)" textLength="{value_tl}">{pct}%</text>
  </g>
</svg>
"""


def main() -> None:
    if not SUMMARY.is_file():
        sys.exit(f"error: {SUMMARY} not found - run gcovr with --json-summary first")
    summary = json.loads(SUMMARY.read_text(encoding="utf-8"))
    covered = int(summary["line_covered"])
    total = int(summary["line_total"])
    if total == 0:
        sys.exit("error: no lines found in the gcovr summary")
    pct = round(100.0 * covered / total)
    color = next(c for threshold, c in COLORS if pct >= threshold)

    label_w = 61
    value_w = 24 + 8 * len(f"{pct}%")
    width = label_w + value_w
    svg = BADGE.format(
        width=width,
        label_w=label_w,
        value_w=value_w,
        color=color,
        pct=pct,
        label_x=label_w * 5,
        label_tl=(label_w - 10) * 10,
        value_x=(label_w + value_w / 2) * 10,
        value_tl=(value_w - 10) * 10,
    )
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text(svg, encoding="utf-8")
    print(f"wrote {OUT_PATH}: coverage {pct}% ({covered}/{total} lines)")


if __name__ == "__main__":
    main()
