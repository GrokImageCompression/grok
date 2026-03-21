#!/usr/bin/env python3
"""
Parse build/Testing/Temporary/LastTest.log, extract md5 mismatches from
failed md5 check tests, and update tests/nonregression/md5refs.txt with
the new checksums.
"""

import re
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
REPO_ROOT = SCRIPT_DIR.parent.parent
LOG_PATH = REPO_ROOT / "build/Testing/Temporary/LastTest.log"
MD5REFS_PATH = SCRIPT_DIR / "md5refs.txt"


def strip_ansi(text):
    return re.sub(r'\x1b\[[0-9;]*m', '', text)


def parse_mismatches(log_path):
    """
    Return list of (md5, filename) pairs from failed md5 check tests only.

    The log contains interleaved test blocks; the same output file can appear
    in both passing and failing blocks. We scope mismatch extraction to blocks
    that end with "Test Failed." to avoid picking up stale entries.

    Each test block in the log is delimited by lines of dashes:
      ----------------------------------------------------------
      N/M Testing: <test-name>
      ...
      Output:
      ----------------------------------------------------------
      <output including any Mismatch lines>
      <end of output>
      ...
      ----------------------------------------------------------
      Test Passed.  /  Test Failed.
      ...
      ----------------------------------------------------------
    """
    text = strip_ansi(log_path.read_text(errors='replace'))

    # Split into test blocks. Each block starts at "N/M Testing: " and ends
    # just before the next such marker (or end of file).
    block_pattern = re.compile(r'(?=\d+/\d+ Testing: )', re.MULTILINE)
    blocks = block_pattern.split(text)

    mismatch_pattern = re.compile(
        r'Mismatch: Expected \[([0-9a-f]{32})\s+(\S+)\]',
        re.DOTALL
    )

    # Use a dict so duplicate filenames across blocks take the last value.
    seen = {}
    for block in blocks:
        if 'Test Failed.' not in block:
            continue
        for m in mismatch_pattern.finditer(block):
            md5, filename = m.group(1), m.group(2)
            seen[filename] = md5

    return [(md5, filename) for filename, md5 in seen.items()]


def update_md5refs(md5refs_path, mismatches):
    lines = md5refs_path.read_text().splitlines(keepends=True)

    # Build index: filename -> line index
    # md5refs.txt format: "<md5>  <filename>\n" (two spaces)
    line_for_file = {}
    for i, line in enumerate(lines):
        stripped = line.strip()
        if not stripped or stripped.startswith('#'):
            continue
        parts = stripped.split()
        if len(parts) == 2:
            line_for_file[parts[1]] = i

    updated = []
    added = []

    for md5, filename in mismatches:
        new_entry = f"{md5}  {filename}\n"
        if filename in line_for_file:
            idx = line_for_file[filename]
            old = lines[idx].strip()
            lines[idx] = new_entry
            line_for_file[filename] = idx  # keep index fresh
            updated.append((old, new_entry.strip()))
        else:
            lines.append(new_entry)
            line_for_file[filename] = len(lines) - 1
            added.append(new_entry.strip())

    md5refs_path.write_text(''.join(lines))
    return updated, added


def main():
    log_path = LOG_PATH
    md5refs_path = MD5REFS_PATH

    # Allow overrides from command line
    for arg in sys.argv[1:]:
        p = Path(arg)
        if p.name == 'LastTest.log':
            log_path = p
        elif p.name == 'md5refs.txt':
            md5refs_path = p

    if not log_path.exists():
        sys.exit(f"Log file not found: {log_path}")
    if not md5refs_path.exists():
        sys.exit(f"md5refs.txt not found: {md5refs_path}")

    mismatches = parse_mismatches(log_path)
    if not mismatches:
        print("No md5 mismatches found in log.")
        return

    print(f"Found {len(mismatches)} mismatch(es):")
    for md5, fname in mismatches:
        print(f"  {md5}  {fname}")

    updated, added = update_md5refs(md5refs_path, mismatches)

    print(f"\nUpdated {len(updated)} existing entry/entries:")
    for old, new in updated:
        print(f"  - {old}")
        print(f"  + {new}")

    if added:
        print(f"\nAdded {len(added)} new entry/entries:")
        for entry in added:
            print(f"  + {entry}")

    print(f"\nWrote {md5refs_path}")


if __name__ == '__main__':
    main()
