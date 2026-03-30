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

"""Tests for JP2 box metadata round-trip: GeoTIFF UUID, IPR, multiple XML, combined."""

import os

import pytest

import grok_core


def _compress_with_meta(tmp_path, filename, setup_meta):
    """Compress a 16x16 gray JP2 with custom metadata, return the file path."""
    path = str(tmp_path / filename)

    params = grok_core.grk_cparameters()
    grok_core.grk_compress_set_default_params(params)
    params.cod_format = grok_core.GRK_FMT_JP2

    image = grok_core.grk_image_new_uniform(
        1, 16, 16, 1, 1, 8, False, grok_core.GRK_CLRSPC_GRAY
    )
    assert image is not None

    # Ensure image has meta
    if image.meta is None:
        image.meta = grok_core.grk_image_meta_new()
    assert image.meta is not None

    # Let caller set metadata
    setup_meta(image.meta)

    stream = grok_core.grk_stream_params()
    stream.file = path
    codec = grok_core.grk_compress_init(stream, params, image)
    assert codec is not None
    length = grok_core.grk_compress(codec, None)
    assert length > 0
    grok_core.grk_object_unref(codec)
    grok_core.grk_object_unref(image.obj)
    return path


def _decompress_and_read(path):
    """Decompress a JP2 file, return (header_info, image, codec)."""
    stream = grok_core.grk_stream_params()
    stream.file = path

    dparams = grok_core.grk_decompress_parameters()
    codec = grok_core.grk_decompress_init(stream, dparams)
    assert codec is not None

    header = grok_core.grk_header_info()
    assert grok_core.grk_decompress_read_header(codec, header)

    image = grok_core.grk_decompress_get_image(codec)
    assert image is not None
    assert grok_core.grk_decompress(codec, None)

    return header, image, codec


class TestGeoTiffUUID:
    def test_geotiff_roundtrip(self, tmp_path):
        """GeoTIFF UUID data should survive compress/decompress round-trip."""
        geo_data = b"FAKE_GEOTIFF_DATA_FOR_TESTING_1234567890"

        def setup(meta):
            meta.set_geotiff(geo_data)

        path = _compress_with_meta(tmp_path, "geotiff.jp2", setup)
        header, image, codec = _decompress_and_read(path)

        assert image.meta is not None
        result = image.meta.get_geotiff()
        assert result is not None, "GeoTIFF data missing after round-trip"
        assert result == geo_data
        grok_core.grk_object_unref(codec)

    def test_no_geotiff(self, tmp_path):
        """JP2 without GeoTIFF should have None for geotiff."""

        def setup(meta):
            pass

        path = _compress_with_meta(tmp_path, "no_geotiff.jp2", setup)
        header, image, codec = _decompress_and_read(path)

        result = image.meta.get_geotiff()
        assert result is None
        grok_core.grk_object_unref(codec)


class TestIPR:
    def test_ipr_roundtrip(self, tmp_path):
        """IPR box data should survive compress/decompress round-trip."""
        ipr_data = b"<IPR><Rights>Test copyright</Rights></IPR>"

        def setup(meta):
            meta.set_ipr(ipr_data)

        path = _compress_with_meta(tmp_path, "ipr.jp2", setup)
        header, image, codec = _decompress_and_read(path)

        assert image.meta is not None
        result = image.meta.get_ipr()
        assert result is not None, "IPR data missing after round-trip"
        assert result == ipr_data
        grok_core.grk_object_unref(codec)

    def test_no_ipr(self, tmp_path):
        """JP2 without IPR should have None for ipr."""

        def setup(meta):
            pass

        path = _compress_with_meta(tmp_path, "no_ipr.jp2", setup)
        header, image, codec = _decompress_and_read(path)

        result = image.meta.get_ipr()
        assert result is None
        grok_core.grk_object_unref(codec)


class TestNoXmlBoxes:
    def test_no_xml(self, tmp_path):
        """JP2 without XML should report 0 xml boxes."""

        def setup(meta):
            pass

        path = _compress_with_meta(tmp_path, "no_xml.jp2", setup)
        header, image, codec = _decompress_and_read(path)

        xml_data = header.get_xml_data()
        assert xml_data is None
        assert header.num_xml_boxes == 0
        grok_core.grk_object_unref(codec)


class TestCombinedMetadata:
    def test_combined_roundtrip(self, tmp_path):
        """Multiple metadata types should all survive round-trip."""
        geo_data = b"GEOTIFF_TEST_COMBINED"
        ipr_data = b"<IPR>combined</IPR>"
        xmp_data = b"<x:xmpmeta>test</x:xmpmeta>"
        iptc_data = b"IPTC-TEST"

        def setup(meta):
            meta.set_geotiff(geo_data)
            meta.set_ipr(ipr_data)
            meta.set_xmp(xmp_data)
            meta.set_iptc(iptc_data)

        path = _compress_with_meta(tmp_path, "combined.jp2", setup)
        header, image, codec = _decompress_and_read(path)

        assert image.meta.get_geotiff() == geo_data
        assert image.meta.get_ipr() == ipr_data
        assert image.meta.get_xmp() == xmp_data
        assert image.meta.get_iptc() == iptc_data
        grok_core.grk_object_unref(codec)


class TestIPRFlagBinary:
    def test_ipr_flag_set(self, tmp_path):
        """IHDR IPR flag byte should be 1 when IPR data is present."""
        ipr_data = b"<IPR>test</IPR>"

        def setup(meta):
            meta.set_ipr(ipr_data)

        path = _compress_with_meta(tmp_path, "ipr_flag.jp2", setup)

        with open(path, "rb") as f:
            data = f.read()

        # Find IHDR box (type = 'ihdr')
        ihdr_sig = b"ihdr"
        pos = data.find(ihdr_sig)
        assert pos >= 0, "IHDR box not found"

        # IPR byte is at offset 17 from type field start
        # type(4) + HEIGHT(4) + WIDTH(4) + NC(2) + BPC(1) + C(1) + UnkC(1) + IPR(1)
        ipr_byte = data[pos + 17]
        assert ipr_byte == 1, f"IHDR IPR flag is {ipr_byte}, expected 1"

    def test_no_ipr_flag(self, tmp_path):
        """IHDR IPR flag byte should be 0 when no IPR data."""

        def setup(meta):
            pass

        path = _compress_with_meta(tmp_path, "no_ipr_flag.jp2", setup)

        with open(path, "rb") as f:
            data = f.read()

        ihdr_sig = b"ihdr"
        pos = data.find(ihdr_sig)
        assert pos >= 0, "IHDR box not found"

        ipr_byte = data[pos + 17]
        assert ipr_byte == 0, f"IHDR IPR flag is {ipr_byte}, expected 0"


class TestXmlNotInsideJp2h:
    def test_no_xml_inside_jp2h(self, tmp_path):
        """XML boxes must not appear inside jp2h super box."""

        def setup(meta):
            pass

        path = _compress_with_meta(tmp_path, "xml_placement.jp2", setup)

        with open(path, "rb") as f:
            data = f.read()

        # Find jp2h box
        jp2h_sig = b"jp2h"
        pos = data.find(jp2h_sig)
        if pos < 0:
            pytest.skip("No jp2h box found")

        # Get box length from preceding 4 bytes
        box_len = int.from_bytes(data[pos - 4 : pos], "big")
        jp2h_content = data[pos : pos + box_len - 4]

        # Verify no xml box inside
        assert b"xml " not in jp2h_content, "Found xml box inside jp2h"
