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

"""Tests for compression parameter variations and codec configuration."""

import ctypes
import os

import pytest

import grok_core


def compress_and_read_header(tmp_path, filename, num_comps, width, height, prec,
                              color_space, setup_params):
    """Compress with custom params and return header info from decompressed file."""
    path = str(tmp_path / filename)

    params = grok_core.grk_cparameters()
    grok_core.grk_compress_set_default_params(params)
    setup_params(params)

    if path.endswith(".jp2"):
        params.cod_format = grok_core.GRK_FMT_JP2
    else:
        params.cod_format = grok_core.GRK_FMT_J2K

    image = grok_core.grk_image_new_uniform(
        num_comps, width, height, 1, 1, prec, False, color_space
    )
    assert image is not None

    stream = grok_core.grk_stream_params()
    stream.file = path
    codec = grok_core.grk_compress_init(stream, params, image)
    assert codec is not None
    length = grok_core.grk_compress(codec, None)
    assert length > 0
    grok_core.grk_object_unref(codec)
    grok_core.grk_object_unref(image.obj)

    # Read back
    stream2 = grok_core.grk_stream_params()
    stream2.file = path
    dparams = grok_core.grk_decompress_parameters()
    codec2 = grok_core.grk_decompress_init(stream2, dparams)
    assert codec2 is not None
    header = grok_core.grk_header_info()
    assert grok_core.grk_decompress_read_header(codec2, header)
    return header, codec2


class TestResolutionLevels:
    @pytest.mark.parametrize("num_res", [1, 2, 3, 4, 5, 6])
    def test_resolution_levels(self, tmp_path, num_res):
        def setup(params):
            params.numresolution = num_res

        header, codec = compress_and_read_header(
            tmp_path, f"res{num_res}.j2k", 1, 64, 64, 8,
            grok_core.GRK_CLRSPC_GRAY, setup
        )
        assert header.numresolutions == num_res
        grok_core.grk_object_unref(codec)


class TestCodeBlockSize:
    @pytest.mark.parametrize("cb_size", [32, 64])
    def test_codeblock_size(self, tmp_path, cb_size):
        def setup(params):
            params.cblockw_init = cb_size
            params.cblockh_init = cb_size

        header, codec = compress_and_read_header(
            tmp_path, f"cb{cb_size}.j2k", 1, 64, 64, 8,
            grok_core.GRK_CLRSPC_GRAY, setup
        )
        assert header.cblockw_init == cb_size
        assert header.cblockh_init == cb_size
        grok_core.grk_object_unref(codec)


class TestProgressionOrder:
    @pytest.mark.parametrize("order,name", [
        (grok_core.GRK_LRCP, "LRCP"),
        (grok_core.GRK_RLCP, "RLCP"),
        (grok_core.GRK_RPCL, "RPCL"),
        (grok_core.GRK_PCRL, "PCRL"),
        (grok_core.GRK_CPRL, "CPRL"),
    ])
    def test_progression_order(self, tmp_path, order, name):
        def setup(params):
            params.prog_order = order

        header, codec = compress_and_read_header(
            tmp_path, f"prog_{name}.j2k", 1, 64, 64, 8,
            grok_core.GRK_CLRSPC_GRAY, setup
        )
        assert header.prog_order == order
        grok_core.grk_object_unref(codec)


class TestLossyVsLossless:
    def test_irreversible_flag(self, tmp_path):
        def setup(params):
            params.irreversible = True

        header, codec = compress_and_read_header(
            tmp_path, "lossy.j2k", 1, 64, 64, 8,
            grok_core.GRK_CLRSPC_GRAY, setup
        )
        assert header.irreversible is True
        grok_core.grk_object_unref(codec)

    def test_reversible_flag(self, tmp_path):
        def setup(params):
            params.irreversible = False

        header, codec = compress_and_read_header(
            tmp_path, "lossless.j2k", 1, 64, 64, 8,
            grok_core.GRK_CLRSPC_GRAY, setup
        )
        assert header.irreversible is False
        grok_core.grk_object_unref(codec)

    def test_lossy_smaller_than_lossless(self, tmp_path):
        """Lossy compression should produce smaller output than lossless."""
        lossless_path = str(tmp_path / "lossless.j2k")
        lossy_path = str(tmp_path / "lossy.j2k")

        for path, irreversible in [(lossless_path, False), (lossy_path, True)]:
            params = grok_core.grk_cparameters()
            grok_core.grk_compress_set_default_params(params)
            params.cod_format = grok_core.GRK_FMT_J2K
            params.irreversible = irreversible
            if irreversible:
                grok_core.grk_cparameters_set_layer_rate(params, 0, 20.0)
                params.numlayers = 1
                params.allocation_by_rate_distortion = True

            image = grok_core.grk_image_new_uniform(
                3, 128, 128, 1, 1, 8, False, grok_core.GRK_CLRSPC_SRGB
            )
            assert image is not None

            # Fill with non-trivial data
            for c in range(3):
                comp = image.comps[c]
                data_ptr = ctypes.cast(
                    int(comp.data),
                    ctypes.POINTER(ctypes.c_int32 * (comp.h * comp.stride)),
                )
                arr = data_ptr.contents
                for y in range(comp.h):
                    for x in range(comp.w):
                        arr[y * comp.stride + x] = ((x * 17 + y * 31 + c * 53) % 256)

            stream = grok_core.grk_stream_params()
            stream.file = path
            codec = grok_core.grk_compress_init(stream, params, image)
            assert codec is not None
            length = grok_core.grk_compress(codec, None)
            assert length > 0
            grok_core.grk_object_unref(codec)
            grok_core.grk_object_unref(image.obj)

        lossless_size = os.path.getsize(lossless_path)
        lossy_size = os.path.getsize(lossy_path)
        assert lossy_size < lossless_size, (
            f"Lossy ({lossy_size}) should be smaller than lossless ({lossless_size})"
        )


class TestReducedResolutionDecompress:
    def test_reduced_resolution(self, tmp_path):
        """Decompress at reduced resolution and verify dimensions shrink."""
        w, h = 128, 128
        path = str(tmp_path / "multi_res.j2k")

        params = grok_core.grk_cparameters()
        grok_core.grk_compress_set_default_params(params)
        params.cod_format = grok_core.GRK_FMT_J2K
        params.numresolution = 4

        image = grok_core.grk_image_new_uniform(
            1, w, h, 1, 1, 8, False, grok_core.GRK_CLRSPC_GRAY
        )
        stream = grok_core.grk_stream_params()
        stream.file = path
        codec = grok_core.grk_compress_init(stream, params, image)
        grok_core.grk_compress(codec, None)
        grok_core.grk_object_unref(codec)
        grok_core.grk_object_unref(image.obj)

        # Decompress at reduce=2 (quarter resolution)
        stream2 = grok_core.grk_stream_params()
        stream2.file = path
        dparams = grok_core.grk_decompress_parameters()
        dparams.core.reduce = 2

        codec2 = grok_core.grk_decompress_init(stream2, dparams)
        header = grok_core.grk_header_info()
        grok_core.grk_decompress_read_header(codec2, header)
        dimg = grok_core.grk_decompress_get_image(codec2)
        grok_core.grk_decompress(codec2, None)

        assert dimg is not None
        # At reduce=2, dimensions should be w/4, h/4
        assert dimg.comps[0].w == w // 4
        assert dimg.comps[0].h == h // 4
        grok_core.grk_object_unref(codec2)


class TestTiledCompression:
    def test_tiled_compress(self, tmp_path):
        """Compress with tiles and verify header reports correct tile grid."""
        w, h = 128, 128
        tw, th = 64, 64
        path = str(tmp_path / "tiled.j2k")

        params = grok_core.grk_cparameters()
        grok_core.grk_compress_set_default_params(params)
        params.cod_format = grok_core.GRK_FMT_J2K
        params.tile_size_on = True
        params.t_width = tw
        params.t_height = th

        image = grok_core.grk_image_new_uniform(
            1, w, h, 1, 1, 8, False, grok_core.GRK_CLRSPC_GRAY
        )
        stream = grok_core.grk_stream_params()
        stream.file = path
        codec = grok_core.grk_compress_init(stream, params, image)
        assert codec is not None
        length = grok_core.grk_compress(codec, None)
        assert length > 0
        grok_core.grk_object_unref(codec)
        grok_core.grk_object_unref(image.obj)

        # Verify in header
        stream2 = grok_core.grk_stream_params()
        stream2.file = path
        dparams = grok_core.grk_decompress_parameters()
        codec2 = grok_core.grk_decompress_init(stream2, dparams)
        header = grok_core.grk_header_info()
        grok_core.grk_decompress_read_header(codec2, header)
        assert header.t_width == tw
        assert header.t_height == th
        assert header.t_grid_width == w // tw
        assert header.t_grid_height == h // th
        grok_core.grk_object_unref(codec2)


class TestJP2Format:
    def test_jp2_vs_j2k_format(self, tmp_path):
        """JP2 files should be larger than J2K due to container overhead."""
        j2k_path = str(tmp_path / "test.j2k")
        jp2_path = str(tmp_path / "test.jp2")

        for path, fmt in [(j2k_path, grok_core.GRK_FMT_J2K),
                          (jp2_path, grok_core.GRK_FMT_JP2)]:
            params = grok_core.grk_cparameters()
            grok_core.grk_compress_set_default_params(params)
            params.cod_format = fmt

            image = grok_core.grk_image_new_uniform(
                1, 32, 32, 1, 1, 8, False, grok_core.GRK_CLRSPC_GRAY
            )
            stream = grok_core.grk_stream_params()
            stream.file = path
            codec = grok_core.grk_compress_init(stream, params, image)
            grok_core.grk_compress(codec, None)
            grok_core.grk_object_unref(codec)
            grok_core.grk_object_unref(image.obj)

        # JP2 has container overhead
        j2k_size = os.path.getsize(j2k_path)
        jp2_size = os.path.getsize(jp2_path)
        assert jp2_size > j2k_size
