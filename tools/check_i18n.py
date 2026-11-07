#!/usr/bin/env python3
"""Check that the English/Russian documentation pairs exist and link to each other.

The check is intentionally small: it verifies the file pairs and the language switcher
at the top of each file. It does not compare section structure or translations.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

PAIRS = [
    ("README.md", "README.ru.md"),
    ("opencl/README.md", "opencl/README.ru.md"),
    ("AGENTS.md", "AGENTS.ru.md"),
    ("opencl/AGENTS.md", "opencl/AGENTS.ru.md"),
    ("docs/architecture.md", "docs/architecture.ru.md"),
    ("docs/tg-wallet.md", "docs/tg-wallet.ru.md"),
    ("docs/release-process.md", "docs/release-process.ru.md"),
    ("CHANGELOG.md", "CHANGELOG.ru.md"),
]

HEAD_LINES = 5


def head(path: Path) -> str:
    with path.open(encoding="utf-8") as handle:
        return "".join(handle.readlines()[:HEAD_LINES])


def main() -> int:
    errors: list[str] = []

    for en_name, ru_name in PAIRS:
        en_path = ROOT / en_name
        ru_path = ROOT / ru_name

        for path in (en_path, ru_path):
            if not path.is_file():
                errors.append(f"missing: {path.relative_to(ROOT)}")

        if not en_path.is_file() or not ru_path.is_file():
            continue

        if f"]({ru_path.name})" not in head(en_path):
            errors.append(f"{en_name}: no link to {ru_path.name} in the first {HEAD_LINES} lines")
        if f"]({en_path.name})" not in head(ru_path):
            errors.append(f"{ru_name}: no link to {en_path.name} in the first {HEAD_LINES} lines")

    if errors:
        print("i18n check failed:")
        for error in errors:
            print(f"  {error}")
        return 1

    print(f"i18n check passed: {len(PAIRS)} language pairs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
