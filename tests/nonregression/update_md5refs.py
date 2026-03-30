#!/usr/bin/env python3
"""
Parse build/Testing/Temporary/LastTest.log, extract md5 mismatches from
failed md5 check tests, and update tests/nonregression/md5refs.txt with
the new checksums.

Also verifies that all mismatched files originate from lossy (9/7 wavelet)
JPEG 2000 images; lossless (5/3) mismatches indicate a real bug.
"""

import re
import struct
import sys
from pathlib import Path
from typing import Optional

SCRIPT_DIR = Path(__file__).parent
REPO_ROOT = SCRIPT_DIR.parent.parent
LOG_PATH = REPO_ROOT / "build/Testing/Temporary/LastTest.log"
MD5REFS_PATH = SCRIPT_DIR / "md5refs.txt"

# JPEG 2000 codestream markers
_SOC = 0xFF4F  # Start of codestream
_COD = 0xFF52  # Coding style default


def _find_codestream_start(data: bytes) -> int:
    """Return offset of the SOC marker in *data*, handling both raw
    codestreams (.j2k/.j2c) and JP2 wrapped files."""
    if len(data) < 2:
        return -1
    # Raw codestream starts with SOC (0xFF4F)
    if data[0] == 0xFF and data[1] == 0x4F:
        return 0
    # JP2 file: scan boxes for the contiguous codestream box (type 'jp2c')
    pos = 0
    while pos + 8 <= len(data):
        box_len = struct.unpack(">I", data[pos : pos + 4])[0]
        box_type = data[pos + 4 : pos + 8]
        header_size = 8
        if box_len == 1 and pos + 16 <= len(data):
            box_len = struct.unpack(">Q", data[pos + 8 : pos + 16])[0]
            header_size = 16
        if box_type == b"jp2c":
            cs_start = pos + header_size
            if cs_start + 2 <= len(data) and data[cs_start] == 0xFF and data[cs_start + 1] == 0x4F:
                return cs_start
            break
        # box_len == 0 means "extends to EOF" — but only jp2c should do that
        if box_len < header_size:
            break
        pos += box_len
    return -1


def is_lossy_j2k(filepath: Path) -> Optional[bool]:
    """Check the COD marker to determine the wavelet transform.

    Returns True for 9/7 irreversible (lossy), False for 5/3 reversible
    (lossless), or None if the transform could not be determined.
    """
    try:
        # Read enough of the header — COD is always in the main header
        data = filepath.read_bytes()[:32768]
    except OSError:
        return None

    cs = _find_codestream_start(data)
    if cs < 0:
        return None

    # Scan markers after SOC until SOD (0xFF93) or end of data.
    pos = cs + 2  # skip SOC
    while pos + 2 <= len(data):
        if data[pos] != 0xFF:
            break
        marker = (data[pos] << 8) | data[pos + 1]
        pos += 2
        if marker == 0xFF93:  # SOD — end of main header
            break
        if marker in (0xFF4F, 0xFFD9):  # SOC, EOC — shouldn't happen
            break
        if pos + 2 > len(data):
            break
        seg_len = (data[pos] << 8) | data[pos + 1]
        if marker == _COD:
            # COD segment layout after Lcod (2 bytes, already read as seg_len):
            #   Scod: 1 byte
            #   SGcod: 4 bytes (progression, numlayers(2), mct)
            #   SPcod: NDecomp(1), cbw(1), cbh(1), cbsty(1), transform(1)
            # transform offset from segment data start: 1 + 4 + 1 + 1 + 1 + 1 = 9
            qmfbid_off = pos + 2 + 9  # pos points at Lcod; +2 skips Lcod
            if qmfbid_off < len(data):
                return data[qmfbid_off] == 0  # 0 = 9/7 lossy
            return None
        pos += seg_len
    return None


def source_file_from_output(output_name: str) -> str:
    """Derive the source JP2/J2K filename from a decoded output filename.

    Output names look like 'Bretagne2.j2k_0.pgx' — the source file is
    everything before the last '_<N>.<ext>' suffix.
    """
    m = re.match(r"^(.+\.\w+?)_\d+\.\w+$", output_name)
    return m.group(1) if m else output_name


def verify_lossy(mismatches, data_dirs, canonical_md5refs=None):
    """Check that every mismatched file comes from a lossy source.

    *data_dirs* is a list of directories to search for source J2K/JP2 files.
    *canonical_md5refs* is a dict {filename: md5} from the canonical md5refs.txt;
    if the new md5 matches the canonical one, we treat it as safe even for lossless
    sources (it's just populating a new platform file).

    Returns (lossy_files, lossless_files, unknown_files) — each a list of
    (source_name, output_name) tuples.
    """
    lossy, lossless, unknown = [], [], []
    checked = {}
    for md5, output_name, source_hint in mismatches:
        # Use the source filename extracted from the test name if available,
        # otherwise fall back to heuristic derivation from the output name.
        src_name = source_hint if source_hint else source_file_from_output(output_name)
        if src_name in checked:
            result = checked[src_name]
        else:
            result = None
            for d in data_dirs:
                p = d / src_name
                if p.exists():
                    result = is_lossy_j2k(p)
                    break
            checked[src_name] = result

        entry = (src_name, output_name)
        if result is True:
            lossy.append(entry)
        elif result is False:
            # If the md5 matches the canonical value, it's just populating
            # a new platform file — not a real mismatch.
            if canonical_md5refs and canonical_md5refs.get(output_name) == md5:
                lossy.append(entry)  # treat as OK
            else:
                lossless.append(entry)
        else:
            unknown.append(entry)
    return lossy, lossless, unknown


def load_md5refs(path):
    """Load md5refs.txt into a dict {filename: md5}."""
    result = {}
    if not path.exists():
        return result
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) == 2:
            result[parts[1]] = parts[0]
    return result


def strip_ansi(text):
    return re.sub(r"\x1b\[[0-9;]*m", "", text)


def parse_mismatches(log_path):
    """
    Return list of (md5, output_filename, source_filename_or_None) tuples
    from failed md5 check tests only.

    The log contains interleaved test blocks; the same output file can appear
    in both passing and failing blocks. We scope mismatch extraction to blocks
    that end with "Test Failed." to avoid picking up stale entries.

    The test name for md5 checks is NR-DEC-<source>-<N>-decode-md5; we extract
    the source filename from it to enable lossy/lossless verification.
    """
    text = strip_ansi(log_path.read_text(errors="replace"))

    block_pattern = re.compile(r"(?=\d+/\d+ Testing: )", re.MULTILINE)
    blocks = block_pattern.split(text)

    mismatch_pattern = re.compile(
        r"Mismatch: Expected \[([0-9a-f]{32})\s+(\S+)\]", re.DOTALL
    )
    # Extract source filename from test name: NR-DEC-<source>-<num>-decode-md5
    testname_pattern = re.compile(r"Testing:\s+NR-DEC-(.+?)-\d+-decode-md5")

    # Use a dict so duplicate filenames across blocks take the last value.
    seen = {}
    for block in blocks:
        if "Test Failed." not in block:
            continue
        # Try to extract source filename from the test name
        tm = testname_pattern.search(block)
        source = tm.group(1) if tm else None
        for m in mismatch_pattern.finditer(block):
            md5, filename = m.group(1), m.group(2)
            seen[filename] = (md5, source)

    return [(md5, filename, source) for filename, (md5, source) in seen.items()]


def update_md5refs(md5refs_path, mismatches):
    lines = md5refs_path.read_text().splitlines(keepends=True)

    # Build index: filename -> line index
    # md5refs.txt format: "<md5>  <filename>\n" (two spaces)
    line_for_file = {}
    for i, line in enumerate(lines):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        parts = stripped.split()
        if len(parts) == 2:
            line_for_file[parts[1]] = i

    updated = []
    added = []

    for md5, filename, _source in mismatches:
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

    md5refs_path.write_text("".join(lines))
    return updated, added


def main():
    import argparse

    parser = argparse.ArgumentParser(
        description="Update md5refs.txt (or a platform-specific override) from ctest LastTest.log"
    )
    parser.add_argument("files", nargs="*", help="Optional: path to LastTest.log or md5refs.txt")
    parser.add_argument(
        "--platform",
        metavar="NAME",
        help=(
            "Write to md5refs-NAME.txt instead of md5refs.txt. "
            "Use the platform key matching the CI matrix: ubuntu-static, "
            "macos-dynamic, macos-static, windows-dynamic, windows-static. "
            "ubuntu-dynamic is canonical (md5refs.txt) and needs no platform key. "
            "Example: --platform macos-static"
        ),
    )
    parser.add_argument(
        "--data-dir",
        metavar="DIR",
        action="append",
        help=(
            "Directory containing source J2K/JP2 files for lossy verification. "
            "May be specified multiple times. Default: GRK_DATA_ROOT/input/nonregression "
            "and GRK_DATA_ROOT/input/conformance."
        ),
    )
    parser.add_argument(
        "--no-verify",
        action="store_true",
        help="Skip the lossy-only verification check.",
    )
    args = parser.parse_args()

    log_path = LOG_PATH
    md5refs_path = MD5REFS_PATH

    for arg in args.files:
        p = Path(arg)
        if p.name == "LastTest.log":
            log_path = p
        elif "md5refs" in p.name:
            md5refs_path = p

    if args.platform:
        md5refs_path = md5refs_path.parent / f"md5refs-{args.platform}.txt"
        if not md5refs_path.exists():
            md5refs_path.write_text("")
            print(f"Created new file: {md5refs_path}")

    if not log_path.exists():
        sys.exit(f"Log file not found: {log_path}")
    if not md5refs_path.exists():
        sys.exit(f"md5refs.txt not found: {md5refs_path}")

    mismatches = parse_mismatches(log_path)
    if not mismatches:
        print("No md5 mismatches found in log.")
        return

    print(f"Found {len(mismatches)} mismatch(es):")
    for md5, fname, source in mismatches:
        print(f"  {md5}  {fname}" + (f"  (source: {source})" if source else ""))

    # --- Verify all mismatches are from lossy sources ---
    if not args.no_verify:
        if args.data_dir:
            data_dirs = [Path(d) for d in args.data_dir]
        else:
            # Auto-detect from common build layout
            grk_data_root = REPO_ROOT.parent / "grok-test-data"
            data_dirs = [
                grk_data_root / "input" / "nonregression",
                grk_data_root / "input" / "conformance",
            ]
            data_dirs = [d for d in data_dirs if d.is_dir()]

        if data_dirs:
            # Load canonical md5refs.txt for comparison
            canonical = load_md5refs(SCRIPT_DIR / "md5refs.txt") if args.platform else {}
            lossy, lossless, unknown = verify_lossy(mismatches, data_dirs, canonical)
            if lossy:
                print(f"\n  Lossy (9/7) mismatches — expected, OK: {len(lossy)}")
                for src, out in lossy:
                    print(f"    {src} -> {out}")
            if unknown:
                print(f"\n  Could not determine transform for: {len(unknown)}")
                for src, out in unknown:
                    print(f"    {src} -> {out}")
            if lossless:
                print(f"\n  ERROR: {len(lossless)} mismatch(es) from LOSSLESS (5/3) sources!")
                for src, out in lossless:
                    print(f"    {src} -> {out}")
                print("\n  Lossless mismatches indicate a real decoder bug, not")
                print("  a floating-point rounding difference. Aborting update.")
                print("  Use --no-verify to override.")
                sys.exit(1)
        else:
            print("\n  (Skipping lossy verification — test data directory not found)")

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


if __name__ == "__main__":
    main()
