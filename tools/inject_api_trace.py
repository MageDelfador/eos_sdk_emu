#!/usr/bin/env python3
"""Inject EOS_API_TRACE() into EOS_DECLARE_FUNC export wrappers."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EOS_DLL = ROOT / "eos_dll"

INCLUDE = '#include "eos_api_trace.h"\n'
MARKER = "EOS_API_TRACE();"
PATTERN = re.compile(
    r"(EOS_DECLARE_FUNC\([^\)]*\)[^\n]*\n\{)\n(?!\s*EOS_API_TRACE\(\);\n)(?!(\s*static uint32_t))",
    re.MULTILINE,
)


def inject_file(path: Path) -> int:
    text = path.read_text(encoding="utf-8")
    if MARKER in text and INCLUDE.strip() in text:
        return 0

    updated = PATTERN.sub(r"\1\n    EOS_API_TRACE();\n", text)
    if updated == text:
        return 0

    if INCLUDE.strip() not in updated:
        lines = updated.splitlines(keepends=True)
        insert_at = 0
        for i, line in enumerate(lines):
            if line.startswith("#include"):
                insert_at = i + 1
        if insert_at == 0:
            lines.insert(0, INCLUDE)
        else:
            lines.insert(insert_at, INCLUDE)
        updated = "".join(lines)

    path.write_text(updated, encoding="utf-8")
    count = updated.count(MARKER) - text.count(MARKER)
    return max(count, 0)


def main() -> int:
    targets = sorted(EOS_DLL.glob("eos_*_flat.cpp"))
    targets.append(EOS_DLL / "eos_flat_platform.cpp")
    targets.append(EOS_DLL / "eos_platform_stubs_flat.cpp")
    targets.append(EOS_DLL / "eos_client_api.cpp")
    targets.append(EOS_DLL / "eos_stub_overrides.cpp")

    total = 0
    for path in targets:
        if not path.exists():
            continue
        added = inject_file(path)
        if added:
            print(f"{path.name}: injected {added} traces")
            total += added
    print(f"Done. Total new traces: {total}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
