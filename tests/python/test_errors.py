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

"""Error handling and edge case tests for the Grok SWIG bindings."""

import os

import grok_core


class TestDecompressErrors:
    def test_nonexistent_file(self):
        stream = grok_core.grk_stream_params()
        stream.file = "/nonexistent/path/to/file.j2k"

        params = grok_core.grk_decompress_parameters()
        codec = grok_core.grk_decompress_init(stream, params)
        assert codec is None

    def test_invalid_file_content(self, tmp_path):
        bad_file = str(tmp_path / "garbage.j2k")
        with open(bad_file, "wb") as f:
            f.write(b"\x00\x01\x02\x03\x04\x05\x06\x07")

        stream = grok_core.grk_stream_params()
        stream.file = bad_file

        params = grok_core.grk_decompress_parameters()
        codec = grok_core.grk_decompress_init(stream, params)
        if codec is not None:
            header = grok_core.grk_header_info()
            result = grok_core.grk_decompress_read_header(codec, header)
            assert result is False
            grok_core.grk_object_unref(codec)

    def test_empty_file(self, tmp_path):
        empty_file = str(tmp_path / "empty.j2k")
        with open(empty_file, "wb") as f:
            pass

        stream = grok_core.grk_stream_params()
        stream.file = empty_file

        params = grok_core.grk_decompress_parameters()
        codec = grok_core.grk_decompress_init(stream, params)
        if codec is not None:
            header = grok_core.grk_header_info()
            result = grok_core.grk_decompress_read_header(codec, header)
            assert result is False
            grok_core.grk_object_unref(codec)


class TestCompressErrors:
    def test_zero_dimension_image(self):
        image = grok_core.grk_image_new_uniform(
            1, 0, 0, 1, 1, 8, False, grok_core.GRK_CLRSPC_GRAY
        )
        assert image is None

    def test_compress_unwritable_path(self):
        image = grok_core.grk_image_new_uniform(
            1, 8, 8, 1, 1, 8, False, grok_core.GRK_CLRSPC_GRAY
        )
        if image is None:
            return

        params = grok_core.grk_cparameters()
        grok_core.grk_compress_set_default_params(params)
        params.cod_format = grok_core.GRK_FMT_J2K

        stream = grok_core.grk_stream_params()
        stream.file = "/nonexistent/dir/out.j2k"

        codec = grok_core.grk_compress_init(stream, params, image)
        if codec is not None:
            length = grok_core.grk_compress(codec, None)
            assert length == 0
            grok_core.grk_object_unref(codec)
        grok_core.grk_object_unref(image.obj)


class TestImageCreation:
    def test_single_component_gray(self):
        comp = grok_core.grk_image_comp()
        comp.w = 64
        comp.h = 64
        comp.dx = 1
        comp.dy = 1
        comp.prec = 8
        comp.sgnd = False
        image = grok_core.grk_image_new(1, comp, grok_core.GRK_CLRSPC_GRAY, True)
        assert image is not None
        assert image.numcomps == 1
        assert image.comps[0].w == 64
        grok_core.grk_object_unref(image.obj)

    def test_multi_component_uniform(self):
        image = grok_core.grk_image_new_uniform(
            3, 128, 128, 1, 1, 8, False, grok_core.GRK_CLRSPC_SRGB
        )
        assert image is not None
        assert image.numcomps == 3
        for i in range(3):
            assert image.comps[i].w == 128
            assert image.comps[i].h == 128
            assert image.comps[i].prec == 8
        grok_core.grk_object_unref(image.obj)

    def test_signed_image(self):
        image = grok_core.grk_image_new_uniform(
            1, 32, 32, 1, 1, 12, True, grok_core.GRK_CLRSPC_GRAY
        )
        assert image is not None
        assert image.comps[0].sgnd is True
        assert image.comps[0].prec == 12
        grok_core.grk_object_unref(image.obj)

    def test_16bit_image(self):
        image = grok_core.grk_image_new_uniform(
            1, 32, 32, 1, 1, 16, False, grok_core.GRK_CLRSPC_GRAY
        )
        assert image is not None
        assert image.comps[0].prec == 16
        grok_core.grk_object_unref(image.obj)
