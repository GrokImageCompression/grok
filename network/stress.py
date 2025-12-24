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
import subprocess
import threading
import argparse
from pathlib import Path

CHUNK_SIZE = 50 * 1024 * 1024  # 50 MB in bytes


def rclone_copy(source, dest_dir, thread_id, progress_list):
    """Use rclone to copy the source file to a thread-specific directory."""
    # Create a thread-specific directory inside the destination
    thread_dir = os.path.join(dest_dir, f"thread_{thread_id}")
    os.makedirs(thread_dir, exist_ok=True)

    # Define the destination file path
    dest_file = os.path.join(
        thread_dir, f"{Path(source).stem}{thread_id}{Path(source).suffix}"
    )

    # Run the rclone copy command
    result = subprocess.run(
        ["rclone", "copyto", source, dest_file],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    # Check the result and report progress
    if result.returncode == 0:
        print(f"Thread {thread_id}: rclone copied {source} to {dest_file}")
    else:
        print(
            f"Thread {thread_id}: rclone copy failed with error: {result.stderr.decode('utf-8')}"
        )

    # Mark this thread as done in the progress list
    progress_list[thread_id - 1] = dest_file


def prefetch_range_mounted(file_path, start, end):
    """Prefetch a specific byte range from a mounted file."""
    with open(file_path, "rb") as f:
        f.seek(start)
        data = f.read(end - start)
        print(f"Prefetched range {start}-{end} from {file_path}")


def binary_compare_in_chunks(file1, file2, chunk_size=4096):
    """Compare two files in binary mode, chunk by chunk."""
    with open(file1, "rb") as f1, open(file2, "rb") as f2:
        while True:
            b1 = f1.read(chunk_size)
            b2 = f2.read(chunk_size)
            if b1 != b2:
                return False
            if not b1:  # End of file reached
                break
    return True


def clear_rclone_vfs_cache(vfs_cache_dir):
    """Delete all contents of the Rclone VFS cache directory."""
    if os.path.exists(vfs_cache_dir):
        subprocess.run(["rclone", "purge", vfs_cache_dir], check=True)
        print(f"Cleared Rclone VFS cache directory: {vfs_cache_dir}")


def main(source, dest, vfs_cache, num_threads):
    # Clear the Rclone VFS cache
    clear_rclone_vfs_cache(vfs_cache)

    # Progress list to keep track of completed copies and prefetches
    progress_list = [None] * num_threads

    # Get the total file size from the mounted source
    file_size = os.path.getsize(source)

    # Launch threads to copy the file using rclone
    threads = []
    half_threads = num_threads // 2
    for i in range(1, half_threads + 1):
        thread = threading.Thread(
            target=rclone_copy, args=(source, dest, i, progress_list)
        )
        threads.append(thread)
        thread.start()

    # Launch threads to prefetch specific ranges from the mounted file
    for i in range(half_threads + 1, num_threads + 1):
        start = file_size - (i - half_threads) * CHUNK_SIZE
        end = min(
            start + CHUNK_SIZE, file_size
        )  # Ensure we don't go past the file size
        if start < 0:
            start = 0
        thread = threading.Thread(
            target=prefetch_range_mounted, args=(source, start, end)
        )
        threads.append(thread)
        thread.start()

    # Wait for all threads to finish
    for thread in threads:
        thread.join()

    # After all threads are done, perform binary comparisons in the main thread
    for i, copied_file in enumerate(progress_list, 1):
        if copied_file:
            print(f"Main thread: Comparing {copied_file} to {source}")
            if binary_compare_in_chunks(source, copied_file):
                print(f"Main thread: Binary comparison successful for {copied_file}")
            else:
                print(f"Main thread: Binary comparison failed for {copied_file}")
        else:
            print(f"Main thread: No file found for thread {i}")


if __name__ == "__main__":
    # Parse command-line arguments
    parser = argparse.ArgumentParser(
        description="Use rclone to copy and prefetch a file into multiple directories using threads."
    )
    parser.add_argument(
        "source", type=str, help="The source file to copy (already mounted)."
    )
    parser.add_argument("dest", type=str, help="The destination directory.")
    parser.add_argument(
        "vfs_cache", type=str, help="Directory of the Rclone VFS cache."
    )
    parser.add_argument(
        "--threads",
        type=int,
        default=10,
        help="Number of threads to use (default: 10).",
    )

    args = parser.parse_args()

    # Run the main function
    main(args.source, args.dest, args.vfs_cache, args.threads)
