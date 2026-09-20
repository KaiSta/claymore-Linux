#!/usr/bin/env python3
"""Generate src/layout_data.cpp from the OpenRGB ASUS Aura keyboard layouts.

The per-key LED indices of the ROG Claymore were reverse engineered by the
OpenRGB project, so rather than transcribing ~700 numbers by hand we extract
them mechanically and keep a byte-for-byte audit trail.

Usage:
    tools/gen_layout.py <path-to-AsusAuraTUFKeyboardLayouts.h> > src/layout_data.cpp

The header ships with OpenRGB:
    Controllers/AsusAuraUSBController/AsusAuraTUFKeyboardController/
        AsusAuraTUFKeyboardLayouts.h
"""

import re
import sys

NA = -1

# Map block name -> (variant id prefix, region suffix)
MAPS = [
    ("AsusClaymoreNoNumpadLayouts", "no-numpad"),
    ("AsusClaymoreNumpadRightLayouts", "numpad-right"),
    ("AsusClaymoreNumpadLeftLayouts", "numpad-left"),
]

# Layout enum values used as keys inside the maps.
UK_KEY = "ASUS_TUF_K7_LAYOUT_UK"
US_KEY = "ASUS_TUF_K7_LAYOUT_US"
REGIONS = [(US_KEY, "us"), (UK_KEY, "uk")]


def parse_matrices(text):
    """Return {array_name: [[int|None, ...], ...]}."""
    out = {}
    pattern = re.compile(
        r"static\s+unsigned\s+int\s+(\w+)\s*\[\s*(\d+)\s*\]\s*\[\s*(\d+)\s*\]\s*=\s*\{(.*?)\n\};",
        re.S,
    )
    for name, rows, cols, body in pattern.findall(text):
        if "CLAYMORE" not in name:
            continue
        row_pattern = re.compile(r"\{([^{}]*)\}")
        matrix = []
        for row_body in row_pattern.findall(body):
            cells = []
            for cell in row_body.split(","):
                cell = cell.strip()
                if not cell:
                    continue
                cells.append(None if cell == "NA" else int(cell, 0))
            if cells:
                matrix.append(cells)
        assert len(matrix) == int(rows), f"{name}: {len(matrix)} rows"
        for row in matrix:
            assert len(row) == int(cols), f"{name}: bad width"
        out[name] = matrix
    return out


def humanize(macro):
    """KEY_EN_ISO_BACK_SLASH -> ('Iso Back Slash', 'iso-back-slash')."""
    if macro.startswith('"') and macro.endswith('"'):
        raw = macro[1:-1]
    else:
        raw = macro
        for prefix in ("KEY_EN_", "KEY_"):
            if raw.startswith(prefix):
                raw = raw[len(prefix):]
                break
    words = [w for w in raw.split("_") if w]
    # Keep short all-caps tokens (F1, F12) uppercase, title case the rest.
    pretty = []
    for w in words:
        if len(w) <= 3 and w.isalnum() and not w.isdigit():
            pretty.append(w.upper())
        else:
            pretty.append(w[:1].upper() + w[1:].lower())
    slug = "-".join(w.lower() for w in words)
    return " ".join(pretty), slug


def parse_map_block(text, map_name):
    """Extract {region: (matrix_name, size, rows, cols, [(name, index), ...])}."""
    block = re.search(
        r"static\s+std::map<int,\s*layout_info>\s+" + map_name + r"\s*=\s*\{(.*?)\n\};",
        text,
        re.S,
    )
    if not block:
        raise SystemExit(f"could not find map block {map_name}")
    body = block.group(1)

    # Split on each region marker; keep the text that follows it.
    spans = {}
    positions = []
    for marker, region in REGIONS:
        idx = body.find(marker)
        if idx < 0:
            raise SystemExit(f"{map_name}: missing {marker}")
        positions.append((idx, len(marker), region))
    positions.sort()
    for i, (idx, mlen, region) in enumerate(positions):
        end = positions[i + 1][0] if i + 1 < len(positions) else len(body)
        spans[region] = body[idx + mlen:end]

    result = {}
    for region, chunk in spans.items():
        m = re.search(r"\*\s*(\w+)\s*,", chunk)
        matrix_name = m.group(1)
        nums = re.search(r"(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,", chunk)
        size, rows, cols = (int(x) for x in nums.groups())

        pairs = re.findall(
            r"\{\s*([A-Za-z_]\w*|\"[^\"]*\")\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*\}",
            chunk,
        )
        if not pairs:
            raise SystemExit(f"{map_name}/{region}: no key names parsed")
        keys = [(humanize(name)[0], humanize(name)[1], int(value, 0))
                for name, value in pairs]
        assert len(keys) == size, (
            f"{map_name}/{region}: {len(keys)} names but size says {size}"
        )
        result[region] = (matrix_name, size, rows, cols, keys)
    return result


def c_ident(prefix, variant, region):
    return f"{prefix}_{variant}_{region}".replace("-", "_").upper()


def emit(text):
    matrices = parse_matrices(text)
    variants = []
    for map_name, variant in MAPS:
        parsed = parse_map_block(text, map_name)
        for region in ("us", "uk"):
            matrix_name, size, rows, cols, keys = parsed[region]
            variants.append(
                (variant, region, matrix_name, size, rows, cols, keys)
            )

    # Sanity: every matrix cell must reference a valid key slot.
    for variant, region, matrix_name, size, rows, cols, keys in variants:
        matrix = matrices[matrix_name]
        for row in matrix:
            for cell in row:
                if cell is not None:
                    assert 0 <= cell < size, (
                        f"{matrix_name}: slot {cell} out of range (size {size})"
                    )
        print(
            f"  {variant}-{region}: {size} leds, {rows}x{cols}, "
            f"matrix {matrix_name}",
            file=sys.stderr,
        )

    out = []
    out.append("/*---------------------------------------------------------*\\")
    out.append("| layout_data.cpp - GENERATED FILE, DO NOT EDIT BY HAND      |")
    out.append("|                                                           |")
    out.append("|   Regenerate with: tools/gen_layout.py <openrgb header>    |")
    out.append("|                                                           |")
    out.append("|   Derived from OpenRGB (C) Mola19, GPL-2.0-or-later.       |")
    out.append("\\*---------------------------------------------------------*/")
    out.append("")
    out.append("#include \"claymore/layout.hpp\"")
    out.append("")
    out.append("namespace claymore")
    out.append("{")
    out.append("namespace layout_data")
    out.append("{")

    for variant, region, matrix_name, size, rows, cols, keys in variants:
        ident = c_ident("MATRIX", variant, region)
        out.append(f"    /* {variant}-{region}: {matrix_name} */")
        out.append(f"    static const std::int8_t {ident}[{rows}][{cols}] = {{")
        for row in matrices[matrix_name]:
            cells = ", ".join("   -1" if c is None else f"{c:5d}" for c in row)
            out.append("        {" + cells + "},")
        out.append("    };")
        out.append("")

    for variant, region, matrix_name, size, rows, cols, keys in variants:
        ident = c_ident("KEYS", variant, region)
        out.append(f"    static const KeyName {ident}[] = {{")
        for name, slug, index in keys:
            out.append(f'        {{ "{name}", "{slug}", 0x{index:02X} }},')
        out.append("    };")
        out.append("")

    out.append("    static const LayoutVariant claymore_variants[] = {")
    for variant, region, matrix_name, size, rows, cols, keys in variants:
        vid = f"{variant}-{region}"
        out.append("        {")
        out.append(f'            "{vid}",')
        out.append(f"            {rows},")
        out.append(f"            {cols},")
        out.append(f"            &{c_ident('MATRIX', variant, region)}[0][0],")
        out.append(f"            {c_ident('KEYS', variant, region)},")
        out.append(f"            {size},")
        out.append("        },")
    out.append("    };")
    out.append("")
    # `extern` matters: a namespace scope const without it gets internal
    # linkage and would not be visible to layout.cpp.
    out.append(
        "    extern const LayoutVariant* const all_claymore_variants;"
    )
    out.append(
        "    extern const int claymore_variant_count;"
    )
    out.append("")
    out.append(
        "    extern const LayoutVariant* const all_claymore_variants = claymore_variants;"
    )
    out.append(
        "    extern const int claymore_variant_count = "
        "sizeof(claymore_variants) / sizeof(claymore_variants[0]);"
    )
    out.append("}")
    out.append("}")
    out.append("")
    return "\n".join(out)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    with open(sys.argv[1], "r", encoding="utf-8") as handle:
        text = handle.read()
    sys.stdout.write(emit(text))


if __name__ == "__main__":
    main()
