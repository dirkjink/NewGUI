#!/usr/bin/env python3
# Copyright 2026, The NewGUI Authors. All rights reserved.
# Distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT
"""Aggregate vcpkg's per-port SPDX documents into one SBOM for NewGUI.

vcpkg writes an SPDX 2.3 document for every installed port
(<installed>/<triplet>/share/<port>/vcpkg.spdx.json): the port itself (with a
pkg:vcpkg purl), the binary it generates and the upstream sources it was
built from (downloadLocation git+https://github.com/<owner>/<repo>@<ref> ...).
This script folds them into a single SPDX 2.3 document whose root package is
NewGUI, depending on every port - the SBOM of what ships.

Only the ports of a vcpkg install WITHOUT the "tests" feature belong in it:
test-only dependencies such as GoogleTest reach no consumer. The helper ports
vcpkg-cmake, vcpkg-cmake-config ... are build tools, not shipped either, and
are left out by their name prefix.

The output is deterministic: sorted packages, and the creation time is
SOURCE_DATE_EPOCH (the commit time if unset), so the same commit gives the
same SBOM byte for byte.

Usage: python3 .github/scripts/vcpkg-sbom.py <installed>/<triplet> <output.spdx.json>
"""

import hashlib
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO_URL = "https://github.com/dirkjink/NewGUI"
BUILD_TOOL_PREFIX = "vcpkg-"


def creation_time() -> str:
    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    if epoch is None:
        epoch = subprocess.run(
            ["git", "log", "-1", "--format=%ct"], capture_output=True, text=True, check=True
        ).stdout.strip()
    return datetime.fromtimestamp(int(epoch), tz=timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    triplet_dir = Path(sys.argv[1])
    output = Path(sys.argv[2])
    version = json.loads(Path("vcpkg.json").read_text(encoding="utf-8"))["version-semver"]

    root_id = "SPDXRef-NewGUI"
    packages = [
        {
            "name": "NewGUI",
            "SPDXID": root_id,
            "versionInfo": version,
            "downloadLocation": f"git+{REPO_URL}.git",
            "licenseConcluded": "MIT",
            "licenseDeclared": "MIT",
            "copyrightText": "Copyright 2026, The NewGUI Authors",
            "filesAnalyzed": False,
            "externalRefs": [
                {
                    "referenceCategory": "PACKAGE-MANAGER",
                    "referenceType": "purl",
                    "referenceLocator": f"pkg:github/dirkjink/NewGUI@{version}",
                }
            ],
        }
    ]
    relationships = [
        {"spdxElementId": "SPDXRef-DOCUMENT", "relationshipType": "DESCRIBES", "relatedSpdxElement": root_id}
    ]

    ports = sorted(triplet_dir.glob("share/*/vcpkg.spdx.json")) if triplet_dir.is_dir() else []
    for spdx in ports:
        port = spdx.parent.name
        if port.startswith(BUILD_TOOL_PREFIX):
            continue
        doc = json.loads(spdx.read_text(encoding="utf-8"))
        # The IDs are unique per document only; prefixed with the port they
        # stay unique in the aggregate.
        rename = {p["SPDXID"]: f"SPDXRef-{port}-{p['SPDXID'].removeprefix('SPDXRef-')}" for p in doc["packages"]}
        for package in doc["packages"]:
            copied = {k: v for k, v in package.items() if k != "hasFiles"}
            copied["SPDXID"] = rename[package["SPDXID"]]
            copied.setdefault("filesAnalyzed", False)
            packages.append(copied)
        for rel in doc.get("relationships", []):
            if rel["spdxElementId"] in rename and rel["relatedSpdxElement"] in rename:
                relationships.append(
                    {
                        "spdxElementId": rename[rel["spdxElementId"]],
                        "relationshipType": rel["relationshipType"],
                        "relatedSpdxElement": rename[rel["relatedSpdxElement"]],
                    }
                )
        port_id = rename.get("SPDXRef-port")
        if port_id is not None:
            relationships.append(
                {"spdxElementId": root_id, "relationshipType": "DEPENDS_ON", "relatedSpdxElement": port_id}
            )

    packages[1:] = sorted(packages[1:], key=lambda p: p["SPDXID"])
    relationships.sort(key=lambda r: (r["spdxElementId"], r["relationshipType"], r["relatedSpdxElement"]))
    content = json.dumps({"packages": packages, "relationships": relationships}, sort_keys=True)
    document = {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": f"NewGUI-{version}",
        # Unique per content, as SPDX requires, and still deterministic.
        "documentNamespace": f"{REPO_URL}/sbom/{version}-{hashlib.sha256(content.encode()).hexdigest()[:16]}",
        "creationInfo": {
            "created": creation_time(),
            "creators": ["Tool: NewGUI .github/scripts/vcpkg-sbom.py", "Tool: vcpkg"],
        },
        "packages": packages,
        "relationships": relationships,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    shipped = sum(1 for r in relationships if r["relationshipType"] == "DEPENDS_ON")
    print(f"wrote {output}: {shipped} shipped vcpkg ports, {len(packages)} packages")


if __name__ == "__main__":
    main()
