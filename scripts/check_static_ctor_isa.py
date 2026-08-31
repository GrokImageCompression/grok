#!/usr/bin/env python3
# scans a shared library's static constructors, and everything they call,
# for ymm/zmm instructions. gcc LTO merges static constructors across
# translation units, so per-file -mavx flags can land in code that runs
# before the cpuid dispatch check.

import re
import struct
import subprocess
import sys

MAX_FUNCTIONS = 200
FUNCTION_SPAN = 0x2000
WIDE_REGISTER = re.compile(r"[yz]mm")
INSTRUCTION_LINE = re.compile(r"^\s+[0-9a-f]+:")
CALL_TARGET = re.compile(r"\bcall\s+([0-9a-f]+)\b")


def constructor_addresses(library):
    sections = subprocess.run(["readelf", "-SW", library], capture_output=True,
                              text=True, check=True).stdout
    match = re.search(r"\.init_array\s+\S+\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)",
                      sections)
    if not match:
        sys.exit("no .init_array section in " + library)
    address, size = int(match.group(1), 16), int(match.group(2), 16)
    headers = subprocess.run(["readelf", "-lW", library], capture_output=True,
                             text=True, check=True).stdout
    offset = None
    for line in headers.splitlines():
        parts = line.split()
        if parts and parts[0] == "LOAD":
            segment_offset = int(parts[1], 16)
            segment_address = int(parts[2], 16)
            segment_size = int(parts[4], 16)
            if segment_address <= address < segment_address + segment_size:
                offset = segment_offset + (address - segment_address)
    if offset is None:
        sys.exit("no load segment covers .init_array")
    data = open(library, "rb").read()
    pointers = [struct.unpack("<Q", data[offset + i:offset + i + 8])[0]
                for i in range(0, size, 8)]
    return [p for p in pointers if p]


def function_body(library, start):
    output = subprocess.run(
        ["objdump", "-d", f"--start-address={hex(start)}",
         f"--stop-address={hex(start + FUNCTION_SPAN)}", library],
        capture_output=True, text=True, check=True).stdout
    body = []
    for line in output.splitlines():
        if INSTRUCTION_LINE.match(line):
            body.append(line)
            if re.search(r"\bret\b", line):
                break
    return body


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: check_static_ctor_isa.py <shared library>")
    library = sys.argv[1]
    pending = constructor_addresses(library)
    print(f"{len(pending)} static constructors in {library}")
    scanned = set()
    violations = []
    while pending:
        address = pending.pop()
        if address in scanned or len(scanned) >= MAX_FUNCTIONS:
            continue
        scanned.add(address)
        for line in function_body(library, address):
            if WIDE_REGISTER.search(line):
                violations.append(f"{hex(address)}: {line.strip()}")
            target = CALL_TARGET.search(line)
            if target:
                pending.append(int(target.group(1), 16))
    print(f"scanned {len(scanned)} functions reachable from .init_array")
    if violations:
        print("AVX instructions in static constructor paths:")
        print("\n".join(violations))
        sys.exit(1)
    print("no ymm/zmm instructions found")


if __name__ == "__main__":
    main()
