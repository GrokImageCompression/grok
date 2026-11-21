# Copyright (C) 2016-2026 Grok Image Compression Inc.
#
# This source code is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License, version 3,
# as published by the Free Software Foundation.
#
# This source code is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.

import argparse
import csv
import shutil
import subprocess
import sys
from pathlib import Path

GNU_TIME = Path("/usr/bin/time")
CINEMA_PROFILES = ("cinema-2k", "cinema-4k")
OUTPUT_FORMAT = "J2K"


def run_encode(binary, frames_dir, output_dir, metrics, profile, fps, threads, measure):
    shutil.rmtree(output_dir, ignore_errors=True)
    output_dir.mkdir(parents=True)
    encode = [
        str(binary),
        "-y",
        str(frames_dir),
        "-a",
        str(output_dir),
        "-O",
        OUTPUT_FORMAT,
        f"--{profile}",
        str(fps),
        "-H",
        str(threads),
    ]
    command = encode
    if measure:
        command = [
            str(GNU_TIME),
            "-f",
            "wall_seconds=%e\npeak_rss_kib=%M",
            "-o",
            str(metrics),
            *encode,
        ]
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"{' '.join(command)}\n{result.stdout}{result.stderr}")
    if not measure:
        return None
    values = {}
    for line in metrics.read_text().splitlines():
        name, value = line.split("=", 1)
        values[name] = value
    return float(values["wall_seconds"]), int(values["peak_rss_kib"])


def parse_arguments():
    parser = argparse.ArgumentParser(
        description="Measure DCI batch encode wall time, frame rate and peak RSS on Linux."
    )
    parser.add_argument("bin_dir", type=Path)
    parser.add_argument("work_dir", type=Path)
    parser.add_argument("frames_dir", type=Path)
    parser.add_argument("--profile", choices=CINEMA_PROFILES, required=True)
    parser.add_argument("--fps", type=int, choices=(24, 48), default=24)
    parser.add_argument("--threads", type=int, nargs="+", default=[1, 2, 4, 0])
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--warmups", type=int, default=1)
    return parser.parse_args()


def main():
    arguments = parse_arguments()
    if not GNU_TIME.is_file():
        raise RuntimeError("GNU time is required at /usr/bin/time")
    binary = (arguments.bin_dir / "grk_compress").resolve()
    if not binary.is_file():
        raise RuntimeError(f"grk_compress was not found at {binary}")
    frames_dir = arguments.frames_dir.resolve()
    frame_count = sum(1 for path in frames_dir.iterdir() if path.is_file())
    if frame_count == 0:
        raise RuntimeError(f"{frames_dir} has no frames")
    arguments.work_dir.mkdir(parents=True, exist_ok=True)
    output_dir = arguments.work_dir / "encoded"
    metrics = arguments.work_dir / "encode.time"

    writer = csv.writer(sys.stdout, lineterminator="\n")
    writer.writerow(
        [
            "frames_dir",
            "frames",
            "profile",
            "threads",
            "run",
            "wall_seconds",
            "frames_per_second",
            "peak_rss_kib",
        ]
    )
    try:
        for threads in arguments.threads:
            for _ in range(arguments.warmups):
                run_encode(
                    binary,
                    frames_dir,
                    output_dir,
                    metrics,
                    arguments.profile,
                    arguments.fps,
                    threads,
                    False,
                )
            for run in range(1, arguments.runs + 1):
                wall_seconds, peak_rss_kib = run_encode(
                    binary,
                    frames_dir,
                    output_dir,
                    metrics,
                    arguments.profile,
                    arguments.fps,
                    threads,
                    True,
                )
                writer.writerow(
                    [
                        frames_dir.name,
                        frame_count,
                        arguments.profile,
                        threads,
                        run,
                        wall_seconds,
                        round(frame_count / wall_seconds, 2),
                        peak_rss_kib,
                    ]
                )
                sys.stdout.flush()
    finally:
        shutil.rmtree(output_dir, ignore_errors=True)
        metrics.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
