#!/usr/bin/env python3
"""Validate the compact agent code map and its referenced build inputs."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAP = ROOT / "docs" / "code-map.md"
AGENTS = ROOT / "AGENTS.md"
SKILL = ROOT / "skills" / "code-map" / "SKILL.md"
CLINE = ROOT / ".clinerules" / "code-map.md"
MAKEFILE = ROOT / "Makefile"

REQUIRED = (AGENTS, MAP, SKILL, CLINE, MAKEFILE)


def fail(message: str) -> None:
    print(f"code-map: ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def make_variable(name: str) -> str:
    match = re.search(
        rf"^{re.escape(name)}\s*:?=\s*(?P<value>.*?)(?=\n[A-Z][A-Z0-9_]*\s*:?=|\n\n)",
        MAKEFILE.read_text(encoding="utf-8"),
        re.MULTILINE | re.DOTALL,
    )
    return match.group("value") if match else ""


def make_sources() -> set[str]:
    sources: set[str] = set()
    for name in ("CORE_SRC", "CLI_SRC", "TEST_SRC", "GUI_SRC"):
        value = make_variable(name)
        sources.update(re.findall(r"(?<![A-Za-z0-9_.-])((?:src|gui|tests)/[A-Za-z0-9_./-]+\.(?:cpp|c))", value))
    return sources


def make_targets() -> set[str]:
    text = MAKEFILE.read_text(encoding="utf-8")
    targets: set[str] = set()
    for line in text.splitlines():
        match = re.match(r"^([A-Za-z0-9_.-]+):(?:\s|$)", line)
        if match:
            targets.add(match.group(1))
    return targets


def map_paths() -> set[str]:
    text = MAP.read_text(encoding="utf-8")
    paths: set[str] = set()
    for candidate in re.findall(
        r"(?<![A-Za-z0-9_./-])((?:src|gui|include|tests|tools|contrib|udev|assets|docs)/[A-Za-z0-9_./-]+)",
        text,
    ):
        candidate = candidate.rstrip(".,;:)")
        if "..." not in candidate and "*" not in candidate:
            paths.add(candidate)
    return paths


def main() -> int:
    for path in REQUIRED:
        if not path.is_file():
            fail(f"missing required file: {path.relative_to(ROOT)}")

    sources = make_sources()
    paths = map_paths()
    missing_sources = sorted(source for source in sources if not (ROOT / source).is_file())
    if missing_sources:
        fail("Makefile source paths do not exist: " + ", ".join(missing_sources))

    missing_map = sorted(source for source in sources if source not in paths)
    if missing_map:
        fail("Makefile sources missing from docs/code-map.md: " + ", ".join(missing_map))

    missing_refs = sorted(path for path in paths if not (ROOT / path).exists())
    if missing_refs:
        fail("code-map paths do not exist: " + ", ".join(missing_refs))

    targets = make_targets()
    documented_targets = {
        "verify-docs",
        "test",
        "verify-gui",
        "verify-design",
        "verify-install",
        "package",
    }
    unknown_targets = sorted(target for target in documented_targets if target not in targets)
    if unknown_targets:
        fail("documented Makefile targets do not exist: " + ", ".join(unknown_targets))

    map_text = MAP.read_text(encoding="utf-8")
    required_phrases = (
        "tools/gen_layout.py",
        "src/layout_data.cpp",
        "0x50 0x55",
        "bit 5",
        "build/",
        "bin/",
    )
    for phrase in required_phrases:
        if phrase not in map_text:
            fail(f"code map is missing required guidance: {phrase}")

    for document in (AGENTS, MAP, SKILL):
        if "391 checks" in document.read_text(encoding="utf-8"):
            fail(f"stale hard-coded test count in {document.relative_to(ROOT)}")

    print(
        f"code-map: OK ({len(sources)} Makefile sources, {len(paths)} explicit map paths, "
        f"{len(documented_targets)} documented targets)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
