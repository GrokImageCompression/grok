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

"""Round-trip compression/decompression tests using the Grok SWIG bindings."""

import ctypes
import os
import tempfile

import pytest

import grok_core


class TestCompressDecompressRoundTrip:
    def _compress_to_file(
        self, path, width, height, num_comps, prec, color_space, irreversible=False
    ):
        """Compress a synthetic image to a J2K/JP2 file. Returns True on success."""
        params = grok_core.grk_cparameters()
        grok_core.grk_compress_set_default_params(params)

        if path.endswith(".jp2"):
            params.cod_format = grok_core.GRK_FMT_JP2
        else:
            params.cod_format = grok_core.GRK_FMT_J2K

        params.irreversible = irreversible

        image = grok_core.grk_image_new_uniform(
            num_comps, width, height, 1, 1, prec, False, color_space
        )
        if image is None:
            return False

        # Fill components with a simple gradient pattern
        max_val = (1 << prec) - 1
        for c in range(num_comps):
            comp = image.comps[c]
            data_ptr = ctypes.cast(
                int(comp.data),
                ctypes.POINTER(ctypes.c_int32 * (comp.h * comp.stride)),
            )
            arr = data_ptr.contents
            for y in range(comp.h):
                for x in range(comp.w):
                    arr[y * comp.stride + x] = ((x + y + c) * 37) % (max_val + 1)

        stream = grok_core.grk_stream_params()
        stream.file = path

        codec = grok_core.grk_compress_init(stream, params, image)
        if codec is None:
            grok_core.grk_object_unref(image.obj)
            return False

        length = grok_core.grk_compress(codec, None)
        grok_core.grk_object_unref(codec)
        grok_core.grk_object_unref(image.obj)
        return length > 0

    def _decompress_file(self, path):
        """Decompress a J2K/JP2 file and return (image, codec) or (None, None)."""
        stream = grok_core.grk_stream_params()
        stream.file = path

        params = grok_core.grk_decompress_parameters()
        codec = grok_core.grk_decompress_init(stream, params)
        if codec is None:
            return None, None

        header = grok_core.grk_header_info()
        if not grok_core.grk_decompress_read_header(codec, header):
            grok_core.grk_object_unref(codec)
            return None, None

        image = grok_core.grk_decompress_get_image(codec)
        if image is None:
            grok_core.grk_object_unref(codec)
            return None, None

        if not grok_core.grk_decompress(codec, None):
            grok_core.grk_object_unref(codec)
            return None, None

        return image, codec

    def test_lossless_roundtrip_gray_8bit(self, tmp_path):
        w, h, nc, prec = 64, 64, 1, 8
        jp2 = str(tmp_path / "gray8.j2k")

        assert self._compress_to_file(jp2, w, h, nc, prec, grok_core.GRK_CLRSPC_GRAY)
        assert os.path.exists(jp2)
        assert os.path.getsize(jp2) > 0

        image, codec = self._decompress_file(jp2)
        assert image is not None
        assert image.numcomps == nc
        assert image.comps[0].w == w
        assert image.comps[0].h == h
        assert image.comps[0].prec == prec
        grok_core.grk_object_unref(codec)

    def test_lossless_roundtrip_rgb_8bit(self, tmp_path):
        w, h, nc, prec = 32, 32, 3, 8
        jp2 = str(tmp_path / "rgb8.jp2")

        assert self._compress_to_file(jp2, w, h, nc, prec, grok_core.GRK_CLRSPC_SRGB)

        image, codec = self._decompress_file(jp2)
        assert image is not None
        assert image.numcomps == nc
        for c in range(nc):
            assert image.comps[c].w == w
            assert image.comps[c].h == h
        grok_core.grk_object_unref(codec)

    def test_lossless_pixel_accuracy(self, tmp_path):
        """Verify lossless compression preserves exact pixel values."""
        w, h, nc, prec = 16, 16, 1, 8
        jp2 = str(tmp_path / "exact.j2k")

        max_val = (1 << prec) - 1
        expected = {}
        for y in range(h):
            for x in range(w):
                expected[(x, y)] = ((x + y) * 37) % (max_val + 1)

        assert self._compress_to_file(jp2, w, h, nc, prec, grok_core.GRK_CLRSPC_GRAY)

        image, codec = self._decompress_file(jp2)
        assert image is not None

        comp = image.comps[0]
        if comp.data_type == grok_core.GRK_INT_16:
            data_ptr = ctypes.cast(
                int(comp.data),
                ctypes.POINTER(ctypes.c_int16 * (comp.h * comp.stride)),
            )
        else:
            data_ptr = ctypes.cast(
                int(comp.data),
                ctypes.POINTER(ctypes.c_int32 * (comp.h * comp.stride)),
            )
        arr = data_ptr.contents
        for y in range(h):
            for x in range(w):
                actual = arr[y * comp.stride + x]
                assert (
                    actual == expected[(x, y)]
                ), f"Pixel mismatch at ({x},{y}): expected {expected[(x,y)]}, got {actual}"
        grok_core.grk_object_unref(codec)

    def test_lossy_compression(self, tmp_path):
        w, h, nc, prec = 64, 64, 3, 8
        jp2 = str(tmp_path / "lossy.jp2")

        assert self._compress_to_file(
            jp2, w, h, nc, prec, grok_core.GRK_CLRSPC_SRGB, irreversible=True
        )

        image, codec = self._decompress_file(jp2)
        assert image is not None
        assert image.numcomps == nc
        grok_core.grk_object_unref(codec)

    def test_16bit_roundtrip(self, tmp_path):
        w, h, nc, prec = 32, 32, 1, 16
        jp2 = str(tmp_path / "gray16.j2k")

        assert self._compress_to_file(jp2, w, h, nc, prec, grok_core.GRK_CLRSPC_GRAY)

        image, codec = self._decompress_file(jp2)
        assert image is not None
        assert image.comps[0].prec == 16
        grok_core.grk_object_unref(codec)


class TestHeaderReading:
    def test_read_header_info(self, tmp_path):
        """Compress a file and verify header info matches parameters."""
        w, h, nc, prec = 64, 64, 3, 8
        jp2 = str(tmp_path / "header_test.jp2")

        params = grok_core.grk_cparameters()
        grok_core.grk_compress_set_default_params(params)
        params.cod_format = grok_core.GRK_FMT_JP2
        params.numresolution = 4

        image = grok_core.grk_image_new_uniform(
            nc, w, h, 1, 1, prec, False, grok_core.GRK_CLRSPC_SRGB
        )
        stream = grok_core.grk_stream_params()
        stream.file = jp2
        codec = grok_core.grk_compress_init(stream, params, image)
        assert codec is not None
        length = grok_core.grk_compress(codec, None)
        assert length > 0
        grok_core.grk_object_unref(codec)
        grok_core.grk_object_unref(image.obj)

        # Now read header and verify
        stream2 = grok_core.grk_stream_params()
        stream2.file = jp2
        dparams = grok_core.grk_decompress_parameters()
        codec2 = grok_core.grk_decompress_init(stream2, dparams)
        assert codec2 is not None

        header = grok_core.grk_header_info()
        assert grok_core.grk_decompress_read_header(codec2, header)
        assert header.numresolutions == 4
        assert header.irreversible is False
        grok_core.grk_object_unref(codec2)


class TestInt16Gate:
    """Tests for the int16 vs int32 decode-path selection (grk_get_data_type).
    From BIBO analysis the int16 fixed-point path is viable iff sample precision +
    transform headroom <= 16.  For irreversible 9/7 the headroom is 7 (≈3 bits 2D
    BIBO gain + ≈4 bits fractional precision), so int16 is used only for precision
    <= 9; 10/11/12-bit (incl. DCI) decode via 32-bit float."""

    def _compress_irrev_rgb(self, path, width, height, prec):
        """Compress a synthetic RGB image with irreversible 9/7 + MCT."""
        params = grok_core.grk_cparameters()
        grok_core.grk_compress_set_default_params(params)
        params.cod_format = grok_core.GRK_FMT_J2K
        params.irreversible = True
        params.mct = 1

        image = grok_core.grk_image_new_uniform(
            3, width, height, 1, 1, prec, False, grok_core.GRK_CLRSPC_SRGB
        )
        if image is None:
            return False

        max_val = (1 << prec) - 1
        for c in range(3):
            comp = image.comps[c]
            data_ptr = ctypes.cast(
                int(comp.data),
                ctypes.POINTER(ctypes.c_int32 * (comp.h * comp.stride)),
            )
            arr = data_ptr.contents
            for y in range(comp.h):
                for x in range(comp.w):
                    arr[y * comp.stride + x] = ((x * 13 + y * 7 + c * 41) * 37) % (
                        max_val + 1
                    )

        stream = grok_core.grk_stream_params()
        stream.file = path
        codec = grok_core.grk_compress_init(stream, params, image)
        if codec is None:
            grok_core.grk_object_unref(image.obj)
            return False

        length = grok_core.grk_compress(codec, None)
        grok_core.grk_object_unref(codec)
        grok_core.grk_object_unref(image.obj)
        return length > 0

    def _decompress(self, path):
        """Decompress fully and return (image, codec)."""
        stream = grok_core.grk_stream_params()
        stream.file = path

        params = grok_core.grk_decompress_parameters()
        codec = grok_core.grk_decompress_init(stream, params)
        if codec is None:
            return None, None

        header = grok_core.grk_header_info()
        if not grok_core.grk_decompress_read_header(codec, header):
            grok_core.grk_object_unref(codec)
            return None, None

        image = grok_core.grk_decompress_get_image(codec)
        if image is None:
            grok_core.grk_object_unref(codec)
            return None, None

        if not grok_core.grk_decompress(codec, None):
            grok_core.grk_object_unref(codec)
            return None, None

        return image, codec

    def test_12bit_mct_produces_int32(self, tmp_path):
        """12-bit RGB 9/7 + MCT decodes via 32-bit float (prec + 7 = 19 > 16)."""
        j2k = str(tmp_path / "rgb12_irrev.j2k")
        assert self._compress_irrev_rgb(j2k, 64, 64, 12)

        image, codec = self._decompress(j2k)
        assert image is not None
        for c in range(3):
            assert image.comps[c].data_type == grok_core.GRK_INT_32
        grok_core.grk_object_unref(codec)

    def test_8bit_mct_produces_int16(self, tmp_path):
        """8-bit RGB 9/7 + MCT decodes via the int16 path (prec + 7 = 15 <= 16)."""
        j2k = str(tmp_path / "rgb8_irrev.j2k")
        assert self._compress_irrev_rgb(j2k, 64, 64, 8)

        image, codec = self._decompress(j2k)
        assert image is not None
        for c in range(3):
            assert image.comps[c].data_type == grok_core.GRK_INT_16
        grok_core.grk_object_unref(codec)

    def test_grk_get_data_type_irreversible(self):
        """Irreversible 9/7 (qmfbid=0): int16 iff prec + 7 <= 16, i.e. prec <= 9,
        independent of MCT on decode."""
        # decode, MCT: 8/9-bit -> int16, 10/12-bit -> int32
        assert grok_core.grk_get_data_type(False, 8, True, 0) == grok_core.GRK_INT_16
        assert grok_core.grk_get_data_type(False, 9, True, 0) == grok_core.GRK_INT_16
        assert grok_core.grk_get_data_type(False, 10, True, 0) == grok_core.GRK_INT_32
        assert grok_core.grk_get_data_type(False, 12, True, 0) == grok_core.GRK_INT_32
        # decode, non-MCT (single component): same threshold
        assert grok_core.grk_get_data_type(False, 9, False, 0) == grok_core.GRK_INT_16
        assert grok_core.grk_get_data_type(False, 10, False, 0) == grok_core.GRK_INT_32
        # compress: MCT 9/7 always int32 (forward ICT is float)
        assert grok_core.grk_get_data_type(True, 8, True, 0) == grok_core.GRK_INT_32

    def test_grk_get_data_type_reversible(self):
        """Reversible 5/3 (qmfbid=1): headroom 4 (non-MCT) / 5 (MCT)."""
        # non-MCT: prec + 4 <= 16 -> prec <= 12
        assert grok_core.grk_get_data_type(False, 12, False, 1) == grok_core.GRK_INT_16
        assert grok_core.grk_get_data_type(False, 13, False, 1) == grok_core.GRK_INT_32
        # MCT: prec + 5 <= 16 -> prec <= 11
        assert grok_core.grk_get_data_type(False, 11, True, 1) == grok_core.GRK_INT_16
        assert grok_core.grk_get_data_type(False, 12, True, 1) == grok_core.GRK_INT_32
