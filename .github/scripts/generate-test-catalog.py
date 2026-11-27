#!/usr/bin/env python3
# Copyright 2026, The NewGUI Authors. All rights reserved.
# Distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT
"""Generate the "Test-Nachweis" docs page from the CTest results.

Reads the JUnit report of the test run (build/docs/test-results.xml, written
by the test preset "docs") - the authoritative record of what actually ran -
and enriches each test with its rationale block from the test sources
(tests/**/*Test.cpp and *Test.mm, and the fuzz targets tests/fuzz/*Fuzz.cpp): the "What is tested / Success criteria / Why it
matters" comment run at the top of the test body (CONVENTIONS.md, section
8.3). Writes docs/tests/test-evidence.md, which the Docs workflow feeds into
MkDocs.

The page is GENERATED - it must never be checked in or edited by hand,
otherwise it becomes a drifting mirror of the suite. MkDocs' --strict build
fails if this script did not run first, which is the intended guard.

Usage: python3 .github/scripts/generate-test-catalog.py [junit_xml] [output_md]
       python3 .github/scripts/generate-test-catalog.py --check

--check reads the test sources only - no test run needed - and fails on a
rationale block that breaks the shape: a missing label, the labels out of order, a label of its own, or no bare `//` line
between the block and a following Given/When/Then stage line. The Build
workflow runs it on every pull request; the page generation applies the
same checks.
"""

import html
import re
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path

CHECK_ONLY = sys.argv[1:] == ["--check"]
ARGS = [] if CHECK_ONLY else sys.argv[1:]
JUNIT = Path(ARGS[0] if len(ARGS) > 0 else "build/docs/test-results.xml")
OUTPUT = Path(ARGS[1] if len(ARGS) > 1 else "docs/tests/test-evidence.md")
TESTS_DIR = Path("tests")
REPO_URL = "https://github.com/dirkjink/NewGUI"

# GoogleTest's test definitions; the opening brace follows on its own line
# (CONVENTIONS.md, section 4.5), so the body starts after the first "{".
TEST_RE = re.compile(r"^\s*(?:TEST|TEST_F|TEST_P|TYPED_TEST|TYPED_TEST_P)\(\s*(\w+)\s*,\s*(\w+)\s*\)")
# A fuzz target (tests/fuzz/<Name>Fuzz.cpp) runs in every test run as the
# test FuzzRegressionTest.<Name> and carries the same rationale block.
FUZZ_RE = re.compile(r"^\s*NGUI_FUZZ_TARGET\(\s*(\w+)\s*\)")
FUZZ_SUITE = "FuzzRegressionTest"
# CTest names a test like GoogleTest does: "Suite.Name", with an instance
# prefix and a parameter suffix for parameterized and typed tests
# ("Prefix/Suite.Name/0", "Suite/0.Name"). The rationale is keyed by the bare
# suite and test name.
CTEST_NAME_RE = re.compile(r"^(?:\w+/)?(\w+)(?:/\w+)?\.(\w+)(?:/\w+)?$")
QUESTIONS = [
    ("Was wird getestet?", re.compile(r"What is tested:")),
    ("Woran wird Erfolg gemessen?", re.compile(r"Success criteria:")),
    ("Warum ist es wichtig?", re.compile(r"Why it matters:")),
]
# The rationale run may be followed by Given/When/Then comments; everything
# from the first stage line on is test structure, not rationale. Matched per
# comment line, so a rationale sentence that starts with "When ..." is never
# mistaken for a stage label (it carries more words before any colon).
STAGE_LABEL = r"(?:Given|When|Then|And)"
STAGE_LINE_RE = re.compile(rf"^{STAGE_LABEL}(?:\s*/\s*{STAGE_LABEL})*\s*(?:\([^)]*\))?\s*(?::.*)?$")
# A line that opens like a label of its own ("Why the second shape:") - the
# three labels are the whole contract, a fourth one would vanish from the page.
LABEL_LINE_RE = re.compile(r"^[A-Z][A-Za-z' ]{2,40}:(\s|$)")


def extract_rationales() -> tuple[dict, dict]:
    """Map (suite, test) -> {question: text}, plus suite -> source file.

    A partial or malformed block fails the run with the exact file and test,
    so incomplete evidence can never be published as documented.
    """
    rationales = {}
    files = {}
    problems = []
    sources = sorted(set(TESTS_DIR.rglob("*Test.cpp")) | set(TESTS_DIR.rglob("*Test.mm"))
                     | set(TESTS_DIR.rglob("*Fuzz.cpp")))
    for source in sources:
        lines = source.read_text(encoding="utf-8").splitlines()
        for i, line in enumerate(lines):
            m = TEST_RE.match(line)
            fuzz = FUZZ_RE.match(line)
            if m:
                suite, name = m.group(1), m.group(2)
            elif fuzz:
                suite, name = FUZZ_SUITE, fuzz.group(1)
            else:
                continue
            files.setdefault(suite, source)
            where = f"{source}:{i + 1}: {suite}.{name}"
            start = i
            while "{" not in lines[start] and start + 1 < len(lines):
                start += 1
            comment = []
            raw = []
            reached_stage = False
            for follow in lines[start + 1:]:
                stripped = follow.strip()
                if stripped.startswith("//"):
                    body = stripped[2:].strip()
                    if STAGE_LINE_RE.match(body):
                        reached_stage = True
                        break
                    comment.append(body)
                    raw.append(stripped[2:])
                elif stripped == "" and not comment:
                    continue
                else:
                    break
            text = " ".join(comment)
            spans = [(q.search(text), label) for label, q in QUESTIONS]
            spans = sorted((mm.start(), mm.end(), label) for mm, label in spans if mm)
            parsed = {}
            for idx, (_, end, label) in enumerate(spans):
                stop = spans[idx + 1][0] if idx + 1 < len(spans) else len(text)
                answer = text[end:stop].strip()
                if answer:
                    parsed[label] = answer
            if not parsed:
                continue
            if len(parsed) < len(QUESTIONS):
                missing = [q.pattern for label, q in QUESTIONS if label not in parsed]
                problems.append(f"{where} lacks {', '.join(missing)}")
            elif [label for _, _, label in spans] != [label for label, _ in QUESTIONS]:
                problems.append(f"{where} has its labels out of order")
            extra = [
                part.strip()
                for part in raw
                if not part.startswith("   ")
                and LABEL_LINE_RE.match(part.strip())
                and not any(q.match(part.strip()) for _, q in QUESTIONS)
            ]
            if extra:
                problems.append(f"{where} has a label of its own: {extra[0]!r}")
            if reached_stage and comment and comment[-1] != "":
                problems.append(f"{where} lacks the bare `//` line before its first stage line")
            rationales[(suite, name)] = parsed
    if problems:
        sys.exit(
            "error: tests whose rationale block breaks the shape of CONVENTIONS.md 8.3 (the three"
            " labels, in order, then a bare `//` line before any stage line):\n  "
            + "\n  ".join(problems)
        )
    return rationales, files


def load_results(junit: Path):
    """Group the test cases: suite -> [(test name, status, time)]."""
    root = ET.parse(junit).getroot()
    suites = defaultdict(list)
    totals = {"tests": 0, "failures": 0, "skipped": 0, "time": 0.0}
    times = defaultdict(float)
    for case in root.iter("testcase"):
        full = case.get("name", "")
        m = CTEST_NAME_RE.match(full)
        suite, name = (m.group(1), m.group(2)) if m else ("(unbekannt)", full)
        if case.find("failure") is not None or case.find("error") is not None:
            status = "fehlgeschlagen"
            totals["failures"] += 1
        elif case.find("skipped") is not None or case.get("status") in ("notrun", "disabled"):
            status = "übersprungen"
            totals["skipped"] += 1
        else:
            status = ""
        seconds = float(case.get("time", 0.0))
        totals["tests"] += 1
        totals["time"] += seconds
        times[suite] += seconds
        suites[suite].append((full, name, status))
    return suites, totals, times


def area_of(source: Path | None) -> str:
    """The module a test belongs to: its directory below tests/ ("gfx" ...)."""
    if source is None:
        return "?"
    parts = source.relative_to(TESTS_DIR).parts[:-1]
    return "/".join(parts) if parts else "ngui"


def md_escape(text: str) -> str:
    return html.escape(text, quote=False)


def main() -> None:
    rationales, files = extract_rationales()
    if CHECK_ONLY:
        print(f"test rationale blocks: ok ({len(rationales)} tests)")
        return
    if not JUNIT.is_file():
        sys.exit(f"error: {JUNIT} not found - run `cmake --workflow --preset docs` first")
    suites, totals, times = load_results(JUNIT)

    by_area = defaultdict(list)
    for suite in suites:
        by_area[area_of(files.get(suite))].append(suite)

    out = []
    out.append("# Test-Nachweis")
    out.append("")
    out.append(
        "> Vom Docs-Workflow aus den Ergebnissen des Testlaufs und den Begründungen in den"
        " Testquellen erzeugt. Nicht von Hand bearbeiten."
    )
    out.append("")
    total = f"{totals['tests']} Test" if totals["tests"] == 1 else f"{totals['tests']} Tests"
    out.append(
        f"Der Lauf hat **{total}** ausgeführt ({totals['failures']} fehlgeschlagen,"
        f" {totals['skipped']} übersprungen) in {totals['time']:.1f} s, mit GCC unter Linux"
        " (Preset `docs`). Wie die Begründungen aufgebaut sind, steht in"
        f" [`CONVENTIONS.md`, Abschnitt 8.3]({REPO_URL}/blob/main/CONVENTIONS.md)."
    )
    out.append("")
    out.append("| Bereich | Testsuite | Tests | Zeit |")
    out.append("|---|---|---:|---:|")
    for area in sorted(by_area):
        for suite in sorted(by_area[area], key=lambda s: (-len(suites[s]), s)):
            out.append(
                f"| `{area}` | [`{suite}`](#{suite.lower()}) | {len(suites[suite])}"
                f" | {times[suite]:.2f} s |"
            )
    out.append("")

    for area in sorted(by_area):
        out.append(f"## {area}")
        out.append("")
        for suite in sorted(by_area[area]):
            source = files.get(suite)
            count = len(suites[suite])
            tests = f"{count} Test" if count == 1 else f"{count} Tests"
            out.append(f"### {suite}")
            out.append("")
            if source is not None:
                out.append(f"{tests} in [`{source}`]({REPO_URL}/blob/main/{source}).")
            else:
                out.append(f"{tests}.")
            out.append("")
            for full, name, status in sorted(suites[suite]):
                suffix = f" – **{status}**" if status else ""
                out.append(f"**{md_escape(full)}**{suffix}")
                out.append("")
                rationale = rationales.get((suite, name))
                if rationale:
                    out.append('??? quote "Begründung"')
                    for label, answer in rationale.items():
                        out.append(f"    **{label}** {md_escape(answer)}")
                        out.append("")
                    out.append("")
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"wrote {OUTPUT}: {totals['tests']} tests, {len(rationales)} rationale blocks")


if __name__ == "__main__":
    main()
