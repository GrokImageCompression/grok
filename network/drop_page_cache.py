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
import os
import ctypes
import ctypes.util
import sys

# Load libc
libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)

# Define constants for posix_fadvise
POSIX_FADV_DONTNEED = 4


def drop_file_cache(file_path, offset=0, length=0):
    # Open the file
    fd = os.open(file_path, os.O_RDONLY)
    try:
        # Use c_long or c_int64 for offset and length instead of c_off_t
        result = libc.posix_fadvise(
            fd, ctypes.c_long(offset), ctypes.c_long(length), POSIX_FADV_DONTNEED
        )
        if result != 0:
            errno = ctypes.get_errno()
            raise OSError(
                errno, f"posix_fadvise failed for {file_path}", os.strerror(errno)
            )
        print(
            f"Cache dropped for {file_path} from offset {offset} with length {length}"
        )
    finally:
        # Close the file descriptor
        os.close(fd)


# Run the script with a file path argument
if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python drop_file_cache.py <file_path>")
        sys.exit(1)
    file_path = sys.argv[1]
    drop_file_cache(file_path)  # Drop cache for the entire file
