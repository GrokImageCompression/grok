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

"""Tests for the Grok core library: version, initialization, and basic API."""

import re

import grok_core


class TestVersion:
    def test_version_returns_string(self):
        version = grok_core.grk_version()
        assert isinstance(version, str)

    def test_version_format(self):
        version = grok_core.grk_version()
        assert re.match(r"^\d+\.\d+\.\d+$", version), (
            f"Version string '{version}' does not match X.Y.Z format"
        )

    def test_version_not_empty(self):
        version = grok_core.grk_version()
        assert len(version) > 0


class TestConstants:
    def test_color_spaces(self):
        assert grok_core.GRK_CLRSPC_UNKNOWN == 0
        assert grok_core.GRK_CLRSPC_SRGB == 2
        assert grok_core.GRK_CLRSPC_GRAY == 3
        assert grok_core.GRK_CLRSPC_SYCC == 4

    def test_file_formats(self):
        assert grok_core.GRK_FMT_UNK == 0
        assert grok_core.GRK_FMT_J2K == 1
        assert grok_core.GRK_FMT_JP2 == 2

    def test_progression_orders(self):
        assert grok_core.GRK_LRCP == 0
        assert grok_core.GRK_RLCP == 1
        assert grok_core.GRK_RPCL == 2
        assert grok_core.GRK_PCRL == 3
        assert grok_core.GRK_CPRL == 4

    def test_tile_cache_constants(self):
        assert grok_core.GRK_TILE_CACHE_NONE == 0
        assert grok_core.GRK_TILE_CACHE_IMAGE == 1
        assert grok_core.GRK_TILE_CACHE_ALL == 2


class TestCompressParams:
    def test_default_params(self):
        params = grok_core.grk_cparameters()
        grok_core.grk_compress_set_default_params(params)
        assert params.numresolution == 6
        assert params.cblockw_init == 64
        assert params.cblockh_init == 64
        assert params.prog_order == grok_core.GRK_LRCP
        assert params.irreversible is False

    def test_modify_params(self):
        params = grok_core.grk_cparameters()
        grok_core.grk_compress_set_default_params(params)
        params.numresolution = 3
        params.irreversible = True
        assert params.numresolution == 3
        assert params.irreversible is True


class TestDecompressParams:
    def test_create_decompress_params(self):
        params = grok_core.grk_decompress_parameters()
        assert params is not None

    def test_decompress_core_defaults(self):
        params = grok_core.grk_decompress_parameters()
        assert params.core.reduce == 0


class TestStreamParams:
    def test_create_stream_params(self):
        stream = grok_core.grk_stream_params()
        assert stream is not None

    def test_stream_defaults(self):
        stream = grok_core.grk_stream_params()
        assert stream.is_read_stream is False


class TestImageComponent:
    def test_create_component(self):
        comp = grok_core.grk_image_comp()
        assert comp is not None

    def test_component_fields(self):
        comp = grok_core.grk_image_comp()
        comp.dx = 1
        comp.dy = 1
        comp.w = 256
        comp.h = 256
        comp.prec = 8
        comp.sgnd = False
        assert comp.dx == 1
        assert comp.dy == 1
        assert comp.w == 256
        assert comp.h == 256
        assert comp.prec == 8
        assert comp.sgnd is False
