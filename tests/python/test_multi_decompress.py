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

"""Multi-decompress tests — verify that decompressing a second tile (whose
index comes BEFORE a previously decompressed tile) on the same codec produces
the same pixel data as a fresh single-tile decompress.

This exercises the resetSOTParsing / tilePartSeq_.reset() path for the
non-TLM sequential decompress pipeline.
"""

import ctypes

import pytest

try:
    import grok_core
except ImportError:
    grok_core = None

try:
    import grok_codec
except ImportError:
    grok_codec = None


pytestmark = [
    pytest.mark.skipif(grok_core is None, reason="grok_core module not available"),
    pytest.mark.skipif(grok_codec is None, reason="grok_codec module not available"),
]


def make_pgm(path, width=128, height=128):
    """Create a PGM (grayscale) test image with a gradient pattern."""
    pixels = bytes((x + y) % 256 for y in range(height) for x in range(width))
    with open(path, "wb") as f:
        f.write(f"P5\n{width} {height}\n255\n".encode())
        f.write(pixels)
    return path


def make_jp2(tmp_path, filename="test.jp2", extra_args=None, width=128, height=128):
    """Create a test JP2 file via grk_codec_compress."""
    input_path = str(tmp_path / "input.pgm")
    make_pgm(input_path, width=width, height=height)
    jp2_path = str(tmp_path / filename)
    args = ["grk_compress", "-i", input_path, "-o", jp2_path]
    if extra_args:
        args.extend(extra_args)
    rc = grok_codec.grk_codec_compress(args, None, None)
    assert rc == 0, f"grk_codec_compress failed with rc={rc}"
    return jp2_path


def extract_tile_pixels(tile_img):
    """Extract pixel data from all components of a tile image as a list of
    lists (one per component), with stride padding stripped."""
    pixels = []
    for c in range(tile_img.numcomps):
        comp = tile_img.comps[c]
        n_elements = comp.h * comp.stride
        if comp.data_type == grok_core.GRK_INT_16:
            data_ptr = ctypes.cast(
                int(comp.data),
                ctypes.POINTER(ctypes.c_int16 * n_elements),
            )
        else:
            data_ptr = ctypes.cast(
                int(comp.data),
                ctypes.POINTER(ctypes.c_int32 * n_elements),
            )
        arr = data_ptr.contents
        comp_pixels = []
        for y in range(comp.h):
            for x in range(comp.w):
                comp_pixels.append(arr[y * comp.stride + x])
        pixels.append(comp_pixels)
    return pixels


def decompress_single_tile_fresh(jp2_path, tile_index):
    """Decompress a single tile on a freshly created codec.
    Returns extracted pixel data."""
    grok_core.grk_initialize(None, 0, None)

    params = grok_core.grk_decompress_parameters()
    params.core.tile_cache_strategy = grok_core.GRK_TILE_CACHE_IMAGE

    stream_params = grok_core.grk_stream_params()
    stream_params.file = jp2_path

    codec = grok_core.grk_decompress_init(stream_params, params)
    assert codec is not None, "grk_decompress_init failed"

    header_info = grok_core.grk_header_info()
    ok = grok_core.grk_decompress_read_header(codec, header_info)
    assert ok, "grk_decompress_read_header failed"

    ok = grok_core.grk_decompress_update(params, codec)
    assert ok, "grk_decompress_update failed"

    ok = grok_core.grk_decompress_tile(codec, tile_index)
    assert ok, f"grk_decompress_tile({tile_index}) failed"

    tile_img = grok_core.grk_decompress_get_tile_image(codec, tile_index, True)
    assert tile_img is not None, f"get_tile_image({tile_index}) returned None"

    pixels = extract_tile_pixels(tile_img)

    grok_core.grk_object_unref(codec)
    return pixels


def decompress_two_tiles_same_codec(jp2_path, first_tile, second_tile):
    """Decompress first_tile, then second_tile on the SAME codec.
    Returns extracted pixel data for second_tile only."""
    grok_core.grk_initialize(None, 0, None)

    params = grok_core.grk_decompress_parameters()
    params.core.tile_cache_strategy = grok_core.GRK_TILE_CACHE_IMAGE

    stream_params = grok_core.grk_stream_params()
    stream_params.file = jp2_path

    codec = grok_core.grk_decompress_init(stream_params, params)
    assert codec is not None, "grk_decompress_init failed"

    header_info = grok_core.grk_header_info()
    ok = grok_core.grk_decompress_read_header(codec, header_info)
    assert ok, "grk_decompress_read_header failed"

    ok = grok_core.grk_decompress_update(params, codec)
    assert ok, "grk_decompress_update failed"

    # First decompress: higher-indexed tile
    ok = grok_core.grk_decompress_tile(codec, first_tile)
    assert ok, f"grk_decompress_tile({first_tile}) failed"

    # Second decompress: lower-indexed tile (comes BEFORE the first in the
    # codestream, so SOT parsing must restart from the beginning)
    ok = grok_core.grk_decompress_tile(codec, second_tile)
    assert ok, f"grk_decompress_tile({second_tile}) failed on second decode"

    tile_img = grok_core.grk_decompress_get_tile_image(codec, second_tile, True)
    assert tile_img is not None, f"get_tile_image({second_tile}) returned None"

    pixels = extract_tile_pixels(tile_img)

    grok_core.grk_object_unref(codec)
    return pixels


# ---------------------------------------------------------------------------
# Test configurations: (description, compress extra_args, image_size,
#                        first_tile_index, second_tile_index)
#
# In all cases second_tile < first_tile so the second decode must re-parse
# SOT markers that come before the already-decompressed tile.
# ---------------------------------------------------------------------------
MULTI_DECOMPRESS_CONFIGS = [
    # 2x2 grid (4 tiles of 64x64 in a 128x128 image), no TLM
    ("4tile_no_tlm", ["-t", "64,64"], 128, 3, 0),
    ("4tile_no_tlm_adj", ["-t", "64,64"], 128, 2, 1),
    # 4x4 grid (16 tiles of 32x32 in a 128x128 image), no TLM
    ("16tile_no_tlm", ["-t", "32,32"], 128, 15, 0),
    ("16tile_no_tlm_mid", ["-t", "32,32"], 128, 10, 3),
]


@pytest.fixture(
    scope="module",
    params=MULTI_DECOMPRESS_CONFIGS,
    ids=[c[0] for c in MULTI_DECOMPRESS_CONFIGS],
)
def multi_tile_jp2(request, tmp_path_factory):
    desc, extra_args, size, first_tile, second_tile = request.param
    tmp_path = tmp_path_factory.mktemp(desc)
    jp2_path = make_jp2(tmp_path, filename=f"{desc}.jp2",
                        extra_args=extra_args, width=size, height=size)
    return desc, jp2_path, first_tile, second_tile


class TestMultiDecompress:
    """Verify that a second grk_decompress_tile call on the same codec
    (targeting an earlier tile) produces correct pixel data."""

    def test_second_decompress_succeeds(self, multi_tile_jp2):
        """Second decompress of an earlier tile completes without error."""
        desc, jp2_path, first_tile, second_tile = multi_tile_jp2
        # This will assert internally if decompress fails
        pixels = decompress_two_tiles_same_codec(jp2_path, first_tile, second_tile)
        assert len(pixels) > 0, f"{desc}: no component data"
        assert len(pixels[0]) > 0, f"{desc}: component 0 has no pixels"

    def test_second_decompress_matches_fresh(self, multi_tile_jp2):
        """Pixel data from second decompress matches a fresh single-tile decompress."""
        desc, jp2_path, first_tile, second_tile = multi_tile_jp2

        # Reference: decompress second_tile on a fresh codec
        ref_pixels = decompress_single_tile_fresh(jp2_path, second_tile)

        # Test: decompress first_tile then second_tile on the same codec
        test_pixels = decompress_two_tiles_same_codec(jp2_path, first_tile, second_tile)

        assert len(ref_pixels) == len(test_pixels), (
            f"{desc}: component count mismatch: {len(ref_pixels)} vs {len(test_pixels)}"
        )
        for c in range(len(ref_pixels)):
            assert ref_pixels[c] == test_pixels[c], (
                f"{desc}: component {c} pixel data differs between fresh and "
                f"second decompress"
            )

    def test_first_tile_still_cached(self, multi_tile_jp2):
        """After second decompress, the first tile is still accessible."""
        desc, jp2_path, first_tile, second_tile = multi_tile_jp2

        grok_core.grk_initialize(None, 0, None)

        params = grok_core.grk_decompress_parameters()
        params.core.tile_cache_strategy = grok_core.GRK_TILE_CACHE_IMAGE

        stream_params = grok_core.grk_stream_params()
        stream_params.file = jp2_path

        codec = grok_core.grk_decompress_init(stream_params, params)
        assert codec is not None

        header_info = grok_core.grk_header_info()
        grok_core.grk_decompress_read_header(codec, header_info)
        grok_core.grk_decompress_update(params, codec)

        grok_core.grk_decompress_tile(codec, first_tile)
        grok_core.grk_decompress_tile(codec, second_tile)

        # First tile should still be in the cache
        tile_img = grok_core.grk_decompress_get_tile_image(codec, first_tile, True)
        assert tile_img is not None, (
            f"{desc}: first tile {first_tile} lost from cache after second decompress"
        )
        assert tile_img.comps[0].w > 0
        assert tile_img.comps[0].h > 0

        grok_core.grk_object_unref(codec)

    def test_first_tile_matches_fresh(self, multi_tile_jp2):
        """First tile pixel data is still correct after second decompress."""
        desc, jp2_path, first_tile, second_tile = multi_tile_jp2

        # Reference: decompress first_tile alone on a fresh codec
        ref_pixels = decompress_single_tile_fresh(jp2_path, first_tile)

        # Test: decompress both tiles, then extract first_tile's data
        grok_core.grk_initialize(None, 0, None)

        params = grok_core.grk_decompress_parameters()
        params.core.tile_cache_strategy = grok_core.GRK_TILE_CACHE_IMAGE

        stream_params = grok_core.grk_stream_params()
        stream_params.file = jp2_path

        codec = grok_core.grk_decompress_init(stream_params, params)
        assert codec is not None

        header_info = grok_core.grk_header_info()
        grok_core.grk_decompress_read_header(codec, header_info)
        grok_core.grk_decompress_update(params, codec)

        grok_core.grk_decompress_tile(codec, first_tile)
        grok_core.grk_decompress_tile(codec, second_tile)

        tile_img = grok_core.grk_decompress_get_tile_image(codec, first_tile, True)
        assert tile_img is not None
        test_pixels = extract_tile_pixels(tile_img)

        grok_core.grk_object_unref(codec)

        for c in range(len(ref_pixels)):
            assert ref_pixels[c] == test_pixels[c], (
                f"{desc}: first tile (tile {first_tile}) component {c} pixel data "
                f"corrupted after second decompress"
            )


# ---------------------------------------------------------------------------
# Overlapping region decompress with TLM
#
# Uses a 4x4 tile grid (128x128, 32x32 tiles = 16 tiles):
#
#    0  1  2  3
#    4  5  6  7
#    8  9 10 11
#   12 13 14 15
#
# Region A and Region B overlap so some tiles are decompressed twice —
# the second time they should be served from cache, not re-decompressed.
# ---------------------------------------------------------------------------

def _decompress_tile_set(jp2_path, tile_indices):
    """Decompress a sequence of tiles on a single codec and return
    {tile_index: pixel_data} for all decompressed tiles."""
    grok_core.grk_initialize(None, 0, None)

    params = grok_core.grk_decompress_parameters()
    params.core.tile_cache_strategy = grok_core.GRK_TILE_CACHE_IMAGE

    stream_params = grok_core.grk_stream_params()
    stream_params.file = jp2_path

    codec = grok_core.grk_decompress_init(stream_params, params)
    assert codec is not None

    header_info = grok_core.grk_header_info()
    ok = grok_core.grk_decompress_read_header(codec, header_info)
    assert ok

    ok = grok_core.grk_decompress_update(params, codec)
    assert ok

    for idx in tile_indices:
        ok = grok_core.grk_decompress_tile(codec, idx)
        assert ok, f"grk_decompress_tile({idx}) failed"

    result = {}
    for idx in tile_indices:
        tile_img = grok_core.grk_decompress_get_tile_image(codec, idx, True)
        assert tile_img is not None, f"get_tile_image({idx}) returned None"
        result[idx] = extract_tile_pixels(tile_img)

    grok_core.grk_object_unref(codec)
    return result


# (desc, extra_args, image_size, region_a_tiles, region_b_tiles)
OVERLAP_CONFIGS = [
    # 4x4 grid with TLM: top-left region then center region (overlap at tile 5)
    ("overlap_tlm_corner_center",
     ["-t", "32,32", "-X"], 128,
     [0, 1, 4, 5], [5, 6, 9, 10]),
    # 4x4 grid with TLM: left column then right-shifted column (overlap at col 1)
    ("overlap_tlm_columns",
     ["-t", "32,32", "-X"], 128,
     [0, 1, 4, 5, 8, 9, 12, 13], [1, 2, 5, 6, 9, 10, 13, 14]),
    # 4x4 grid without TLM: same corner-center overlap
    ("overlap_no_tlm_corner_center",
     ["-t", "32,32"], 128,
     [0, 1, 4, 5], [5, 6, 9, 10]),
    # 4x4 grid without TLM: same column overlap
    ("overlap_no_tlm_columns",
     ["-t", "32,32"], 128,
     [0, 1, 4, 5, 8, 9, 12, 13], [1, 2, 5, 6, 9, 10, 13, 14]),
]


@pytest.fixture(
    scope="module",
    params=OVERLAP_CONFIGS,
    ids=[c[0] for c in OVERLAP_CONFIGS],
)
def overlap_jp2(request, tmp_path_factory):
    desc, extra_args, size, region_a, region_b = request.param
    tmp_path = tmp_path_factory.mktemp(desc)
    jp2_path = make_jp2(tmp_path, filename=f"{desc}.jp2",
                        extra_args=extra_args, width=size, height=size)
    return desc, jp2_path, region_a, region_b


class TestOverlappingRegionDecompress:
    """Decompress two overlapping tile regions on the same codec.
    Tiles in the overlap must be served from cache (not re-decompressed)
    and all pixel data must match a fresh single-tile reference."""

    def test_overlapping_decompress_succeeds(self, overlap_jp2):
        """Both regions decompress without error."""
        desc, jp2_path, region_a, region_b = overlap_jp2
        all_tiles = region_a + [t for t in region_b if t not in region_a]
        result = _decompress_tile_set(jp2_path, all_tiles)
        assert len(result) == len(all_tiles)

    def test_overlap_tiles_match_fresh(self, overlap_jp2):
        """Tiles in the overlap produce the same pixels as a fresh decompress."""
        desc, jp2_path, region_a, region_b = overlap_jp2
        overlap = set(region_a) & set(region_b)
        assert len(overlap) > 0, f"{desc}: no overlap between regions"

        # Decompress region A then region B on same codec
        all_tiles = region_a + [t for t in region_b if t not in region_a]
        test_result = _decompress_tile_set(jp2_path, all_tiles)

        for idx in overlap:
            ref_pixels = decompress_single_tile_fresh(jp2_path, idx)
            test_pixels = test_result[idx]
            for c in range(len(ref_pixels)):
                assert ref_pixels[c] == test_pixels[c], (
                    f"{desc}: overlap tile {idx} component {c} differs from fresh"
                )

    def test_region_a_only_tiles_match_fresh(self, overlap_jp2):
        """Tiles exclusive to region A are correct after region B is decoded."""
        desc, jp2_path, region_a, region_b = overlap_jp2
        a_only = set(region_a) - set(region_b)
        if not a_only:
            pytest.skip("No tiles exclusive to region A")

        all_tiles = region_a + [t for t in region_b if t not in region_a]
        test_result = _decompress_tile_set(jp2_path, all_tiles)

        for idx in a_only:
            ref_pixels = decompress_single_tile_fresh(jp2_path, idx)
            test_pixels = test_result[idx]
            for c in range(len(ref_pixels)):
                assert ref_pixels[c] == test_pixels[c], (
                    f"{desc}: region-A-only tile {idx} component {c} differs"
                )

    def test_region_b_only_tiles_match_fresh(self, overlap_jp2):
        """Tiles exclusive to region B (newly decompressed) are correct."""
        desc, jp2_path, region_a, region_b = overlap_jp2
        b_only = set(region_b) - set(region_a)
        assert len(b_only) > 0, f"{desc}: no tiles exclusive to region B"

        all_tiles = region_a + [t for t in region_b if t not in region_a]
        test_result = _decompress_tile_set(jp2_path, all_tiles)

        for idx in b_only:
            ref_pixels = decompress_single_tile_fresh(jp2_path, idx)
            test_pixels = test_result[idx]
            for c in range(len(ref_pixels)):
                assert ref_pixels[c] == test_pixels[c], (
                    f"{desc}: region-B-only tile {idx} component {c} differs"
                )
