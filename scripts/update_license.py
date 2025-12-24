# Copyright (C) 2016-2026 Grok Image Compression Inc.
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
import os

# Define the existing license header to identify
existing_license_start = (
    "/*\n *    Copyright (C) 2016-2026 Grok Image Compression Inc.\n"
)
existing_license_end = "*/"

# Define the new license header
new_license = """/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
 *
 *    All rights reserved. This source code is proprietary and confidential.
 *    Unauthorized copying of this file, via any medium, is strictly prohibited.
 *    Proprietary and confidential.
 */
"""


def replace_license_in_file(file_path):
    """Replace the existing license header in the given file with the new license."""
    try:
        with open(file_path, "r") as file:
            content = file.read()

        # Find the start and end of the existing license
        start_index = content.find(existing_license_start)
        end_index = content.find(existing_license_end) + len(existing_license_end)

        if start_index != -1 and end_index != -1:
            # Replace the license header
            updated_content = new_license + content[end_index:]

            with open(file_path, "w") as file:
                file.write(updated_content)

            print(f"Updated license in: {file_path}")
        else:
            print(f"No existing license found in: {file_path}")
    except Exception as e:
        print(f"Failed to update license in {file_path}: {e}")


def update_license_in_directory(directory, extensions=[".h", ".cpp"], recursive=True):
    """Replace the license in all files with the specified extensions in a directory."""
    for root, _, files in os.walk(directory):
        for file in files:
            if any(file.endswith(ext) for ext in extensions):
                file_path = os.path.join(root, file)
                replace_license_in_file(file_path)

        # Stop recursion if not recursive
        if not recursive:
            break


if __name__ == "__main__":
    # Define the directory to process
    directory_to_process = "./src"  # Change this to your source code directory

    # Run the license updater
    update_license_in_directory(directory_to_process)
