#!/usr/bin/env python3
# Copyright 2026, The NewGUI Authors. All rights reserved.
# Distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT
"""Writes src/MimeTable.cpp from Haiku's MIME database (src/data/mime_db).

Usage: generate-mime-table.py <haiku checkout> <output file>

Takes each type's META:TYPE, META:S:DESC and file extensions (META:EXTENS).
Haiku's types carry their icon as HVIF data (META:ICON), the icons of
data/icons/haiku/ are SVG converted from Icon-O-Matic files, so the two
cannot be compared byte for byte: ICONS names the SVG of each type with an
icon. It was picked by comparing the colors and the path coordinates of
the HVIF data with those of the SVGs; where both left a tie, the name of
the icon decided (File_Archive_TAR for application/x-tar, say).
"""

import os
import re
import subprocess
import sys

ICONS = {
    "application": "File_Generic",
    "audio": "File_Audio_2",
    "image": "File_Image_1",
    "message": "File_Mail_3",
    "text": "File_Text",
    "video": "File_Video",
    "application/java-archive": "File_Archive_Java",
    "application/pdf": "File_PDF",
    "application/postscript": "File_Postscript",
    "application/vnd.apple.mpegurl": "File_Playlist",
    "application/vnd.ms-access": "File_Database_MS_Access",
    "application/vnd.ms-visio.drawing": "File_Diagram_MS_Visio",
    "application/vnd.oasis.opendocument.base": "File_Database_Open",
    "application/vnd.oasis.opendocument.chart": "File_Chart_Open",
    "application/vnd.oasis.opendocument.chart-template": "File_Chart_Open",
    "application/vnd.oasis.opendocument.database": "File_Database_Open",
    "application/vnd.oasis.opendocument.formula": "File_Spreadsheet_Open",
    "application/vnd.oasis.opendocument.formula-template": "File_Spreadsheet_Open",
    "application/vnd.oasis.opendocument.graphics": "File_Graphics_Open",
    "application/vnd.oasis.opendocument.graphics-template": "File_Graphics_Open",
    "application/vnd.oasis.opendocument.image": "File_Image_Open",
    "application/vnd.oasis.opendocument.image-template": "File_Image_Open",
    "application/vnd.oasis.opendocument.presentation": "File_Chart_Open",
    "application/vnd.oasis.opendocument.presentation-template": "File_Chart_Open",
    "application/vnd.oasis.opendocument.spreadsheet": "File_Spreadsheet_Open",
    "application/vnd.oasis.opendocument.spreadsheet-template": "File_Spreadsheet_Open",
    "application/vnd.oasis.opendocument.text": "File_Text_Open",
    "application/vnd.oasis.opendocument.text-master": "File_Text_Open",
    "application/vnd.oasis.opendocument.text-template": "File_Text_Open",
    "application/vnd.oasis.opendocument.text-web": "File_HTML_Open",
    "application/vnd.openxmlformats-officedocument.presentationml.presentation":
        "File_Chart_MS_PowerPoint",
    "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet":
        "File_Spreadsheet_MS_Excel",
    "application/vnd.openxmlformats-officedocument.wordprocessingml.document":
        "File_Text_MS_Word",
    "application/x-7z-compressed": "File_Archive_7zip",
    "application/x-bfs-image": "Device_Volume",
    "application/x-bzip2": "File_Archive_BZ2",
    "application/x-cd-image": "Device_CD",
    "application/x-gzip": "File_Archive_GZ",
    "application/x-intel-image": "Device_Volume",
    "application/x-lharc": "File_Archive_LHA",
    "application/x-lzip": "File_Archive_LZIP",
    "application/x-ntfs-image": "Device_Volume",
    "application/x-person": "File_Person",
    "application/x-rar": "File_Archive_RAR",
    "application/x-scode-UPkg": "File_Archive",
    "application/x-tar": "File_Archive_TAR",
    "application/x-vnd.Be-bookmark": "File_Bookmark",
    "application/x-vnd.Be-directory": "Folder_generic",
    "application/x-vnd.Be-elfexecutable": "App_Generic_2",
    "application/x-vnd.Be-query": "File_Query",
    "application/x-vnd.Be-querytemplate": "File_Query_Template",
    "application/x-vnd.Be-symlink": "File_Broken_Link",
    "application/x-vnd.Be-volume": "Device_Volume",
    "application/x-vnd.haiku-core-file": "App_Generic_2",
    "application/x-vnd.haiku-package": "File_Archive_HPKG",
    "application/x-vnd.haiku-playlist": "File_Playlist",
    "application/x-vnd.haiku-virtual-directory": "Folder_virtual",
    "application/x-xar": "File_Archive_XAR",
    "application/x-xz": "File_Archive_XZ",
    "application/xhtml+xml": "File_HTML",
    "application/zip": "File_Archive_ZIP",
    "image/x-wonderbrush": "File_WonderBrush",
    "audio/x-scpls": "File_Playlist",
    "text/html": "File_HTML",
    "text/plain": "File_Text",
    "text/x-email": "File_Mail_3",
    "text/x-jamfile": "File_Pe_Project",
    "text/x-makefile": "File_Pe_Project",
    "text/x-partial-email": "File_New_Mail",
    "text/x-patch": "File_Patch",
    "text/x-source-code": "File_SourceCode",
    "text/x-vnd.Be-MailDraft": "File_Mail_4",
    "x-vnd.be.resourcedef": "File_Resource",
    "multipart/related": "File_HTML",
}


def read_types(database):
    types = []
    for directory, _, files in os.walk(database):
        for name in files:
            if name == "Jamfile":
                continue
            text = open(os.path.join(directory, name), encoding="utf-8").read()
            mime = re.search(r'"META:TYPE"\)\s*"([^"]+)"', text)
            if mime is None:
                continue
            description = re.search(r'"META:S:DESC"\)\s*#\'MSDC\'\s*"([^"]*)"', text)
            extensions = re.findall(r'"extensions"\s*=\s*"([^"]+)"', text)
            has_icon = '"META:ICON"' in text
            types.append((mime.group(1), description.group(1) if description else "",
                extensions, has_icon))
    return sorted(types, key=lambda t: t[0].lower())


def quoted(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    haiku, output = sys.argv[1], sys.argv[2]
    commit = subprocess.run(["git", "-C", haiku, "rev-parse", "--short", "HEAD"],
        capture_output=True, text=True, check=True).stdout.strip()
    types = read_types(os.path.join(haiku, "src/data/mime_db"))
    known = {t[0] for t in types}
    for mime, _, _, has_icon in types:
        if has_icon != (mime in ICONS):
            sys.exit(f"{mime}: icon in the database {has_icon}, in ICONS {mime in ICONS}")
    for mime in ICONS:
        if mime not in known:
            sys.exit(f"{mime}: in ICONS, not in the database")

    lines = [
        "/*",
        " * Copyright 2026, The NewGUI Authors. All rights reserved.",
        " * Distributed under the terms of the MIT License.",
        " * SPDX-License-Identifier: MIT",
        " */",
        "",
        "",
        f"// Generated by tools/mime/generate-mime-table.py from Haiku's MIME database",
        f"// (src/data/mime_db, commit {commit}); do not edit.",
        "",
        "",
        '#include "MimeTable.h"',
        "",
        "",
        "namespace ngui::detail {",
        "",
        "",
        f"const std::array<MimeTableEntry, {len(types)}> kMimeTable = {{{{",
    ]
    for mime, description, extensions, _ in types:
        # each field on a line of its own, long ones as adjacent literals,
        # within the 100 columns of the sources (tabs of 4)
        lines.append("\t{")
        for field in (mime, description, " ".join(extensions), ICONS.get(mime, "")):
            parts = [field[i:i + 80] for i in range(0, len(field), 80)] or [""]
            for k, part in enumerate(parts):
                end = "," if k == len(parts) - 1 else ""
                lines.append("\t\t" + quoted(part) + end)
        lines.append("\t},")
    lines += ["}};", "", "", "} // namespace ngui::detail", ""]
    open(output, "w", encoding="utf-8").write("\n".join(lines))


if __name__ == "__main__":
    main()
