# Copyright (C) 2016-2025 Grok Image Compression Inc.
#
# This source code is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License, version 3,
# as published by the Free Software Foundation.
#
# This source code is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
import sys
import textwrap


def escape_c_string(s):
    s = s.replace("\\", "\\\\").replace('"', '\\"')
    s = s.replace("#", "")  # Remove markdown special character #
    return s


def markdown_to_fprintf(md_file, output_file):
    with open(md_file, "r") as md, open(output_file, "w") as out:
        for line in md:
            stripped = escape_c_string(line.strip())
            if stripped:  # non-empty line
                # textwrap.wrap breaks the line into segments no longer than 80 characters
                for segment in textwrap.wrap(stripped, 80):
                    out.write(f'fprintf(stdout, "{segment}\\n");\n')
            else:  # empty line
                out.write('fprintf(stdout, "\\n");\n')


def main():
    if len(sys.argv) != 3:
        print("Usage: python3 markdown_to_fprintf.py <input.md> <output.c>")
        return

    md_file = sys.argv[1]
    output_file = sys.argv[2]
    markdown_to_fprintf(md_file, output_file)


if __name__ == "__main__":
    main()
