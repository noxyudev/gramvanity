#!/usr/bin/env python3
"""Check that every tr("english", "russian") pair carries the same printf conversion specifiers.

tr() selects between two message variants at run time, and the surrounding printf feeds both
variants from one shared argument list, so both variants must consume the same arguments
(AGENTS.md: "both variants must carry the same printf conversion specifiers"). The check parses
the C sources directly: calls whose arguments are not plain string literals (the tr() definition
itself) are skipped, adjacent string literals concatenate, comments are ignored, and the ordered
conversion sequences (length modifiers + conversion, `*` widths included) must match exactly.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "opencl" / "src"

SPEC_RE = re.compile(
    r"%(?:(\*)|(\d+\$))?[-+ #0]*(?:(\*)|(\d+))?(?:\.(\*)?(\d+)?)?"
    r"(hh|h|ll|l|j|z|t|L)?([diouxXeEfFgGaAcspn%])"
)


def strip_comments(text: str) -> str:
    """Replace comments with spaces, preserving offsets and string literals."""
    out = list(text)
    i, n = 0, len(text)
    in_str = False
    while i < n:
        c = text[i]
        if in_str:
            if c == "\\" and i + 1 < n:
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
            continue
        if c == '"':
            in_str = True
            i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = " "
            i = j
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if text[k] != "\n":
                    out[k] = " "
            i = j
            continue
        i += 1
    return "".join(out)


def read_string(text: str, i: int):
    """Read one concatenated string-literal group at i (whitespace allowed between parts).

    Returns (raw_content, next_index) or (None, i) when the next token is not a string.
    """
    parts = []
    n = len(text)
    while True:
        while i < n and text[i] in " \t\r\n":
            i += 1
        if i >= n or text[i] != '"':
            break
        i += 1
        buf = []
        while i < n:
            c = text[i]
            if c == "\\" and i + 1 < n:
                buf.append(text[i : i + 2])
                i += 2
                continue
            if c == '"':
                i += 1
                break
            buf.append(c)
            i += 1
        else:
            return None, i  # unterminated
        parts.append("".join(buf))
    if not parts:
        return None, i
    return "".join(parts), i


def specs(fmt: str):
    """Ordered conversion tokens of a printf format: '*' widths plus length+conv."""
    out = []
    for m in SPEC_RE.finditer(fmt):
        conv = m.group(8)
        if conv == "%":
            continue
        star = "*" if (m.group(1) or m.group(3) or m.group(5)) else ""
        out.append(star + (m.group(7) or "") + conv)
    leftovers = SPEC_RE.sub("", fmt)
    if "%" in leftovers:
        out.append("?")
    return out


def main() -> int:
    errors = []
    checked = 0
    skipped = 0
    for path in sorted(SRC_DIR.glob("*.c")):
        text = strip_comments(path.read_text(encoding="utf-8"))
        for m in re.finditer(r"\btr\s*\(", text):
            i = m.end()
            en, i = read_string(text, i)
            if en is None:
                skipped += 1  # the tr() definition or a non-literal call
                continue
            while i < len(text) and text[i] in " \t\r\n":
                i += 1
            if i >= len(text) or text[i] != ",":
                skipped += 1
                continue
            ru, i = read_string(text, i + 1)
            if ru is None:
                skipped += 1
                continue
            while i < len(text) and text[i] in " \t\r\n":
                i += 1
            if i >= len(text) or text[i] != ")":
                skipped += 1
                continue
            checked += 1
            en_s, ru_s = specs(en), specs(ru)
            if en_s != ru_s:
                line = text.count("\n", 0, m.start()) + 1
                errors.append(
                    f"{path.relative_to(ROOT)}:{line}: specifier mismatch: "
                    f"en {en_s if en_s else '[]'} != ru {ru_s if ru_s else '[]'}"
                )
    if errors:
        print("tr() specifier check failed:")
        for e in errors:
            print(f"  {e}")
        return 1
    print(f"tr() specifier check passed: {checked} pair(s) checked, {skipped} skipped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
