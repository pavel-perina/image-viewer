#!/usr/bin/env python3
"""Fails if the AppImage bundles a GPL library.

Every shared library in the AppDir is traced to where it came from: a Conan package (its licence
from the dependency graph), Qt (LGPL-3.0), or a system package (its licence from rpm). A licence
passes when it can be met without the GPL: one acceptable alternative of an "or", every part of an
"and". The LGPL and the GCC runtime's exception are fine. Packages whose rpm licence mixes the
library with GPL tools are in REVIEWED, each with the library's own licence and why. Usage: check_licences.py APPDIR CONAN_GRAPH_JSON QT_DIR
"""
import json
import os
import re
import subprocess
import sys

# GPL, but not LGPL or AGPL counted separately; "GPL ... with exceptions" is the GCC runtime.
GPL = re.compile(r"(?<![LA])GPL")
RUNTIME_EXCEPTION = re.compile(r"exception", re.IGNORECASE)

# An rpm's licence covers the whole package, its tools too, so "X and GPL" may be a library under
# X beside GPL programs. These were looked at one by one: the library's own licence, and why.
REVIEWED = {
    "libdbus-1.so.3": "AFL-2.1 or GPL-2.0+: the library may be taken under AFL; the GPL part is the tools",
    "liblz4.so.1": "BSD-2-Clause: the library; the GPL part is the lz4 program",
    "libzstd.so.1": "BSD-3-Clause or GPL-2.0: the library may be taken under BSD",
    "libkeyutils.so.1": "LGPL-2.1+: the library; the GPL part is the keyctl tools",
    "libatomic.so.1": "GPL-3.0+ with the GCC Runtime Library Exception",
}


def split_top(text, word):
    """Splits at a word outside parentheses: "a or (b and c)" at "or" gives a, (b and c)."""
    parts, depth, start = [], 0, 0
    lowered = text.lower()
    i = 0
    while i < len(text):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
        elif depth == 0 and lowered.startswith(f" {word} ", i):
            parts.append(text[start:i])
            start = i + len(word) + 2
            i = start
            continue
        i += 1
    parts.append(text[start:])
    return [part.strip() for part in parts]


def acceptable(licence):
    """Whether a licence expression can be met without the GPL: every part joined by "and", and
    one alternative of each "or"."""
    licence = licence.strip()
    while licence.startswith("(") and licence.endswith(")") and split_top(licence[1:-1], "or")[0] != licence:
        inner = licence[1:-1]
        if inner.count("(") != inner.count(")"):
            break
        licence = inner.strip()
    alternatives = split_top(licence, "or")
    if len(alternatives) > 1:
        return any(acceptable(a) for a in alternatives)
    parts = split_top(licence, "and")
    if len(parts) > 1:
        return all(acceptable(p) for p in parts)
    return not GPL.search(licence) or bool(RUNTIME_EXCEPTION.search(licence))


def conan_libraries(graph_path):
    with open(graph_path) as f:
        nodes = json.load(f)["graph"]["nodes"].values()
    found = {}
    for node in nodes:
        folder = node.get("package_folder")
        if node.get("context") != "host" or not folder:
            continue
        licence = node.get("license") or "unknown"
        if isinstance(licence, list):
            licence = " AND ".join(licence)
        for root, _, files in os.walk(folder):
            for name in files:
                if ".so" in name:
                    found[name] = (node["ref"], licence)
    return found


def rpm_licence(name):
    for folder in ("/usr/lib64", "/lib64"):
        path = os.path.join(folder, name)
        if os.path.exists(path):
            result = subprocess.run(["rpm", "-qf", "--qf", "%{NAME} %{LICENSE}", path],
                                    capture_output=True, text=True)
            if result.returncode == 0:
                package, _, licence = result.stdout.partition(" ")
                return package, licence
    return None, None


def main():
    appdir, graph_path, qt_dir = sys.argv[1:4]
    conan = conan_libraries(graph_path)
    qt_libs = set(os.listdir(os.path.join(qt_dir, "gcc_64", "lib")))
    failures = []
    for root, _, files in os.walk(appdir):
        for name in sorted(files):
            if ".so" not in name or os.path.islink(os.path.join(root, name)):
                continue
            if name in conan:
                source, licence = conan[name]
            elif name in qt_libs or name.startswith("libQt6") or "/plugins/" in root or "/qml/" in root:
                source, licence = "Qt", "LGPL-3.0"
            else:
                source, licence = rpm_licence(name)
                if source is None:
                    failures.append(f"{name}: where it came from is not known")
                    continue
            if name in REVIEWED:
                licence = REVIEWED[name]
                ok = True
            else:
                ok = acceptable(licence)
            print(f"{'ok  ' if ok else 'GPL!'} {name:40} {source:30} {licence}")
            if not ok:
                failures.append(f"{name}: {source} is {licence}")
    if failures:
        print("\nThe AppImage must not bundle these:", *failures, sep="\n  ")
        sys.exit(1)


if __name__ == "__main__":
    main()
