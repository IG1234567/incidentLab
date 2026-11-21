#!/usr/bin/env python3
"""Summarize GCC coverage for IncidentLab production sources.

Reads .gcda files under the core library and prints line and function coverage.
Test sources, the GUI, and generated build files are not counted.
gcovr is not required.
"""

import gzip
import json
import subprocess
import sys
from pathlib import Path


def production_source(path: str) -> bool:
    normalized = path.replace("\\", "/")
    if "/tests/" in normalized or normalized.startswith("tests/"):
        return False
    if "/src/gui/" in normalized or "/build/" in normalized:
        return False
    return "/src/" in normalized or "/include/incidentlab/" in normalized or normalized.startswith(
        ("src/", "include/incidentlab/")
    )


def load_gcov_json(gcda: Path) -> dict:
    completed = subprocess.run(
        ["gcov", "--json-format", "--stdout", "--preserve-paths", "--object-directory", str(gcda.parent), str(gcda)],
        check=False,
        capture_output=True,
    )
    if completed.returncode != 0:
        sys.stderr.write(completed.stderr.decode("utf-8", errors="replace"))
        raise SystemExit(f"gcov failed for {gcda}")
    payload = completed.stdout
    if payload.startswith(b"\x1f\x8b"):
        payload = gzip.decompress(payload)
    text = payload.decode("utf-8")
    start = text.find("{")
    if start < 0:
        raise SystemExit(f"gcov produced no JSON for {gcda}")
    return json.loads(text[start:])


def main() -> int:
    if len(sys.argv) != 3:
        sys.stderr.write("usage: coverage_summary.py <build-dir> <output-file>\n")
        return 2
    build_dir = Path(sys.argv[1])
    output_path = Path(sys.argv[2])
    gcda_files = sorted(build_dir.glob("**/CMakeFiles/incidentlab_core.dir/**/*.gcda"))
    if not gcda_files:
        sys.stderr.write(f"no core coverage data under {build_dir}\n")
        return 1

    lines: dict[tuple[str, int], int] = {}
    functions: dict[tuple[str, str, int], int] = {}
    for gcda in gcda_files:
        document = load_gcov_json(gcda)
        for source in document.get("files", []):
            filename = source.get("file", "")
            if not production_source(filename):
                continue
            for line in source.get("lines", []):
                key = (filename, int(line["line_number"]))
                lines[key] = max(lines.get(key, 0), int(line.get("count", 0)))
            for function in source.get("functions", []):
                name = function.get("demangled_name") or function.get("name") or "?"
                key = (filename, name, int(function.get("start_line", 0)))
                functions[key] = max(functions.get(key, 0), int(function.get("execution_count", 0)))

    line_total = len(lines)
    line_hit = sum(1 for count in lines.values() if count > 0)
    function_total = len(functions)
    function_hit = sum(1 for count in functions.values() if count > 0)
    if line_total == 0 or function_total == 0:
        sys.stderr.write("coverage data did not include production lines or functions\n")
        return 1

    line_percent = 100.0 * line_hit / line_total
    function_percent = 100.0 * function_hit / function_total
    report = (
        f"Line coverage: {line_percent:.1f}% ({line_hit}/{line_total})\n"
        f"Function coverage: {function_percent:.1f}% ({function_hit}/{function_total})\n"
        "Scope: incidentlab_core sources under src/ and include/incidentlab/.\n"
        "Excluded: tests/, src/gui/, and generated build files.\n"
    )
    output_path.write_text(report, encoding="utf-8")
    sys.stdout.write(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
