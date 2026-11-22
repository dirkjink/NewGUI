#!/usr/bin/env python3
# Copyright 2026, The NewGUI Authors. All rights reserved.
# Distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT
"""Copy the project documents into the MkDocs docs_dir.

The specification, the estimate and the conventions live in the repository
root, where the README links them and where they are reviewed. This script
copies them to docs/project/ for the site and rewrites every relative link
that points at a file not on the site (LICENSE, sources ...) to its GitHub
URL; links between the copied documents stay relative.

The copies are GENERATED - they must never be checked in or edited by hand,
otherwise they drift from the originals. MkDocs' --strict build fails if this
script did not run first, which is the intended guard.

Usage: python3 .github/scripts/generate-doc-pages.py [output_dir]
"""

import re
import sys
from pathlib import Path

OUTPUT = Path(sys.argv[1] if len(sys.argv) > 1 else "docs/project")
REPO_URL = "https://github.com/dirkjink/NewGUI"
DOCUMENTS = [
    "canvas-core-profil-spezifikation.md",
    "toolkit-gesamtumfang.md",
    "CONVENTIONS.md",
    "SECURITY.md",
]
# Inline Markdown links and images: [text](target) - the target without a
# title; reference-style links are not used in the documents.
LINK_RE = re.compile(r"(!?\[[^\]]*\]\()([^)\s]+)(\))")


def rewrite(match: re.Match) -> str:
    target = match.group(2)
    if re.match(r"^[a-z][a-z0-9+.-]*:", target) or target.startswith("#"):
        return match.group(0)
    path, _, anchor = target.partition("#")
    if path in DOCUMENTS:
        return match.group(0)
    url = f"{REPO_URL}/blob/main/{path.removeprefix('./')}" + (f"#{anchor}" if anchor else "")
    return f"{match.group(1)}{url}{match.group(3)}"


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name in DOCUMENTS:
        text = Path(name).read_text(encoding="utf-8")
        note = (
            f"> Kopie von [`{name}`]({REPO_URL}/blob/main/{name}) im Repository,"
            " vom Docs-Workflow erzeugt.\n\n"
        )
        # The note goes below the first heading, so the page title stays.
        heading, sep, rest = text.partition("\n")
        body = LINK_RE.sub(rewrite, rest)
        (OUTPUT / name).write_text(f"{heading}{sep}\n{note}{body.lstrip(chr(10))}", encoding="utf-8")
    print(f"wrote {len(DOCUMENTS)} documents to {OUTPUT}")


if __name__ == "__main__":
    main()
