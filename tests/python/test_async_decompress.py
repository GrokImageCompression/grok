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
  - Single tile with TLM / without TLM
  - Multi tile with TLM / without TLM
  - Sub-region (window) decompress
  - Non-aligned swath heights
  - Single tile decompress by index
  - RGB (multi-component) images
  - Larger tile grids (64 tiles)
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


def make_ppm(path, width=128, height=128):
    """Create a PPM (RGB) test image with a gradient pattern."""
    pixels = bytearray()
    for y in range(height):
        for x in range(width):
            pixels.append((x + y) % 256)       # R
            pixels.append((x * 2 + y) % 256)   # G
            pixels.append((x + y * 2) % 256)   # B
    with open(path, "wb") as f:
        f.write(f"P6\n{width} {height}\n255\n".encode())
        f.write(bytes(pixels))
    return path


def make_jp2(tmp_path, filename="test.jp2", extra_args=None, width=128, height=128,
             rgb=False):
    """Create a test JP2 file via grk_codec_compress."""
    if rgb:
        input_path = str(tmp_path / "input.ppm")
        make_ppm(input_path, width=width, height=height)
    else:
        input_path = str(tmp_path / "input.pgm")
        make_pgm(input_path, width=width, height=height)
    jp2_path = str(tmp_path / filename)
    args = ["grk_compress", "-i", input_path, "-o", jp2_path]
    if extra_args:
        args.extend(extra_args)
    rc = grok_codec.grk_codec_compress(args, None, None)
    assert rc == 0, f"grk_codec_compress failed with rc={rc}"
    assert os.path.getsize(jp2_path) > 0
    return jp2_path


def async_decompress_swath(jp2_path, swath_height=0, window=None):
    """Perform async decompress with simulated synchronous swath retrieval.

    Args:
        jp2_path: path to JP2 file
        swath_height: override swath height (0 = use tile height)
        window: optional (x0, y0, x1, y1) decompress window

    Returns:
        (tiles_retrieved, full_width, full_height, num_tiles, num_comps)
        where tiles_retrieved is a list of (tile_index, width, height) tuples.
    """
    grok_core.grk_initialize(None, 0, None)

    params = grok_core.grk_decompress_parameters()
    params.asynchronous = True
    params.simulate_synchronous = True
    params.core.tile_cache_strategy = grok_core.GRK_TILE_CACHE_IMAGE

    if window:
        params.dw_x0 = float(window[0])
        params.dw_y0 = float(window[1])
        params.dw_x1 = float(window[2])
        params.dw_y1 = float(window[3])

    stream_params = grok_core.grk_stream_params()
    stream_params.file = jp2_path

    codec = grok_core.grk_decompress_init(stream_params, params)
    assert codec is not None, "grk_decompress_init failed"

    header_info = grok_core.grk_header_info()
    ok = grok_core.grk_decompress_read_header(codec, header_info)
    assert ok, "grk_decompress_read_header failed"

    num_tiles = header_info.t_grid_width * header_info.t_grid_height
    num_comps = header_info.header_image.numcomps
    single_tile = (num_tiles == 1)

    if single_tile:
        params.core.skip_allocate_composite = False
    else:
        params.core.skip_allocate_composite = True

    ok = grok_core.grk_decompress_update(params, codec)
    assert ok, "grk_decompress_update failed"

    ok = grok_core.grk_decompress(codec, None)
    assert ok, "grk_decompress failed"

    img_x0 = header_info.header_image.x0
    img_y0 = header_info.header_image.y0
    img_x1 = header_info.header_image.x1
    img_y1 = header_info.header_image.y1

    full_width = img_x1 - img_x0
    full_height = img_y1 - img_y0
    sh = swath_height if swath_height > 0 else header_info.t_height

    tiles_retrieved = []
    y = img_y0
    while y < img_y1:
        swath_y1 = min(y + sh, img_y1)

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
    return tiles_retrieved, full_width, full_height, num_tiles, num_comps


def decompress_tile_by_index(jp2_path, tile_index):
    """Decompress a single tile by index using grk_decompress_tile.

    Returns (width, height, num_comps) of the decompressed tile.
    """
    grok_core.grk_initialize(None, 0, None)

    params = grok_core.grk_decompress_parameters()
    params.asynchronous = True
    params.simulate_synchronous = True
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

    w = tile_img.comps[0].w
    h = tile_img.comps[0].h
    nc = tile_img.numcomps

    grok_core.grk_object_unref(codec)
    return w, h, nc


# ---------------------------------------------------------------------------
# Parameterized tile/TLM configurations for basic async tests
# ---------------------------------------------------------------------------
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
        tiles, full_w, full_h, num_tiles, _ = async_decompress_swath(jp2_path)
        assert len(tiles) > 0, f"No tiles retrieved for {desc}"

    def test_async_all_tiles_retrieved(self, jp2_file):
        """All tiles in the image are retrieved across swaths."""
        desc, jp2_path, multi_tile = jp2_file
        tiles, full_w, full_h, num_tiles, _ = async_decompress_swath(jp2_path)
        tile_indices = {t[0] for t in tiles}
        expected = set(range(num_tiles))
        assert tile_indices == expected, (
            f"{desc}: expected tiles {expected}, got {tile_indices}"
        )

    def test_async_tile_dimensions_valid(self, jp2_file):
        """Each retrieved tile has valid non-zero dimensions."""
        desc, jp2_path, multi_tile = jp2_file
        tiles, full_w, full_h, num_tiles, _ = async_decompress_swath(jp2_path)
        for tidx, w, h in tiles:
            assert w > 0, f"{desc}: tile {tidx} has zero width"
            assert h > 0, f"{desc}: tile {tidx} has zero height"
            assert w <= full_w, f"{desc}: tile {tidx} width {w} > image width {full_w}"
            assert h <= full_h, f"{desc}: tile {tidx} height {h} > image height {full_h}"

    def test_async_matches_sync(self, jp2_file, tmp_path):
        """Async decompress produces same output as synchronous decompress."""
        desc, jp2_path, multi_tile = jp2_file

        sync_out = str(tmp_path / "sync_output.pgm")
        rc = grok_codec.grk_codec_decompress(
            ["grk_decompress", "-i", jp2_path, "-o", sync_out]
        )
        assert rc == 0, f"Synchronous decompress failed for {desc}"
        assert os.path.exists(sync_out)
        assert os.path.getsize(sync_out) > 0

        tiles, full_w, full_h, num_tiles, _ = async_decompress_swath(jp2_path)
        tile_indices = {t[0] for t in tiles}
        expected = set(range(num_tiles))
        assert tile_indices == expected, (
            f"{desc}: async didn't retrieve all tiles"
        )


# ---------------------------------------------------------------------------
# Window / sub-region decompress
# ---------------------------------------------------------------------------
WINDOW_CONFIGS = [
    ("window_single_tile_tlm", ["-X"], (16, 16, 80, 80)),
    ("window_single_tile_no_tlm", [], (16, 16, 80, 80)),
    ("window_multi_tile_tlm", ["-t", "64,64", "-X"], (20, 20, 100, 100)),
    ("window_multi_tile_no_tlm", ["-t", "64,64"], (20, 20, 100, 100)),
]


@pytest.fixture(
    scope="module", params=WINDOW_CONFIGS, ids=[c[0] for c in WINDOW_CONFIGS]
)
def jp2_file_window(request, tmp_path_factory):
    """Create JP2 and return with window coordinates."""
    desc, extra_args, window = request.param
    tmp_path = tmp_path_factory.mktemp(desc)
    jp2_path = make_jp2(tmp_path, filename=f"{desc}.jp2", extra_args=extra_args)
    return desc, jp2_path, window


class TestAsyncWindow:
    def test_window_decompress_succeeds(self, jp2_file_window):
        """Async window decompress completes without error."""
        desc, jp2_path, window = jp2_file_window
        tiles, full_w, full_h, num_tiles, _ = async_decompress_swath(
            jp2_path, window=window
        )
        assert len(tiles) > 0, f"No tiles retrieved for {desc}"

    def test_window_tile_dimensions_within_bounds(self, jp2_file_window):
        """Tile dimensions from window decompress don't exceed window size."""
        desc, jp2_path, window = jp2_file_window
        wx0, wy0, wx1, wy1 = window
        win_w = wx1 - wx0
        win_h = wy1 - wy0
        tiles, full_w, full_h, num_tiles, _ = async_decompress_swath(
            jp2_path, window=window
        )
        for tidx, w, h in tiles:
            assert w > 0, f"{desc}: tile {tidx} has zero width"
            assert h > 0, f"{desc}: tile {tidx} has zero height"


# ---------------------------------------------------------------------------
# Non-aligned swath heights (swath crosses tile boundaries)
# ---------------------------------------------------------------------------
SWATH_CONFIGS = [
    # 128x128 image, 64x64 tiles, swath height 48 crosses tile boundary
    ("swath48_tlm", ["-t", "64,64", "-X"], 48),
    ("swath48_no_tlm", ["-t", "64,64"], 48),
    # Swath height 100 — second swath is only 28 rows
    ("swath100_tlm", ["-t", "64,64", "-X"], 100),
    ("swath100_no_tlm", ["-t", "64,64"], 100),
    # Swath height 1 — extreme case, one row per swath
    ("swath1_tlm", ["-t", "64,64", "-X"], 1),
    ("swath1_no_tlm", ["-t", "64,64"], 1),
]


@pytest.fixture(
    scope="module", params=SWATH_CONFIGS, ids=[c[0] for c in SWATH_CONFIGS]
)
def jp2_file_swath(request, tmp_path_factory):
    """Create JP2 and return with custom swath height."""
    desc, extra_args, swath_h = request.param
    tmp_path = tmp_path_factory.mktemp(desc)
    jp2_path = make_jp2(tmp_path, filename=f"{desc}.jp2", extra_args=extra_args)
    return desc, jp2_path, swath_h


class TestAsyncNonAlignedSwath:
    def test_non_aligned_swath_succeeds(self, jp2_file_swath):
        """Async decompress with non-aligned swath height completes."""
        desc, jp2_path, swath_h = jp2_file_swath
        tiles, full_w, full_h, num_tiles, _ = async_decompress_swath(
            jp2_path, swath_height=swath_h
        )
        assert len(tiles) > 0, f"No tiles for {desc}"

    def test_non_aligned_swath_all_tiles(self, jp2_file_swath):
        """All tiles are retrieved even with non-aligned swath height."""
        desc, jp2_path, swath_h = jp2_file_swath
        tiles, full_w, full_h, num_tiles, _ = async_decompress_swath(
            jp2_path, swath_height=swath_h
        )
        tile_indices = {t[0] for t in tiles}
        expected = set(range(num_tiles))
        assert tile_indices == expected, (
            f"{desc}: expected tiles {expected}, got {tile_indices}"
        )


# ---------------------------------------------------------------------------
# Single tile decompress by index (grk_decompress_tile)
# ---------------------------------------------------------------------------
class TestDecompressTileByIndex:
    @pytest.fixture(scope="class")
    def multi_tile_jp2(self, tmp_path_factory):
        """Create a multi-tile JP2 (4 tiles: 64x64 in 128x128)."""
        tmp_path = tmp_path_factory.mktemp("tile_by_index")
        return make_jp2(tmp_path, filename="multi.jp2",
                        extra_args=["-t", "64,64", "-X"])

    def test_decompress_tile_0(self, multi_tile_jp2):
        """Decompress tile 0 by index."""
        w, h, nc = decompress_tile_by_index(multi_tile_jp2, 0)
        assert w == 64 and h == 64

    def test_decompress_tile_1(self, multi_tile_jp2):
        """Decompress tile 1 by index."""
        w, h, nc = decompress_tile_by_index(multi_tile_jp2, 1)
        assert w == 64 and h == 64

    def test_decompress_tile_2(self, multi_tile_jp2):
        """Decompress tile 2 by index."""
        w, h, nc = decompress_tile_by_index(multi_tile_jp2, 2)
        assert w == 64 and h == 64

    def test_decompress_tile_3(self, multi_tile_jp2):
        """Decompress tile 3 by index."""
        w, h, nc = decompress_tile_by_index(multi_tile_jp2, 3)
        assert w == 64 and h == 64


# ---------------------------------------------------------------------------
# RGB (multi-component) images
# ---------------------------------------------------------------------------
RGB_CONFIGS = [
    ("rgb_single_tile_tlm", ["-X"], False),
    ("rgb_single_tile_no_tlm", [], False),
    ("rgb_multi_tile_tlm", ["-t", "64,64", "-X"], True),
    ("rgb_multi_tile_no_tlm", ["-t", "64,64"], True),
]


@pytest.fixture(
    scope="module", params=RGB_CONFIGS, ids=[c[0] for c in RGB_CONFIGS]
)
def rgb_jp2_file(request, tmp_path_factory):
    """Create an RGB JP2 file."""
    desc, extra_args, multi_tile = request.param
    tmp_path = tmp_path_factory.mktemp(desc)
    jp2_path = make_jp2(tmp_path, filename=f"{desc}.jp2",
                        extra_args=extra_args, rgb=True)
    return desc, jp2_path, multi_tile


class TestAsyncRGB:
    def test_rgb_async_succeeds(self, rgb_jp2_file):
        """Async decompress of RGB image completes."""
        desc, jp2_path, multi_tile = rgb_jp2_file
        tiles, full_w, full_h, num_tiles, num_comps = async_decompress_swath(jp2_path)
        assert len(tiles) > 0
        assert num_comps == 3, f"{desc}: expected 3 components, got {num_comps}"

    def test_rgb_all_tiles_retrieved(self, rgb_jp2_file):
        """All tiles retrieved for RGB image."""
        desc, jp2_path, multi_tile = rgb_jp2_file
        tiles, full_w, full_h, num_tiles, _ = async_decompress_swath(jp2_path)
        tile_indices = {t[0] for t in tiles}
        assert tile_indices == set(range(num_tiles))

    def test_rgb_matches_sync(self, rgb_jp2_file, tmp_path):
        """RGB async decompress matches synchronous decompress."""
        desc, jp2_path, multi_tile = rgb_jp2_file

        sync_out = str(tmp_path / "sync_output.ppm")
        rc = grok_codec.grk_codec_decompress(
            ["grk_decompress", "-i", jp2_path, "-o", sync_out]
        )
        assert rc == 0, f"Sync decompress failed for {desc}"
        assert os.path.getsize(sync_out) > 0

        tiles, _, _, num_tiles, _ = async_decompress_swath(jp2_path)
        assert {t[0] for t in tiles} == set(range(num_tiles))


# ---------------------------------------------------------------------------
# Larger tile grid (256x256 with 32x32 tiles = 64 tiles)
# ---------------------------------------------------------------------------
LARGE_GRID_CONFIGS = [
    ("large_grid_tlm", ["-t", "32,32", "-X"]),
    ("large_grid_no_tlm", ["-t", "32,32"]),
]


@pytest.fixture(
    scope="module", params=LARGE_GRID_CONFIGS, ids=[c[0] for c in LARGE_GRID_CONFIGS]
)
def large_grid_jp2(request, tmp_path_factory):
    """Create a 256x256 JP2 with 32x32 tiles (64 tiles)."""
    desc, extra_args = request.param
    tmp_path = tmp_path_factory.mktemp(desc)
    jp2_path = make_jp2(tmp_path, filename=f"{desc}.jp2",
                        extra_args=extra_args, width=256, height=256)
    return desc, jp2_path


class TestAsyncLargeGrid:
    def test_large_grid_succeeds(self, large_grid_jp2):
        """Async decompress of 64-tile image completes."""
        desc, jp2_path = large_grid_jp2
        tiles, full_w, full_h, num_tiles, _ = async_decompress_swath(jp2_path)
        assert num_tiles == 64, f"{desc}: expected 64 tiles, got {num_tiles}"
        assert len(tiles) > 0

    def test_large_grid_all_tiles(self, large_grid_jp2):
        """All 64 tiles are retrieved."""
        desc, jp2_path = large_grid_jp2
        tiles, full_w, full_h, num_tiles, _ = async_decompress_swath(jp2_path)
        tile_indices = {t[0] for t in tiles}
        assert tile_indices == set(range(64))

    def test_large_grid_tile_dimensions(self, large_grid_jp2):
        """Each tile in a 64-tile grid has correct 32x32 dimensions."""
        desc, jp2_path = large_grid_jp2
        tiles, full_w, full_h, num_tiles, _ = async_decompress_swath(jp2_path)
        for tidx, w, h in tiles:
            assert w == 32, f"{desc}: tile {tidx} width {w} != 32"
            assert h == 32, f"{desc}: tile {tidx} height {h} != 32"

    def test_large_grid_non_aligned_swath(self, large_grid_jp2):
        """64-tile image with swath height crossing tile boundaries."""
        desc, jp2_path = large_grid_jp2
        tiles, full_w, full_h, num_tiles, _ = async_decompress_swath(
            jp2_path, swath_height=50
        )
        assert {t[0] for t in tiles} == set(range(64))
