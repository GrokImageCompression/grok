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

"""Tests for the grok_codec SWIG bindings (codec library wrapper)."""

import os

import pytest

try:
    import grok_codec
except ImportError:
    grok_codec = None

import grok_core

requires_codec = pytest.mark.skipif(
    grok_codec is None,
    reason="grok_codec module not available",
)


def make_pgm(path, width=32, height=32):
    """Create a small PGM (grayscale) test image."""
    pixels = bytes((x + y) % 256 for y in range(height) for x in range(width))
    with open(path, "wb") as f:
        f.write(f"P5\n{width} {height}\n255\n".encode())
        f.write(pixels)
    return path


def make_jp2(tmp_path, filename="test.jp2"):
    """Create a test JP2 file via grk_codec_compress."""
    pgm_path = str(tmp_path / "input.pgm")
    make_pgm(pgm_path)
    jp2_path = str(tmp_path / filename)
    rc = grok_codec.grk_codec_compress(
        ["grk_compress", "-i", pgm_path, "-o", jp2_path], None, None
    )
    assert rc == 0, f"grk_codec_compress failed with rc={rc}"
    assert os.path.getsize(jp2_path) > 0
    return jp2_path


@requires_codec
class TestCodecDecompress:
    def test_decompress_to_file(self, tmp_path):
        """Decompress a JP2 to PGM file via codec bindings."""
        jp2_path = make_jp2(tmp_path)
        out_path = str(tmp_path / "output.pgm")

        rc = grok_codec.grk_codec_decompress(
            ["grk_decompress", "-i", jp2_path, "-o", out_path]
        )
        assert rc == 0
        assert os.path.exists(out_path)
        assert os.path.getsize(out_path) > 0

    def test_decompress_to_png(self, tmp_path):
        """Decompress a JP2 to PNG file via codec bindings."""
        jp2_path = make_jp2(tmp_path)
        out_path = str(tmp_path / "output.png")

        rc = grok_codec.grk_codec_decompress(
            ["grk_decompress", "-i", jp2_path, "-o", out_path]
        )
        assert rc == 0
        assert os.path.exists(out_path)
        assert os.path.getsize(out_path) > 0

    def test_decompress_to_stdout(self, tmp_path):
        """Decompress a JP2 to PNG via stdout using --out-fmt."""
        jp2_path = make_jp2(tmp_path)
        out_path = str(tmp_path / "stdout_output.png")

        # Use --out-fmt to decompress to stdout (output captured by codec)
        rc = grok_codec.grk_codec_decompress(
            ["grk_decompress", "-i", jp2_path, "--out-fmt", "PNG"]
        )
        assert rc == 0


@requires_codec
class TestCodecCompress:
    def test_compress_pgm_to_jp2(self, tmp_path):
        """Compress a PGM to JP2 via codec bindings."""
        pgm_path = make_pgm(str(tmp_path / "input.pgm"))
        jp2_path = str(tmp_path / "output.jp2")

        rc = grok_codec.grk_codec_compress(
            ["grk_compress", "-i", pgm_path, "-o", jp2_path], None, None
        )
        assert rc == 0
        assert os.path.getsize(jp2_path) > 0

    def test_compress_pgm_to_j2k(self, tmp_path):
        """Compress a PGM to J2K (codestream only) via codec bindings."""
        pgm_path = make_pgm(str(tmp_path / "input.pgm"))
        j2k_path = str(tmp_path / "output.j2k")

        rc = grok_codec.grk_codec_compress(
            ["grk_compress", "-i", pgm_path, "-o", j2k_path], None, None
        )
        assert rc == 0
        assert os.path.getsize(j2k_path) > 0

    def test_compress_with_none_image_stream(self, tmp_path):
        """grk_codec_compress accepts None for in_image and out_buffer."""
        pgm_path = make_pgm(str(tmp_path / "input.pgm"))
        jp2_path = str(tmp_path / "output.jp2")

        rc = grok_codec.grk_codec_compress(
            ["grk_compress", "-i", pgm_path, "-o", jp2_path],
            None, None
        )
        assert rc == 0
        assert os.path.getsize(jp2_path) > 0


@requires_codec
class TestCodecDump:
    def test_dump_jp2(self, tmp_path):
        """Dump JP2 header info via codec bindings."""
        jp2_path = make_jp2(tmp_path)

        rc = grok_codec.grk_codec_dump(
            ["grk_dump", "-i", jp2_path]
        )
        assert rc == 0


@requires_codec
class TestCodecRoundTrip:
    def test_compress_then_decompress(self, tmp_path):
        """Full round-trip: PGM -> JP2 -> PGM via codec bindings."""
        pgm_path = make_pgm(str(tmp_path / "input.pgm"), width=64, height=64)
        jp2_path = str(tmp_path / "compressed.jp2")
        out_path = str(tmp_path / "output.pgm")

        # Compress
        rc = grok_codec.grk_codec_compress(
            ["grk_compress", "-i", pgm_path, "-o", jp2_path], None, None
        )
        assert rc == 0

        # Decompress
        rc = grok_codec.grk_codec_decompress(
            ["grk_decompress", "-i", jp2_path, "-o", out_path]
        )
        assert rc == 0

        # Verify output is a valid PGM
        with open(out_path, "rb") as f:
            header = f.read(2)
        assert header in (b"P5", b"P2"), f"Expected PGM header, got {header}"

    def test_roundtrip_preserves_dimensions(self, tmp_path):
        """Verify round-trip preserves image dimensions."""
        width, height = 48, 32
        pgm_path = make_pgm(str(tmp_path / "input.pgm"), width=width, height=height)
        jp2_path = str(tmp_path / "compressed.jp2")
        out_path = str(tmp_path / "output.pgm")

        grok_codec.grk_codec_compress(
            ["grk_compress", "-i", pgm_path, "-o", jp2_path], None, None
        )
        grok_codec.grk_codec_decompress(
            ["grk_decompress", "-i", jp2_path, "-o", out_path]
        )

        # Verify via core API
        stream = grok_core.grk_stream_params()
        stream.file = jp2_path
        dparams = grok_core.grk_decompress_parameters()
        codec = grok_core.grk_decompress_init(stream, dparams)
        assert codec is not None
        header = grok_core.grk_header_info()
        assert grok_core.grk_decompress_read_header(codec, header)
        image = grok_core.grk_decompress_get_image(codec)
        assert image.comps[0].w == width
        assert image.comps[0].h == height
        grok_core.grk_object_unref(codec)


@requires_codec
class TestCodecErrors:
    def test_decompress_nonexistent_file(self):
        """Decompress a nonexistent file should return non-zero."""
        rc = grok_codec.grk_codec_decompress(
            ["grk_decompress", "-i", "/nonexistent/file.jp2", "-o", "/tmp/out.pgm"]
        )
        assert rc != 0

    def test_compress_missing_args(self):
        """Compress with missing arguments should return non-zero."""
        rc = grok_codec.grk_codec_compress(
            ["grk_compress"], None, None
        )
        assert rc != 0

    def test_minimal_args_list(self):
        """Args with only program name should fail gracefully."""
        rc = grok_codec.grk_codec_decompress(["grk_decompress"])
        assert rc != 0
