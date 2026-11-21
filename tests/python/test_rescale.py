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

"""Tests for the grk_rescale decompression feature (linear value remap)."""

import ctypes
import os

import pytest

import grok_core


def _fill_uniform(image, value):
    """Fill every pixel of every component with a constant int32 value."""
    for c in range(image.numcomps):
        comp = image.comps[c]
        data_ptr = ctypes.cast(
            int(comp.data),
            ctypes.POINTER(ctypes.c_int32 * (comp.h * comp.stride)),
        )
        arr = data_ptr.contents
        for y in range(comp.h):
            for x in range(comp.w):
                arr[y * comp.stride + x] = value


def _compress_uniform(path, w, h, nc, prec, color_space, value):
    """Compress a uniform-valued image to `path`. Returns True on success."""
    params = grok_core.grk_cparameters()
    grok_core.grk_compress_set_default_params(params)
    params.cod_format = (
        grok_core.GRK_FMT_JP2 if path.endswith(".jp2") else grok_core.GRK_FMT_J2K
    )

    image = grok_core.grk_image_new_uniform(nc, w, h, 1, 1, prec, False, color_space)
    assert image is not None
    _fill_uniform(image, value)

    stream = grok_core.grk_stream_params()
    stream.file = path
    codec = grok_core.grk_compress_init(stream, params, image)
    assert codec is not None
    length = grok_core.grk_compress(codec, None)
    grok_core.grk_object_unref(codec)
    grok_core.grk_object_unref(image.obj)
    return length > 0


def _decompress_with_rescale(path, rescale_quad):
    """Decompress `path` applying a single rescale entry. Returns (image, codec).

    The image is returned still owned by the codec — caller must unref codec.
    """
    stream = grok_core.grk_stream_params()
    stream.file = path

    dparams = grok_core.grk_decompress_parameters()
    codec = grok_core.grk_decompress_init(stream, dparams)
    assert codec is not None

    # Build a one-element rescale array. SWIG owns the grk_rescale instance;
    # we attach it to the header so it outlives this scope via the C struct's
    # raw pointer (kept alive by `_holder` reference below).
    r = grok_core.grk_rescale()
    r.src_min, r.src_max, r.dst_min, r.dst_max = rescale_quad

    header = grok_core.grk_header_info()
    header.rescale = r
    header.num_rescale = 1
    header._rescale_holder = r  # prevent GC of r until header is dropped

    if not grok_core.grk_decompress_read_header(codec, header):
        grok_core.grk_object_unref(codec)
        return None, None

    image = grok_core.grk_decompress_get_image(codec)
    assert image is not None

    if not grok_core.grk_decompress(codec, None):
        grok_core.grk_object_unref(codec)
        return None, None

    return image, codec


def _read_pixel(comp, x, y):
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
    return data_ptr.contents[y * comp.stride + x]


class TestRescale:
    def test_forward_mapping_8bit(self, tmp_path):
        """Pixels of value 50 in an 8-bit gray image, rescaled 0..100 -> 0..200,
        should land at 50 * 200/100 = 100 (ratio is exact in floating point)."""
        path = str(tmp_path / "uniform50.j2k")
        assert _compress_uniform(
            path, 16, 16, 1, 8, grok_core.GRK_CLRSPC_GRAY, value=50
        )

        image, codec = _decompress_with_rescale(path, (0.0, 100.0, 0.0, 200.0))
        assert image is not None
        try:
            assert image.numcomps == 1
            comp = image.comps[0]
            assert comp.prec == 8  # dst range 0..200 needs 8 bits
            assert comp.sgnd == 0
            for y in (0, 5, 15):
                for x in (0, 7, 15):
                    assert (
                        _read_pixel(comp, x, y) == 100
                    ), f"pixel ({x},{y}) = {_read_pixel(comp, x, y)}, expected 100"
        finally:
            grok_core.grk_object_unref(codec)

    def test_clamp_to_dst_range(self, tmp_path):
        """Source values above src_max must clamp to dst_max, not overflow."""
        path = str(tmp_path / "uniform200.j2k")
        assert _compress_uniform(path, 8, 8, 1, 8, grok_core.GRK_CLRSPC_GRAY, value=200)

        # Map 0..100 -> 0..255: 200 is outside src; must clamp to dst_max=255.
        image, codec = _decompress_with_rescale(path, (0.0, 100.0, 0.0, 255.0))
        assert image is not None
        try:
            comp = image.comps[0]
            for y in range(comp.h):
                for x in range(comp.w):
                    assert _read_pixel(comp, x, y) == 255
        finally:
            grok_core.grk_object_unref(codec)

    def test_reversed_dst_range(self, tmp_path):
        """Negative-slope rescale (dst_min > dst_max). Source 0 must map to dst_min,
        source 100 to dst_max."""
        path = str(tmp_path / "uniform_low.j2k")
        # Pick a source value at src_min so the expected output is dst_min.
        assert _compress_uniform(path, 8, 8, 1, 8, grok_core.GRK_CLRSPC_GRAY, value=0)

        image, codec = _decompress_with_rescale(path, (0.0, 100.0, 255.0, 0.0))
        assert image is not None
        try:
            comp = image.comps[0]
            assert _read_pixel(comp, 0, 0) == 255  # source 0 -> dst_min=255
        finally:
            grok_core.grk_object_unref(codec)

    def test_no_rescale_when_num_rescale_zero(self, tmp_path):
        """Setting rescale=ptr but num_rescale=0 must leave pixels untouched."""
        path = str(tmp_path / "uniform100b.j2k")
        assert _compress_uniform(path, 8, 8, 1, 8, grok_core.GRK_CLRSPC_GRAY, value=100)

        stream = grok_core.grk_stream_params()
        stream.file = path
        dparams = grok_core.grk_decompress_parameters()
        codec = grok_core.grk_decompress_init(stream, dparams)
        try:
            header = grok_core.grk_header_info()
            r = grok_core.grk_rescale()
            r.src_min, r.src_max, r.dst_min, r.dst_max = 0.0, 200.0, 0.0, 255.0
            header.rescale = r
            header.num_rescale = 0  # disabled
            header._rescale_holder = r

            assert grok_core.grk_decompress_read_header(codec, header)
            image = grok_core.grk_decompress_get_image(codec)
            assert grok_core.grk_decompress(codec, None)
            assert _read_pixel(image.comps[0], 0, 0) == 100
        finally:
            grok_core.grk_object_unref(codec)
