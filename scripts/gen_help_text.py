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
        if (line.strip() == "DESCRIPTION"
                and i + 1 < len(lines)
                and lines[i + 1].startswith("===")):
            desc_start = i + 2  # skip heading + underline
            break

    if desc_start is None:
        print(f"Error: DESCRIPTION section not found in {md_path}",
              file=sys.stderr)
        sys.exit(1)

    # Find next top-level section (line followed by ====)
    desc_end = len(lines)
    for i in range(desc_start, len(lines)):
        if (i + 1 < len(lines)
                and lines[i + 1].rstrip().startswith("====")
                and len(lines[i + 1].rstrip()) >= 4):
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
    """Write content as a C++ raw string literal header."""
    rel_md = os.path.basename(md_path)
    header = (
        f"// Auto-generated from {rel_md} - DO NOT EDIT\n"
        f"// Regenerate with: python3 scripts/gen_help_text.py\n"
        f"#pragma once\n"
        f"\n"
        f"// NOLINTNEXTLINE\n"
        f"static const char {var_name}[] =\n"
        f'R"HELPTEXT(\n'
        f"{content}\n"
        f')HELPTEXT";\n'
    )
    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(header)


def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <input.md> <output.h> <variable_name>",
              file=sys.stderr)
        sys.exit(1)

    md_path, output_path, var_name = sys.argv[1], sys.argv[2], sys.argv[3]
    content = extract_description_body(md_path)
    write_header(content, output_path, var_name, md_path)
    print(f"Generated {output_path} from {md_path}")


if __name__ == "__main__":
    main()
