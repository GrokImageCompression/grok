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

"""Tests for stdin/stdout support in grk_compress and grk_decompress CLI tools."""

import os
import shutil
import subprocess

import pytest


def find_binary(name):
    """Find a grok CLI binary, checking the build directory first."""
    build_bin = os.path.join(
        os.path.dirname(__file__), "..", "..", "build", "bin", name
    )
    build_bin = os.path.normpath(build_bin)
    if os.path.isfile(build_bin):
        return build_bin
    path = shutil.which(name)
    if path:
        return path
    return None


GRK_COMPRESS = find_binary("grk_compress")
GRK_DECOMPRESS = find_binary("grk_decompress")

requires_cli = pytest.mark.skipif(
    GRK_COMPRESS is None or GRK_DECOMPRESS is None,
    reason="grk_compress or grk_decompress not found",
)


def make_test_jp2(tmp_path, filename="test.jp2"):
    """Create a small test JP2 file using grk_compress."""
    # Create a small PGM (grayscale) image
    pgm_path = str(tmp_path / "test.pgm")
    width, height, maxval = 32, 32, 255
    pixels = bytes((x + y) % 256 for y in range(height) for x in range(width))
    with open(pgm_path, "wb") as f:
        f.write(f"P5\n{width} {height}\n{maxval}\n".encode())
        f.write(pixels)

    jp2_path = str(tmp_path / filename)
    result = subprocess.run(
        [GRK_COMPRESS, "-i", pgm_path, "-o", jp2_path],
        capture_output=True,
    )
    assert result.returncode == 0, (
        f"grk_compress failed: {result.stderr.decode()}"
    )
    assert os.path.getsize(jp2_path) > 0
    return jp2_path, pgm_path


@requires_cli
class TestDecompressToStdout:
    def test_decompress_to_stdout_png(self, tmp_path):
        """Decompress a JP2 to PNG via stdout using --out-fmt."""
        jp2_path, _ = make_test_jp2(tmp_path)

        result = subprocess.run(
            [GRK_DECOMPRESS, "-i", jp2_path, "--out-fmt", "PNG"],
            capture_output=True,
        )
        assert result.returncode == 0, (
            f"grk_decompress to stdout failed: {result.stderr.decode()}"
        )
        # PNG files start with an 8-byte signature
        assert result.stdout[:4] == b"\x89PNG", (
            "Output does not look like a PNG file"
        )
        assert len(result.stdout) > 8

    def test_decompress_to_stdout_ppm(self, tmp_path):
        """Decompress a JP2 to PPM/PGM via stdout using --out-fmt."""
        jp2_path, _ = make_test_jp2(tmp_path)

        result = subprocess.run(
            [GRK_DECOMPRESS, "-i", jp2_path, "--out-fmt", "PGM"],
            capture_output=True,
        )
        assert result.returncode == 0, (
            f"grk_decompress to stdout failed: {result.stderr.decode()}"
        )
        # PGM files start with "P5" (binary) or "P2" (ASCII)
        assert result.stdout[:2] in (b"P5", b"P2"), (
            "Output does not look like a PGM file"
        )

    def test_decompress_to_stdout_matches_file(self, tmp_path):
        """Verify stdout output matches file output."""
        jp2_path, _ = make_test_jp2(tmp_path)

        # Decompress to file
        out_path = str(tmp_path / "out.png")
        result_file = subprocess.run(
            [GRK_DECOMPRESS, "-i", jp2_path, "-o", out_path],
            capture_output=True,
        )
        assert result_file.returncode == 0

        # Decompress to stdout
        result_stdout = subprocess.run(
            [GRK_DECOMPRESS, "-i", jp2_path, "--out-fmt", "PNG"],
            capture_output=True,
        )
        assert result_stdout.returncode == 0

        with open(out_path, "rb") as f:
            file_data = f.read()
        assert result_stdout.stdout == file_data


@requires_cli
class TestCompressFromStdin:
    def test_compress_from_stdin_pgm(self, tmp_path):
        """Compress a PGM image from stdin to a file."""
        # Create a PGM image in memory
        width, height, maxval = 32, 32, 255
        pixels = bytes(
            (x + y) % 256 for y in range(height) for x in range(width)
        )
        pgm_data = f"P5\n{width} {height}\n{maxval}\n".encode() + pixels

        out_path = str(tmp_path / "from_stdin.jp2")
        result = subprocess.run(
            [GRK_COMPRESS, "--in-fmt", "PGM", "-o", out_path],
            input=pgm_data,
            capture_output=True,
        )
        assert result.returncode == 0, (
            f"grk_compress from stdin failed: {result.stderr.decode()}"
        )
        assert os.path.getsize(out_path) > 0

        # Verify the output is valid by decompressing it
        verify = subprocess.run(
            [GRK_DECOMPRESS, "-i", out_path, "-o", str(tmp_path / "verify.pgm")],
            capture_output=True,
        )
        assert verify.returncode == 0

    def test_compress_from_stdin_png(self, tmp_path):
        """Compress a PNG image from stdin to a file."""
        # First create a PNG via decompress
        jp2_path, _ = make_test_jp2(tmp_path)
        png_path = str(tmp_path / "input.png")
        subprocess.run(
            [GRK_DECOMPRESS, "-i", jp2_path, "-o", png_path],
            capture_output=True,
            check=True,
        )

        with open(png_path, "rb") as f:
            png_data = f.read()

        out_path = str(tmp_path / "from_stdin.jp2")
        result = subprocess.run(
            [GRK_COMPRESS, "--in-fmt", "PNG", "-o", out_path],
            input=png_data,
            capture_output=True,
        )
        assert result.returncode == 0, (
            f"grk_compress from stdin failed: {result.stderr.decode()}"
        )
        assert os.path.getsize(out_path) > 0
