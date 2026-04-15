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

"""GPU plugin tests for Grok compress/decompress.

These tests exercise the GPU plugin via the grk_compress and grk_decompress
CLI binaries. They require a CUDA-capable GPU with the plugin built.

Prerequisites:
    - CUDA-capable GPU
    - GPU plugin built and available in build/bin/
    - grk_compress and grk_decompress binaries built

Running via CTest (recommended):
    cd build
    ctest -R python_gpu_tests -V

Running via CTest label filter:
    cd build
    ctest -L gpu -V

Running via pytest directly:
    cd <workspace>
    PYTHONPATH=build/bin:$PYTHONPATH \\
    LD_LIBRARY_PATH=build/bin:$LD_LIBRARY_PATH \\
    pytest tests/python/test_gpu.py --run-gpu -v

These tests are disabled by default and skipped unless --run-gpu is passed.
The CTest target `python_gpu_tests` passes --run-gpu automatically.

All compress operations use -b 32,32 (required for GPU decompress).
CUDA_MODULE_LOADING=EAGER is set in the test environment.
"""

import os
import pathlib
import shutil
import subprocess
import tempfile

import pytest

# Resolve paths relative to this file so tests work regardless of cwd
_THIS_DIR = pathlib.Path(__file__).resolve().parent
_WORKSPACE = _THIS_DIR.parent.parent
_BIN_DIR = _WORKSPACE / "build" / "bin"
_GRK_COMPRESS = _BIN_DIR / "grk_compress"
_GRK_DECOMPRESS = _BIN_DIR / "grk_decompress"
_TEST_DATA = _WORKSPACE.parent / "grok-test-data" / "input"

# Common environment for all GPU tests
_GPU_ENV = {
    **os.environ,
    "CUDA_MODULE_LOADING": "EAGER",
    "GRK_DEBUG": "3",
}

pytestmark = pytest.mark.gpu


def _run(args, check=True):
    """Run a subprocess from the build/bin directory with GPU environment."""
    return subprocess.run(
        args,
        cwd=str(_BIN_DIR),
        env=_GPU_ENV,
        stdin=subprocess.DEVNULL,
        capture_output=True,
        text=True,
        timeout=120,
        check=check,
    )


def _make_synthetic_tiff(path, width=256, height=256, channels=3, depth=8):
    """Create a minimal uncompressed single-strip TIFF for compress input.

    Uses raw TIFF structure — no external dependencies required.
    For depths 10/12, data is stored in 16-bit containers (TIFF standard).
    """
    import struct

    # TIFF stores sub-16 depths > 8 in 16-bit containers
    storage_depth = 16 if depth > 8 else 8
    bytes_per_sample = storage_depth // 8
    image_data_size = width * height * channels * bytes_per_sample

    # IFD entries (sorted by tag, all fitting in 4 bytes → inline values)
    num_entries = 9
    ifd_offset = 8  # right after 8-byte header
    ifd_size = 2 + num_entries * 12 + 4  # count + entries + next-IFD ptr

    # BitsPerSample: inline if 1 channel, else external array
    bps_ext_offset = ifd_offset + ifd_size
    bps_data = b""
    if channels > 1:
        bps_data = struct.pack(f"<{channels}H", *([storage_depth] * channels))

    # Pixel data starts after IFD + BitsPerSample array
    pixel_offset = bps_ext_offset + len(bps_data)

    entries = [
        (256, 3, 1, width),                                    # ImageWidth
        (257, 3, 1, height),                                   # ImageLength
        (258, 3, channels,
         storage_depth if channels == 1 else bps_ext_offset),  # BitsPerSample
        (259, 3, 1, 1),                                        # Compression = None
        (262, 3, 1, 2 if channels >= 3 else 1),                # PhotometricInterpretation
        (273, 4, 1, pixel_offset),                              # StripOffsets (single strip)
        (277, 3, 1, channels),                                  # SamplesPerPixel
        (278, 4, 1, height),                                    # RowsPerStrip = all rows
        (279, 4, 1, image_data_size),                           # StripByteCounts
    ]

    # TIFF header (little-endian)
    header = struct.pack("<2sHI", b"II", 42, ifd_offset)

    # IFD
    ifd = struct.pack("<H", num_entries)
    for tag, dtype, count, value in entries:
        ifd += struct.pack("<HHII", tag, dtype, count, value)
    ifd += struct.pack("<I", 0)  # no next IFD

    # Pixel data — use urandom for speed, pattern doesn't matter for codec tests
    pixel_bytes = os.urandom(image_data_size)

    with open(path, "wb") as f:
        f.write(header)
        f.write(ifd)
        f.write(bps_data)
        f.write(pixel_bytes)


@pytest.fixture
def work_dir(tmp_path):
    """Provide temporary input/output directories for batch tests."""
    src = tmp_path / "input"
    dst = tmp_path / "output"
    src.mkdir()
    dst.mkdir()
    return src, dst


# ---------------------------------------------------------------------------
# Single-image compress tests
# ---------------------------------------------------------------------------


class TestGpuCompressSingle:
    """Single-image GPU compression via grk_compress."""

    def test_lossless_rgb(self, tmp_path):
        """Lossless compress a synthetic RGB TIFF to JP2."""
        tiff = tmp_path / "rgb.tif"
        jp2 = tmp_path / "rgb.jp2"
        _make_synthetic_tiff(str(tiff), width=128, height=128, channels=3)

        result = _run([
            str(_GRK_COMPRESS),
            "-i", str(tiff),
            "-o", str(jp2),
            "-b", "32,32",
        ])
        assert result.returncode == 0, result.stderr
        assert jp2.exists() and jp2.stat().st_size > 0

    def test_lossy_rgb(self, tmp_path):
        """Lossy (irreversible) compress a synthetic RGB TIFF to JP2."""
        tiff = tmp_path / "rgb.tif"
        jp2 = tmp_path / "rgb_lossy.jp2"
        _make_synthetic_tiff(str(tiff), width=128, height=128, channels=3)

        result = _run([
            str(_GRK_COMPRESS),
            "-i", str(tiff),
            "-o", str(jp2),
            "-b", "32,32",
            "-I",
        ])
        assert result.returncode == 0, result.stderr
        assert jp2.exists() and jp2.stat().st_size > 0

    def test_lossless_mono(self, tmp_path):
        """Lossless compress a mono (grayscale) TIFF to JP2."""
        tiff = tmp_path / "mono.tif"
        jp2 = tmp_path / "mono.jp2"
        _make_synthetic_tiff(str(tiff), width=128, height=128, channels=1)

        result = _run([
            str(_GRK_COMPRESS),
            "-i", str(tiff),
            "-o", str(jp2),
            "-n", "1",
            "-b", "32,32",
        ])
        assert result.returncode == 0, result.stderr
        assert jp2.exists() and jp2.stat().st_size > 0

    def test_lossy_mono(self, tmp_path):
        """Lossy compress a mono TIFF to J2K."""
        tiff = tmp_path / "mono.tif"
        j2k = tmp_path / "mono.j2k"
        _make_synthetic_tiff(str(tiff), width=128, height=128, channels=1)

        result = _run([
            str(_GRK_COMPRESS),
            "-i", str(tiff),
            "-o", str(j2k),
            "-n", "1",
            "-b", "32,32",
            "-I",
        ])
        assert result.returncode == 0, result.stderr
        assert j2k.exists() and j2k.stat().st_size > 0


# ---------------------------------------------------------------------------
# Single-image decompress tests
# ---------------------------------------------------------------------------


class TestGpuDecompressSingle:
    """Single-image GPU decompression via grk_decompress."""

    def _compress_first(self, tmp_path, lossy=False):
        """Helper: compress a TIFF so we have a JP2 to decompress."""
        tiff = tmp_path / "input.tif"
        jp2 = tmp_path / "input.jp2"
        _make_synthetic_tiff(str(tiff), width=128, height=128, channels=3)
        args = [
            str(_GRK_COMPRESS),
            "-i", str(tiff),
            "-o", str(jp2),
            "-b", "32,32",
        ]
        if lossy:
            args.append("-I")
        _run(args)
        return jp2

    def test_decompress_lossless(self, tmp_path):
        """Decompress a lossless JP2 back to TIFF."""
        jp2 = self._compress_first(tmp_path, lossy=False)
        out_tif = tmp_path / "output.tif"

        result = _run([
            str(_GRK_DECOMPRESS),
            "-i", str(jp2),
            "-o", str(out_tif),
        ])
        assert result.returncode == 0, result.stderr
        assert out_tif.exists() and out_tif.stat().st_size > 0

    def test_decompress_lossy(self, tmp_path):
        """Decompress a lossy JP2 back to TIFF."""
        jp2 = self._compress_first(tmp_path, lossy=True)
        out_tif = tmp_path / "output.tif"

        result = _run([
            str(_GRK_DECOMPRESS),
            "-i", str(jp2),
            "-o", str(out_tif),
        ])
        assert result.returncode == 0, result.stderr
        assert out_tif.exists() and out_tif.stat().st_size > 0


# ---------------------------------------------------------------------------
# Round-trip tests (compress then decompress)
# ---------------------------------------------------------------------------


class TestGpuRoundTrip:
    """Full compress → decompress round-trip through GPU plugin."""

    def test_roundtrip_lossless_rgb(self, tmp_path):
        """Lossless RGB round-trip: TIFF → JP2 → TIFF."""
        tiff_in = tmp_path / "in.tif"
        jp2 = tmp_path / "rt.jp2"
        tiff_out = tmp_path / "out.tif"
        _make_synthetic_tiff(str(tiff_in), width=64, height=64, channels=3)

        r1 = _run([
            str(_GRK_COMPRESS),
            "-i", str(tiff_in),
            "-o", str(jp2),
            "-b", "32,32",
        ])
        assert r1.returncode == 0, r1.stderr

        r2 = _run([
            str(_GRK_DECOMPRESS),
            "-i", str(jp2),
            "-o", str(tiff_out),
        ])
        assert r2.returncode == 0, r2.stderr
        assert tiff_out.exists() and tiff_out.stat().st_size > 0

    def test_roundtrip_lossy_rgb(self, tmp_path):
        """Lossy RGB round-trip: TIFF → JP2 → TIFF."""
        tiff_in = tmp_path / "in.tif"
        jp2 = tmp_path / "rt_lossy.jp2"
        tiff_out = tmp_path / "out.tif"
        _make_synthetic_tiff(str(tiff_in), width=64, height=64, channels=3)

        r1 = _run([
            str(_GRK_COMPRESS),
            "-i", str(tiff_in),
            "-o", str(jp2),
            "-b", "32,32",
            "-I",
        ])
        assert r1.returncode == 0, r1.stderr

        r2 = _run([
            str(_GRK_DECOMPRESS),
            "-i", str(jp2),
            "-o", str(tiff_out),
        ])
        assert r2.returncode == 0, r2.stderr
        assert tiff_out.exists() and tiff_out.stat().st_size > 0

    def test_roundtrip_mono(self, tmp_path):
        """Mono (grayscale) round-trip."""
        tiff_in = tmp_path / "mono_in.tif"
        jp2 = tmp_path / "mono.jp2"
        tiff_out = tmp_path / "mono_out.tif"
        _make_synthetic_tiff(str(tiff_in), width=64, height=64, channels=1)

        r1 = _run([
            str(_GRK_COMPRESS),
            "-i", str(tiff_in),
            "-o", str(jp2),
            "-n", "1",
            "-b", "32,32",
        ])
        assert r1.returncode == 0, r1.stderr

        r2 = _run([
            str(_GRK_DECOMPRESS),
            "-i", str(jp2),
            "-o", str(tiff_out),
        ])
        assert r2.returncode == 0, r2.stderr
        assert tiff_out.exists() and tiff_out.stat().st_size > 0


# ---------------------------------------------------------------------------
# Batch mode tests
# ---------------------------------------------------------------------------


class TestGpuBatchCompress:
    """Batch GPU compression via --batch-src."""

    def _populate_batch_input(self, src_dir, count=3, channels=3, width=64, height=64):
        """Create several synthetic TIFFs in src_dir."""
        for i in range(count):
            _make_synthetic_tiff(
                str(src_dir / f"img_{i:03d}.tif"),
                width=width,
                height=height,
                channels=channels,
            )

    def test_batch_compress_lossless(self, work_dir):
        """Batch lossless compress multiple TIFFs to JP2."""
        src, dst = work_dir
        self._populate_batch_input(src)

        result = _run([
            str(_GRK_COMPRESS),
            "--batch-src", str(src),
            "--out-dir", str(dst),
            "--out-fmt", "jp2",
            "-b", "32,32",
        ])
        assert result.returncode == 0, result.stderr
        outputs = list(dst.glob("*.jp2"))
        assert len(outputs) >= 3

    def test_batch_compress_lossy(self, work_dir):
        """Batch lossy compress multiple TIFFs to J2K."""
        src, dst = work_dir
        self._populate_batch_input(src)

        result = _run([
            str(_GRK_COMPRESS),
            "--batch-src", str(src),
            "--out-dir", str(dst),
            "--out-fmt", "j2k",
            "-b", "32,32",
            "-I",
        ])
        assert result.returncode == 0, result.stderr
        outputs = list(dst.glob("*.j2k"))
        assert len(outputs) >= 3

    def test_batch_compress_with_repetitions(self, work_dir):
        """Batch compress with -e (repetitions) for stress testing."""
        src, dst = work_dir
        self._populate_batch_input(src, count=2)

        result = _run([
            str(_GRK_COMPRESS),
            "--batch-src", str(src),
            "--out-dir", str(dst),
            "--out-fmt", "jp2",
            "-b", "32,32",
            "-e", "2",
        ])
        assert result.returncode == 0, result.stderr
        outputs = list(dst.glob("*.jp2"))
        assert len(outputs) >= 2


class TestGpuBatchDecompress:
    """Batch GPU decompression via --batch-src."""

    def _batch_compress_first(self, src, compressed, count=3, lossy=False):
        """Helper: batch-compress TIFFs so we have JP2s to decompress."""
        for i in range(count):
            _make_synthetic_tiff(
                str(src / f"img_{i:03d}.tif"),
                width=64, height=64, channels=3,
            )
        args = [
            str(_GRK_COMPRESS),
            "--batch-src", str(src),
            "--out-dir", str(compressed),
            "--out-fmt", "jp2",
            "-b", "32,32",
        ]
        if lossy:
            args.append("-I")
        _run(args)

    def test_batch_decompress(self, tmp_path):
        """Batch decompress JP2s to TIFF."""
        src = tmp_path / "tiffs"
        compressed = tmp_path / "compressed"
        decompressed = tmp_path / "decompressed"
        src.mkdir()
        compressed.mkdir()
        decompressed.mkdir()

        self._batch_compress_first(src, compressed)

        result = _run([
            str(_GRK_DECOMPRESS),
            "--batch-src", str(compressed),
            "--out-dir", str(decompressed),
            "--out-fmt", "tif",
        ])
        assert result.returncode == 0, result.stderr
        outputs = list(decompressed.glob("*.tif"))
        assert len(outputs) >= 3

    def test_batch_decompress_with_repetitions(self, tmp_path):
        """Batch decompress with multiple repetitions."""
        src = tmp_path / "tiffs"
        compressed = tmp_path / "compressed"
        decompressed = tmp_path / "decompressed"
        src.mkdir()
        compressed.mkdir()
        decompressed.mkdir()

        self._batch_compress_first(src, compressed, count=2)

        result = _run([
            str(_GRK_DECOMPRESS),
            "--batch-src", str(compressed),
            "--out-dir", str(decompressed),
            "--out-fmt", "tif",
            "-e", "2",
        ])
        assert result.returncode == 0, result.stderr
        outputs = list(decompressed.glob("*.tif"))
        assert len(outputs) >= 2


# ---------------------------------------------------------------------------
# Batch round-trip
# ---------------------------------------------------------------------------


class TestGpuBatchRoundTrip:
    """Full batch compress → decompress round-trip."""

    def test_batch_roundtrip_lossless(self, tmp_path):
        src = tmp_path / "tiffs"
        compressed = tmp_path / "compressed"
        decompressed = tmp_path / "decompressed"
        src.mkdir()
        compressed.mkdir()
        decompressed.mkdir()

        for i in range(3):
            _make_synthetic_tiff(
                str(src / f"frame_{i:03d}.tif"),
                width=64, height=64, channels=3,
            )

        r1 = _run([
            str(_GRK_COMPRESS),
            "--batch-src", str(src),
            "--out-dir", str(compressed),
            "--out-fmt", "jp2",
            "-b", "32,32",
        ])
        assert r1.returncode == 0, r1.stderr

        r2 = _run([
            str(_GRK_DECOMPRESS),
            "--batch-src", str(compressed),
            "--out-dir", str(decompressed),
            "--out-fmt", "tif",
        ])
        assert r2.returncode == 0, r2.stderr
        assert len(list(decompressed.glob("*.tif"))) >= 3

    def test_batch_roundtrip_lossy(self, tmp_path):
        src = tmp_path / "tiffs"
        compressed = tmp_path / "compressed"
        decompressed = tmp_path / "decompressed"
        src.mkdir()
        compressed.mkdir()
        decompressed.mkdir()

        for i in range(3):
            _make_synthetic_tiff(
                str(src / f"frame_{i:03d}.tif"),
                width=64, height=64, channels=3,
            )

        r1 = _run([
            str(_GRK_COMPRESS),
            "--batch-src", str(src),
            "--out-dir", str(compressed),
            "--out-fmt", "jp2",
            "-b", "32,32",
            "-I",
        ])
        assert r1.returncode == 0, r1.stderr

        r2 = _run([
            str(_GRK_DECOMPRESS),
            "--batch-src", str(compressed),
            "--out-dir", str(decompressed),
            "--out-fmt", "tif",
        ])
        assert r2.returncode == 0, r2.stderr
        assert len(list(decompressed.glob("*.tif"))) >= 3


# ---------------------------------------------------------------------------
# Bit depth tests (8, 10, 12, 16)
# ---------------------------------------------------------------------------


class TestGpuBitDepth:
    """Round-trip tests across different bit depths, lossy and lossless.

    Depths 10/12 are stored in 16-bit TIFF containers (standard practice).
    GPU plugin must handle 8-bit and 16-bit data; 10/12-bit data is stored
    in 16-bit containers with values clamped to the appropriate range.
    """

    @pytest.mark.parametrize("depth", [8, 10, 12, 16])
    def test_lossless_rgb_depth(self, tmp_path, depth):
        """Lossless RGB round-trip at various bit depths."""
        tiff_in = tmp_path / f"rgb_{depth}bit.tif"
        jp2 = tmp_path / f"rgb_{depth}bit.jp2"
        tiff_out = tmp_path / f"rgb_{depth}bit_out.tif"
        _make_synthetic_tiff(str(tiff_in), width=64, height=64, channels=3, depth=depth)

        r1 = _run([
            str(_GRK_COMPRESS),
            "-i", str(tiff_in),
            "-o", str(jp2),
            "-b", "32,32",
        ])
        assert r1.returncode == 0, r1.stderr

        r2 = _run([
            str(_GRK_DECOMPRESS),
            "-i", str(jp2),
            "-o", str(tiff_out),
        ])
        assert r2.returncode == 0, r2.stderr
        assert tiff_out.exists() and tiff_out.stat().st_size > 0

    @pytest.mark.parametrize("depth", [8, 10, 12, 16])
    def test_lossy_rgb_depth(self, tmp_path, depth):
        """Lossy RGB round-trip at various bit depths."""
        tiff_in = tmp_path / f"rgb_{depth}bit_lossy.tif"
        jp2 = tmp_path / f"rgb_{depth}bit_lossy.jp2"
        tiff_out = tmp_path / f"rgb_{depth}bit_lossy_out.tif"
        _make_synthetic_tiff(str(tiff_in), width=64, height=64, channels=3, depth=depth)

        r1 = _run([
            str(_GRK_COMPRESS),
            "-i", str(tiff_in),
            "-o", str(jp2),
            "-b", "32,32",
            "-I",
        ])
        assert r1.returncode == 0, r1.stderr

        r2 = _run([
            str(_GRK_DECOMPRESS),
            "-i", str(jp2),
            "-o", str(tiff_out),
        ])
        assert r2.returncode == 0, r2.stderr
        assert tiff_out.exists() and tiff_out.stat().st_size > 0

    @pytest.mark.parametrize("depth", [8, 10, 12, 16])
    def test_lossless_mono_depth(self, tmp_path, depth):
        """Lossless mono round-trip at various bit depths."""
        tiff_in = tmp_path / f"mono_{depth}bit.tif"
        jp2 = tmp_path / f"mono_{depth}bit.jp2"
        tiff_out = tmp_path / f"mono_{depth}bit_out.tif"
        _make_synthetic_tiff(str(tiff_in), width=64, height=64, channels=1, depth=depth)

        r1 = _run([
            str(_GRK_COMPRESS),
            "-i", str(tiff_in),
            "-o", str(jp2),
            "-n", "1",
            "-b", "32,32",
        ])
        assert r1.returncode == 0, r1.stderr

        r2 = _run([
            str(_GRK_DECOMPRESS),
            "-i", str(jp2),
            "-o", str(tiff_out),
        ])
        assert r2.returncode == 0, r2.stderr
        assert tiff_out.exists() and tiff_out.stat().st_size > 0

    @pytest.mark.parametrize("depth", [8, 10, 12, 16])
    def test_lossy_mono_depth(self, tmp_path, depth):
        """Lossy mono round-trip at various bit depths."""
        tiff_in = tmp_path / f"mono_{depth}bit_lossy.tif"
        jp2 = tmp_path / f"mono_{depth}bit_lossy.jp2"
        tiff_out = tmp_path / f"mono_{depth}bit_lossy_out.tif"
        _make_synthetic_tiff(str(tiff_in), width=64, height=64, channels=1, depth=depth)

        r1 = _run([
            str(_GRK_COMPRESS),
            "-i", str(tiff_in),
            "-o", str(jp2),
            "-n", "1",
            "-b", "32,32",
            "-I",
        ])
        assert r1.returncode == 0, r1.stderr

        r2 = _run([
            str(_GRK_DECOMPRESS),
            "-i", str(jp2),
            "-o", str(tiff_out),
        ])
        assert r2.returncode == 0, r2.stderr
        assert tiff_out.exists() and tiff_out.stat().st_size > 0


# ---------------------------------------------------------------------------
# Cinema 2K mode tests
# ---------------------------------------------------------------------------


class TestGpuCinema:
    """GPU cinema profile compress/decompress tests."""

    def _make_cinema_tiff(self, path):
        """Create a cinema-compatible TIFF (small for test speed)."""
        _make_synthetic_tiff(str(path), width=128, height=64, channels=3, depth=8)

    def test_cinema_2k_compress(self, tmp_path):
        """Compress with --cinema-2k 24 profile."""
        tiff = tmp_path / "cinema.tif"
        jp2 = tmp_path / "cinema.jp2"
        self._make_cinema_tiff(tiff)

        result = _run([
            str(_GRK_COMPRESS),
            "-i", str(tiff),
            "-o", str(jp2),
            "--cinema-2k", "24",
        ])
        assert result.returncode == 0, result.stderr
        assert jp2.exists() and jp2.stat().st_size > 0

    def test_cinema_2k_roundtrip(self, tmp_path):
        """Cinema 2K compress → decompress round-trip."""
        tiff_in = tmp_path / "cinema_in.tif"
        jp2 = tmp_path / "cinema.jp2"
        tiff_out = tmp_path / "cinema_out.tif"
        self._make_cinema_tiff(tiff_in)

        r1 = _run([
            str(_GRK_COMPRESS),
            "-i", str(tiff_in),
            "-o", str(jp2),
            "--cinema-2k", "24",
        ])
        assert r1.returncode == 0, r1.stderr

        r2 = _run([
            str(_GRK_DECOMPRESS),
            "-i", str(jp2),
            "-o", str(tiff_out),
        ])
        assert r2.returncode == 0, r2.stderr
        assert tiff_out.exists() and tiff_out.stat().st_size > 0

    @pytest.mark.skip(reason="Batch cinema compress with small test images is slow; "
                       "use larger images for manual batch cinema testing")
    def test_batch_cinema_2k_roundtrip(self, tmp_path):
        """Batch cinema 2K compress → decompress round-trip."""
        src = tmp_path / "cinema_input"
        compressed = tmp_path / "cinema_compressed"
        decompressed = tmp_path / "cinema_decompressed"
        src.mkdir()
        compressed.mkdir()
        decompressed.mkdir()

        for i in range(2):
            self._make_cinema_tiff(src / f"cinema_{i:03d}.tif")

        r1 = _run([
            str(_GRK_COMPRESS),
            "--batch-src", str(src),
            "--out-dir", str(compressed),
            "--out-fmt", "jp2",
            "--cinema-2k", "24",
        ])
        assert r1.returncode == 0, r1.stderr

        r2 = _run([
            str(_GRK_DECOMPRESS),
            "--batch-src", str(compressed),
            "--out-dir", str(decompressed),
            "--out-fmt", "tif",
        ])
        assert r2.returncode == 0, r2.stderr
        assert len(list(decompressed.glob("*.tif"))) >= 2


# ---------------------------------------------------------------------------
# CPU-only fallback (sanity check that -G -2 bypasses GPU)
# ---------------------------------------------------------------------------


class TestCpuFallback:
    """Verify that -G -2 runs without GPU (CPU-only mode)."""

    def test_compress_cpu_only(self, tmp_path):
        tiff = tmp_path / "cpu.tif"
        jp2 = tmp_path / "cpu.jp2"
        _make_synthetic_tiff(str(tiff), width=64, height=64, channels=3)

        result = _run([
            str(_GRK_COMPRESS),
            "-i", str(tiff),
            "-o", str(jp2),
            "-G", "-2",
        ])
        assert result.returncode == 0, result.stderr
        assert jp2.exists() and jp2.stat().st_size > 0

    def test_decompress_cpu_only(self, tmp_path):
        tiff = tmp_path / "cpu.tif"
        jp2 = tmp_path / "cpu.jp2"
        out_tif = tmp_path / "cpu_out.tif"
        _make_synthetic_tiff(str(tiff), width=64, height=64, channels=3)

        _run([
            str(_GRK_COMPRESS),
            "-i", str(tiff),
            "-o", str(jp2),
            "-G", "-2",
        ])

        result = _run([
            str(_GRK_DECOMPRESS),
            "-i", str(jp2),
            "-o", str(out_tif),
            "-G", "-2",
        ])
        assert result.returncode == 0, result.stderr
        assert out_tif.exists() and out_tif.stat().st_size > 0
