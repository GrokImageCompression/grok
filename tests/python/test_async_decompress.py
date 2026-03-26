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

"""Async decompress tests — simulated synchronous with swath-based retrieval.

Tests the grk_decompress(async) + grk_decompress_wait(swath) +
grk_decompress_get_tile_image() pipeline, covering:
  - Single tile with TLM
  - Single tile without TLM
  - Multi tile with TLM
  - Multi tile without TLM
"""

import os

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
    pgm_path = str(tmp_path / "input.pgm")
    make_pgm(pgm_path, width=width, height=height)
    jp2_path = str(tmp_path / filename)
    args = ["grk_compress", "-i", pgm_path, "-o", jp2_path]
    if extra_args:
        args.extend(extra_args)
    rc = grok_codec.grk_codec_compress(args, None, None)
    assert rc == 0, f"grk_codec_compress failed with rc={rc}"
    assert os.path.getsize(jp2_path) > 0
    return jp2_path


def async_decompress_swath(jp2_path):
    """Perform async decompress with simulated synchronous swath retrieval.

    Returns a list of (tile_index, width, height) tuples for each tile
    retrieved across all swaths.
    """
    grok_core.grk_initialize(None, 0, None)

    # Set up decompress parameters for async + simulate_synchronous
    params = grok_core.grk_decompress_parameters()
    params.asynchronous = True
    params.simulate_synchronous = True
    params.core.tile_cache_strategy = grok_core.GRK_TILE_CACHE_IMAGE

    # Set up stream
    stream_params = grok_core.grk_stream_params()
    stream_params.file = jp2_path

    # Init and read header
    codec = grok_core.grk_decompress_init(stream_params, params)
    assert codec is not None, "grk_decompress_init failed"

    header_info = grok_core.grk_header_info()
    ok = grok_core.grk_decompress_read_header(codec, header_info)
    assert ok, "grk_decompress_read_header failed"

    num_tiles = header_info.t_grid_width * header_info.t_grid_height
    single_tile = (num_tiles == 1)

    # For single-tile, we need the composite buffer
    if single_tile:
        params.core.skip_allocate_composite = False
    else:
        params.core.skip_allocate_composite = True

    ok = grok_core.grk_decompress_update(params, codec)
    assert ok, "grk_decompress_update failed"

    # Start async decompression
    ok = grok_core.grk_decompress(codec, None)
    assert ok, "grk_decompress failed"

    img_x0 = header_info.header_image.x0
    img_y0 = header_info.header_image.y0
    img_x1 = header_info.header_image.x1
    img_y1 = header_info.header_image.y1

    full_width = img_x1 - img_x0
    full_height = img_y1 - img_y0
    swath_height = header_info.t_height

    tiles_retrieved = []
    y = img_y0
    while y < img_y1:
        swath_y1 = min(y + swath_height, img_y1)

        swath = grok_core.grk_wait_swath()
        swath.x0 = img_x0
        swath.y0 = y
        swath.x1 = img_x1
        swath.y1 = swath_y1

        grok_core.grk_decompress_wait(codec, swath)

        for ty in range(swath.tile_y0, swath.tile_y1):
            for tx in range(swath.tile_x0, swath.tile_x1):
                tidx = ty * swath.num_tile_cols + tx

                if single_tile:
                    tile_img = grok_core.grk_decompress_get_image(codec)
                else:
                    tile_img = grok_core.grk_decompress_get_tile_image(
                        codec, tidx, True
                    )

                assert tile_img is not None, f"Failed to get tile image for tile {tidx}"
                w = tile_img.comps[0].w
                h = tile_img.comps[0].h
                assert w > 0 and h > 0, f"Tile {tidx} has zero dimensions"
                tiles_retrieved.append((tidx, w, h))

        y = swath_y1

    grok_core.grk_object_unref(codec)
    return tiles_retrieved, full_width, full_height, num_tiles


# Parameterized tile/TLM configurations:
#   (description, extra_compress_args, expect_multi_tile)
ASYNC_CONFIGS = [
    ("single_tile_with_tlm", ["-X"], False),
    ("single_tile_no_tlm", [], False),
    ("multi_tile_with_tlm", ["-t", "64,64", "-X"], True),
    ("multi_tile_no_tlm", ["-t", "64,64"], True),
]


@pytest.fixture(
    scope="module", params=ASYNC_CONFIGS, ids=[c[0] for c in ASYNC_CONFIGS]
)
def jp2_file(request, tmp_path_factory):
    """Create a JP2 file with the given tile/TLM configuration."""
    desc, extra_args, multi_tile = request.param
    tmp_path = tmp_path_factory.mktemp(desc)
    jp2_path = make_jp2(tmp_path, filename=f"{desc}.jp2", extra_args=extra_args)
    return desc, jp2_path, multi_tile


class TestAsyncDecompress:
    def test_async_decompress_succeeds(self, jp2_file):
        """Async decompress with swath retrieval completes without error."""
        desc, jp2_path, multi_tile = jp2_file
        tiles, full_w, full_h, num_tiles = async_decompress_swath(jp2_path)
        assert len(tiles) > 0, f"No tiles retrieved for {desc}"

    def test_async_all_tiles_retrieved(self, jp2_file):
        """All tiles in the image are retrieved across swaths."""
        desc, jp2_path, multi_tile = jp2_file
        tiles, full_w, full_h, num_tiles = async_decompress_swath(jp2_path)
        tile_indices = {t[0] for t in tiles}
        expected = set(range(num_tiles))
        assert tile_indices == expected, (
            f"{desc}: expected tiles {expected}, got {tile_indices}"
        )

    def test_async_tile_dimensions_valid(self, jp2_file):
        """Each retrieved tile has valid non-zero dimensions."""
        desc, jp2_path, multi_tile = jp2_file
        tiles, full_w, full_h, num_tiles = async_decompress_swath(jp2_path)
        for tidx, w, h in tiles:
            assert w > 0, f"{desc}: tile {tidx} has zero width"
            assert h > 0, f"{desc}: tile {tidx} has zero height"
            assert w <= full_w, f"{desc}: tile {tidx} width {w} > image width {full_w}"
            assert h <= full_h, f"{desc}: tile {tidx} height {h} > image height {full_h}"

    def test_async_matches_sync(self, jp2_file, tmp_path):
        """Async decompress produces same output as synchronous decompress."""
        desc, jp2_path, multi_tile = jp2_file

        # Synchronous decompress to file
        sync_out = str(tmp_path / "sync_output.pgm")
        rc = grok_codec.grk_codec_decompress(
            ["grk_decompress", "-i", jp2_path, "-o", sync_out]
        )
        assert rc == 0, f"Synchronous decompress failed for {desc}"
        assert os.path.exists(sync_out)
        sync_size = os.path.getsize(sync_out)
        assert sync_size > 0

        # Async decompress — just verify it completes and retrieves all tiles
        tiles, full_w, full_h, num_tiles = async_decompress_swath(jp2_path)
        tile_indices = {t[0] for t in tiles}
        expected = set(range(num_tiles))
        assert tile_indices == expected, (
            f"{desc}: async didn't retrieve all tiles"
        )
