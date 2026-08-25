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
import os
from pathlib import Path
import subprocess
import sys


GNU_TIME = Path("/usr/bin/time")
MERCURY_BAIL = "mercury fastpath bail"
SIZ_MARKER = b"\xff\x51"
SIZ_HEADER_BYTES = 40
IMAGE_WIDTH_OFFSET = 6
IMAGE_HEIGHT_OFFSET = 10
IMAGE_X_OFFSET = 14
IMAGE_Y_OFFSET = 18
COMPONENT_COUNT_OFFSET = 38
MARKER_READ_BYTES = 1024 * 1024
MERCURY_ENVIRONMENT_VARIABLES = (
    "GRK_MERCURY",
    "GRK_MERCURY_DEBUG",
    "MERCURY_FORCE_F32",
    "MERCURY_FORCE_I16",
    "MERCURY_FORCE_I32",
    "WEFT_DEBUG",
)


def image_shape(path):
    with path.open("rb") as source:
        data = source.read(MARKER_READ_BYTES)
    siz = data.find(SIZ_MARKER)
    if siz < 0 or siz + SIZ_HEADER_BYTES > len(data):
        raise ValueError(f"{path}: SIZ marker was not found in the first MiB")
    width = int.from_bytes(
        data[siz + IMAGE_WIDTH_OFFSET : siz + IMAGE_HEIGHT_OFFSET], "big"
    )
    height = int.from_bytes(
        data[siz + IMAGE_HEIGHT_OFFSET : siz + IMAGE_X_OFFSET], "big"
    )
    x_offset = int.from_bytes(data[siz + IMAGE_X_OFFSET : siz + IMAGE_Y_OFFSET], "big")
    y_offset = int.from_bytes(
        data[siz + IMAGE_Y_OFFSET : siz + IMAGE_Y_OFFSET + 4], "big"
    )
    components = int.from_bytes(
        data[siz + COMPONENT_COUNT_OFFSET : siz + SIZ_HEADER_BYTES], "big"
    )
    return width - x_offset, height - y_offset, components


def decode_environment(mercury):
    environment = dict(os.environ)
    for variable in MERCURY_ENVIRONMENT_VARIABLES:
        environment.pop(variable, None)
    if mercury:
        environment["GRK_MERCURY"] = "1"
        environment["GRK_MERCURY_DEBUG"] = "1"
    else:
        environment["GRK_MERCURY"] = "0"
    return environment


def run_decode(binary, source, output, metrics, threads, mercury, measure):
    decode = [str(binary), "-i", str(source), "-o", str(output), "-H", str(threads)]
    command = decode
    if measure:
        command = [
            str(GNU_TIME),
            "-f",
            "wall_seconds=%e\npeak_rss_kib=%M",
            "-o",
            str(metrics),
            *decode,
        ]
    result = subprocess.run(
        command, capture_output=True, text=True, env=decode_environment(mercury)
    )
    if result.returncode != 0:
        raise RuntimeError(f"{' '.join(command)}\n{result.stdout}{result.stderr}")
    if mercury and MERCURY_BAIL in result.stderr:
        raise RuntimeError(
            f"{source}: Mercury rejected the benchmark input\n{result.stderr}"
        )
    if not output.is_file():
        raise RuntimeError(f"{source}: decode did not create {output}")
    if not measure:
        return None
    values = {}
    for line in metrics.read_text().splitlines():
        name, value = line.split("=", 1)
        values[name] = value
    return float(values["wall_seconds"]), int(values["peak_rss_kib"])


def parse_arguments():
    parser = argparse.ArgumentParser(
        description="Measure classic and Mercury decode wall time and peak RSS on Linux."
    )
    parser.add_argument("bin_dir", type=Path)
    parser.add_argument("work_dir", type=Path)
    parser.add_argument("inputs", type=Path, nargs="+")
    parser.add_argument("--threads", type=int, nargs="+", default=[1, 2, 4, 0])
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--warmups", type=int, default=1)
    return parser.parse_args()


def main():
    arguments = parse_arguments()
    if not GNU_TIME.is_file():
        raise RuntimeError("GNU time is required at /usr/bin/time")
    binary = (arguments.bin_dir / "grk_decompress").resolve()
    if not binary.is_file():
        raise RuntimeError(f"grk_decompress was not found at {binary}")
    arguments.work_dir.mkdir(parents=True, exist_ok=True)

    writer = csv.writer(sys.stdout, lineterminator="\n")
    writer.writerow(
        [
            "file",
            "width",
            "height",
            "components",
            "input_bytes",
            "threads",
            "decoder",
            "run",
            "wall_seconds",
            "peak_rss_kib",
        ]
    )
    for input_index, source in enumerate(arguments.inputs):
        source = source.resolve()
        width, height, components = image_shape(source)
        for threads in arguments.threads:
            for decoder, mercury in (("classic", False), ("mercury", True)):
                output = arguments.work_dir / f"{input_index}-{decoder}-{threads}.tif"
                metrics = arguments.work_dir / f"{input_index}-{decoder}-{threads}.time"
                try:
                    for _ in range(arguments.warmups):
                        run_decode(
                            binary, source, output, metrics, threads, mercury, False
                        )
                    for run in range(1, arguments.runs + 1):
                        wall_seconds, peak_rss_kib = run_decode(
                            binary, source, output, metrics, threads, mercury, True
                        )
                        writer.writerow(
                            [
                                source.name,
                                width,
                                height,
                                components,
                                source.stat().st_size,
                                threads,
                                decoder,
                                run,
                                wall_seconds,
                                peak_rss_kib,
                            ]
                        )
                        sys.stdout.flush()
                finally:
                    output.unlink(missing_ok=True)
                    metrics.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
