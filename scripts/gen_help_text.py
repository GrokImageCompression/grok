#!/usr/bin/env python3
"""Generate C++ help text header from markdown man page.

Extracts the DESCRIPTION section (minus the first paragraph) from a pandoc
markdown man page and writes it as a C++11 raw string literal in a header file.

Usage:
    python3 gen_help_text.py <input.md> <output.h> <variable_name>
"""

import sys
import os


def extract_description_body(md_path):
    """Extract DESCRIPTION content after the first paragraph, up to FILES."""
    with open(md_path, encoding="utf-8") as f:
        lines = f.readlines()

    # Find "DESCRIPTION" followed by "====..." underline
    desc_start = None
    for i, line in enumerate(lines):
        if (
            line.strip() == "DESCRIPTION"
            and i + 1 < len(lines)
            and lines[i + 1].startswith("===")
        ):
            desc_start = i + 2  # skip heading + underline
            break

    if desc_start is None:
        print(f"Error: DESCRIPTION section not found in {md_path}", file=sys.stderr)
        sys.exit(1)

    # Find next top-level section (line followed by ====)
    desc_end = len(lines)
    for i in range(desc_start, len(lines)):
        if (
            i + 1 < len(lines)
            and lines[i + 1].rstrip().startswith("====")
            and len(lines[i + 1].rstrip()) >= 4
        ):
            desc_end = i
            break

    text = lines[desc_start:desc_end]

    # Strip leading blank lines
    while text and text[0].strip() == "":
        text = text[1:]

    # Skip the first paragraph (all non-blank lines until first blank line)
    first_blank = len(text)
    for i, line in enumerate(text):
        if line.strip() == "":
            first_blank = i
            break
    text = text[first_blank:]

    # Strip leading blank lines after removed paragraph
    while text and text[0].strip() == "":
        text = text[1:]

    # Strip trailing blank lines
    while text and text[-1].strip() == "":
        text.pop()

    return "".join(text)


def write_header(content, output_path, var_name, md_path):
    """Write content as a C++ raw string literal header.

    MSVC limits string literals to 16380 bytes (error C2026), so split
    into multiple concatenated raw-string chunks when the content is large.
    """
    max_chunk = 14000  # well under MSVC's 16380-byte limit
    rel_md = os.path.basename(md_path)

    parts = []
    parts.append(f"// Auto-generated from {rel_md} - DO NOT EDIT\n")
    parts.append(f"// Regenerate with: python3 scripts/gen_help_text.py\n")
    parts.append(f"#pragma once\n")
    parts.append(f"\n")
    parts.append(f"// NOLINTNEXTLINE\n")
    parts.append(f"static const char {var_name}[] =\n")

    # Split content into chunks at line boundaries.
    # Each chunk except the last carries a trailing \n so that when
    # the compiler concatenates adjacent literals the newline between
    # chunks is preserved.
    lines = content.split("\n")
    chunks = []
    chunk_lines = []
    chunk_size = 0
    for line in lines:
        line_len = len(line.encode("utf-8")) + 1  # +1 for newline
        if chunk_size + line_len > max_chunk and chunk_lines:
            chunks.append("\n".join(chunk_lines))
            chunk_lines = []
            chunk_size = 0
        chunk_lines.append(line)
        chunk_size += line_len
    if chunk_lines:
        chunks.append("\n".join(chunk_lines))

    for i, chunk in enumerate(chunks):
        last = i == len(chunks) - 1
        # Non-last chunks: include trailing \n to preserve the line
        # break that was consumed by the split.
        trail = "" if last else "\n"
        end = ";\n" if last else "\n"
        parts.append(f'R"HELPTEXT({chunk}{trail})HELPTEXT"{end}')

    header = "".join(parts)
    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(header)


def main():
    if len(sys.argv) != 4:
        print(
            f"Usage: {sys.argv[0]} <input.md> <output.h> <variable_name>",
            file=sys.stderr,
        )
        sys.exit(1)

    md_path, output_path, var_name = sys.argv[1], sys.argv[2], sys.argv[3]
    content = extract_description_body(md_path)
    write_header(content, output_path, var_name, md_path)
    print(f"Generated {output_path} from {md_path}")


if __name__ == "__main__":
    main()
