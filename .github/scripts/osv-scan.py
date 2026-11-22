#!/usr/bin/env python3
# Copyright 2026, The NewGUI Authors. All rights reserved.
# Distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT
"""Scan the SBOM against the OSV database and fail on a known advisory.

OSV records C and C++ advisories in its GIT ecosystem, keyed by repository
and tag or commit. osv-scanner itself does not resolve vcpkg's purls
(pkg:vcpkg/... is reported as invalid) and finds nothing for pkg:github, so
this script queries the OSV API directly with the upstream sources from the
SBOM: every package whose downloadLocation is git+https://<host>/<repo>@<ref>.
Sources without such a location (plain tarballs) cannot be matched and are
listed as a notice.

No API key needed. If an advisory is ever unfixable and provably not
exploitable here, record it in osv-scanner.toml with the reason and an
ignoreUntil date rather than removing this gate:

    [[IgnoredVulns]]
    id = "CVE-2026-12345"
    ignoreUntil = 2027-01-31
    reason = "Only the decoder is affected; NewGUI uses the encoder only. Assessed 2026-11-22."

Usage: python3 .github/scripts/osv-scan.py <sbom.spdx.json> [osv-scanner.toml]
"""

import json
import re
import sys
import tomllib
import urllib.request
from datetime import date, datetime
from pathlib import Path

OSV_BATCH = "https://api.osv.dev/v1/querybatch"
GIT_LOCATION_RE = re.compile(r"^git\+(https://[^@]+?)(?:\.git)?@(.+)$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")


def load_ignored(path: Path) -> dict:
    """Map advisory id -> reason, for the entries that have not expired."""
    if not path.is_file():
        return {}
    ignored = {}
    for entry in tomllib.loads(path.read_text(encoding="utf-8")).get("IgnoredVulns", []):
        until = entry.get("ignoreUntil")
        if isinstance(until, datetime):
            until = until.date()
        if until is not None and until < date.today():
            print(f"::warning::{path}: the exception for {entry['id']} expired on {until}")
            continue
        ignored[entry["id"]] = entry.get("reason", "")
    return ignored


def main() -> None:
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    sbom = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    ignored = load_ignored(Path(sys.argv[2] if len(sys.argv) == 3 else "osv-scanner.toml"))

    sources = []
    unmatched = []
    for package in sbom["packages"]:
        location = package.get("downloadLocation", "")
        if package["SPDXID"] == "SPDXRef-NewGUI" or location in ("NONE", "NOASSERTION"):
            continue
        m = GIT_LOCATION_RE.match(location)
        if m is None:
            unmatched.append(f"{package['name']} ({location})")
            continue
        repo, ref = m.group(1), m.group(2)
        # The vcpkg registry itself (the port's recipe) is not a dependency.
        if repo.lower() == "https://github.com/microsoft/vcpkg":
            continue
        sources.append((package["name"], repo, ref))

    for item in unmatched:
        print(f"::notice::not scannable, no git source: {item}")
    if not sources:
        print("no upstream sources with a git location in the SBOM - nothing to scan")
        return

    queries = []
    for _, repo, ref in sources:
        if COMMIT_RE.match(ref):
            queries.append({"commit": ref})
        else:
            queries.append({"package": {"name": f"{repo}.git", "ecosystem": "GIT"}, "version": ref})
    request = urllib.request.Request(
        OSV_BATCH,
        data=json.dumps({"queries": queries}).encode(),
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        results = json.load(response)["results"]

    found = []
    for (name, repo, ref), result in zip(sources, results, strict=True):
        ids = sorted(v["id"] for v in result.get("vulns", []))
        open_ids = [i for i in ids if i not in ignored]
        for i in ids:
            if i in ignored:
                print(f"ignored {i} in {name} {ref}: {ignored[i]}")
        status = ", ".join(open_ids) if open_ids else "no known advisories"
        print(f"{name} ({repo} @ {ref}): {status}")
        found += [(name, ref, i) for i in open_ids]

    if found:
        for name, ref, i in found:
            print(f"::error::{name} {ref} is affected by {i}: https://osv.dev/vulnerability/{i}")
        sys.exit(1)


if __name__ == "__main__":
    main()
