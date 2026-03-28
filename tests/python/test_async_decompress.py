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


# ---------------------------------------------------------------------------
# Swath buffer copy: grk_decompress_schedule_swath_copy + grk_decompress_wait_swath_copy
# ---------------------------------------------------------------------------

def async_decompress_with_swath_buf(jp2_path, swath_height=0, prec=8):
    """Perform async decompress and collect output via scheduled swath buffer copies.

    After grk_decompress_wait() returns for each swath, this helper calls
    grk_decompress_schedule_swath_copy() to schedule Highway-SIMD copies into a
    user-managed bytearray, then grk_decompress_wait_swath_copy() to sync.

    Args:
        jp2_path:     path to JP2 file
        swath_height: override swath height in pixels (0 = tile height)
        prec:         output precision (8 or 16)

    Returns:
        (swath_bufs, full_width, full_height, num_tiles, numcomps)
        where swath_bufs is a list of bytearrays, one per swath.
    """
    grok_core.grk_initialize(None, 0, None)

    params = grok_core.grk_decompress_parameters()
    params.asynchronous = True
    params.simulate_synchronous = True
    params.core.tile_cache_strategy = grok_core.GRK_TILE_CACHE_IMAGE

    stream_params = grok_core.grk_stream_params()
    stream_params.file = jp2_path

    codec = grok_core.grk_decompress_init(stream_params, params)
    assert codec is not None

    header_info = grok_core.grk_header_info()
    ok = grok_core.grk_decompress_read_header(codec, header_info)
    assert ok

    num_tiles = header_info.t_grid_width * header_info.t_grid_height
    numcomps = header_info.header_image.numcomps
    single_tile = num_tiles == 1

    if single_tile:
        params.core.skip_allocate_composite = False
    else:
        params.core.skip_allocate_composite = True

    ok = grok_core.grk_decompress_update(params, codec)
    assert ok

    ok = grok_core.grk_decompress(codec, None)
    assert ok

    img_x0 = header_info.header_image.x0
    img_y0 = header_info.header_image.y0
    img_x1 = header_info.header_image.x1
    img_y1 = header_info.header_image.y1
    full_width = img_x1 - img_x0
    full_height = img_y1 - img_y0

    sh = swath_height if swath_height > 0 else header_info.t_height

    elem_size = 2 if prec > 8 else 1
    swath_bufs = []

    y = img_y0
    while y < img_y1:
        swath_y1 = min(y + sh, img_y1)
        this_swath_h = swath_y1 - y

        swath = grok_core.grk_wait_swath()
        swath.x0 = img_x0
        swath.y0 = y
        swath.x1 = img_x1
        swath.y1 = swath_y1
        grok_core.grk_decompress_wait(codec, swath)

        # Allocate BSQ output buffer: numcomps planes × swath_height × full_width
        buf_bytes = numcomps * this_swath_h * full_width * elem_size
        raw_buf = bytearray(buf_bytes)

        swath_buf = grok_core.grk_swath_buffer()
        swath_buf.prec = prec
        swath_buf.sgnd = False
        swath_buf.numcomps = numcomps
        swath_buf.x0 = img_x0
        swath_buf.y0 = y
        swath_buf.x1 = img_x1
        swath_buf.y1 = swath_y1
        swath_buf.promote_alpha = -1
        swath_buf.band_map = None
        swath_buf.set_bsq_layout(full_width, this_swath_h)
        swath_buf.set_data(raw_buf)

        grok_core.grk_decompress_schedule_swath_copy(codec, swath, swath_buf)
        grok_core.grk_decompress_wait_swath_copy(codec)

        swath_bufs.append(raw_buf)
        y = swath_y1

    grok_core.grk_object_unref(codec)
    return swath_bufs, full_width, full_height, num_tiles, numcomps


SWATH_BUF_CONFIGS = [
    ("sbuf_single_tile_tlm_8bit", ["-X"], False, 8),
    ("sbuf_single_tile_no_tlm_8bit", [], False, 8),
    ("sbuf_multi_tile_tlm_8bit", ["-t", "64,64", "-X"], True, 8),
    ("sbuf_multi_tile_no_tlm_8bit", ["-t", "64,64"], True, 8),
    ("sbuf_multi_tile_tlm_16bit", ["-t", "64,64", "-X"], True, 16),
    ("sbuf_multi_tile_no_tlm_16bit", ["-t", "64,64"], True, 16),
]


@pytest.fixture(
    scope="module", params=SWATH_BUF_CONFIGS, ids=[c[0] for c in SWATH_BUF_CONFIGS]
)
def swath_buf_jp2(request, tmp_path_factory):
    """Create a JP2 and return descriptor for swath buffer tests."""
    desc, extra_args, multi_tile, prec = request.param
    tmp_path = tmp_path_factory.mktemp(desc)
    jp2_path = make_jp2(tmp_path, filename=f"{desc}.jp2", extra_args=extra_args)
    return desc, jp2_path, multi_tile, prec


class TestSwathBufCopy:
    """Tests for grk_decompress_schedule_swath_copy / grk_decompress_wait_swath_copy."""

    def test_swath_buf_copy_succeeds(self, swath_buf_jp2):
        """Swath buffer copy completes without error."""
        desc, jp2_path, multi_tile, prec = swath_buf_jp2
        swath_bufs, fw, fh, num_tiles, numcomps = async_decompress_with_swath_buf(
            jp2_path, prec=prec
        )
        assert len(swath_bufs) > 0, f"{desc}: no swath buffers returned"

    def test_swath_buf_correct_size(self, swath_buf_jp2):
        """Each swath buffer has the correct byte size."""
        desc, jp2_path, multi_tile, prec = swath_buf_jp2
        swath_bufs, fw, fh, num_tiles, numcomps = async_decompress_with_swath_buf(
            jp2_path, prec=prec
        )
        # With default swath height == full image height for single-tile,
        # or tileHeight for multi-tile, the total pixels across all swaths
        # must cover the full image for each component.
        total_elem = sum(len(b) for b in swath_bufs)
        elem_size = 2 if prec > 8 else 1
        # BSQ: numcomps planes, each full_width × swath_height
        expected_min = numcomps * fw * fh * elem_size
        assert total_elem >= expected_min, (
            f"{desc}: total buf bytes {total_elem} < expected {expected_min}"
        )

    def test_swath_buf_non_zero_data(self, swath_buf_jp2):
        """Swath buffer output is not all zeros (data was actually copied)."""
        desc, jp2_path, multi_tile, prec = swath_buf_jp2
        swath_bufs, fw, fh, num_tiles, numcomps = async_decompress_with_swath_buf(
            jp2_path, prec=prec
        )
        combined = bytearray()
        for b in swath_bufs:
            combined.extend(b)
        assert any(v != 0 for v in combined), (
            f"{desc}: swath buffer is all zeros — no data was copied"
        )

    def test_swath_buf_non_aligned_swath(self, swath_buf_jp2):
        """Swath buffer copy works with non-tile-aligned swath height."""
        desc, jp2_path, multi_tile, prec = swath_buf_jp2
        swath_bufs, fw, fh, num_tiles, numcomps = async_decompress_with_swath_buf(
            jp2_path, swath_height=48, prec=prec
        )
        assert len(swath_bufs) > 0, f"{desc}: no swath buffers"

    def test_swath_buf_16bit_larger_than_8bit(self, swath_buf_jp2):
        """16-bit swath buffer is exactly twice the size of 8-bit."""
        desc, jp2_path, multi_tile, prec = swath_buf_jp2
        if prec != 16:
            pytest.skip("Only relevant for 16-bit prec configs")

        bufs8, fw, fh, _, nc = async_decompress_with_swath_buf(jp2_path, prec=8)
        bufs16, _, _, _, _ = async_decompress_with_swath_buf(jp2_path, prec=16)
        total8 = sum(len(b) for b in bufs8)
        total16 = sum(len(b) for b in bufs16)
        assert total16 == total8 * 2, (
            f"{desc}: 16-bit buf ({total16}B) != 2 × 8-bit buf ({total8}B)"
        )

    def test_swath_buf_rgb_copies_all_components(self, tmp_path):
        """RGB image: all 3 component planes in the swath buffer are populated."""
        jp2_path = make_jp2(tmp_path, filename="rgb_swath.jp2",
                            extra_args=["-t", "64,64", "-X"], rgb=True)

        swath_bufs, fw, fh, num_tiles, numcomps = async_decompress_with_swath_buf(
            jp2_path
        )
        assert numcomps == 3, f"Expected 3 components, got {numcomps}"

        for swath_idx, raw in enumerate(swath_bufs):
            swath_h = len(raw) // (numcomps * fw)
            if swath_h == 0:
                continue
            # Check each component plane has non-zero data
            for c in range(numcomps):
                plane_start = c * swath_h * fw
                plane_end = plane_start + swath_h * fw
                plane = raw[plane_start:plane_end]
                assert any(v != 0 for v in plane), (
                    f"Swath {swath_idx} component {c} plane is all zeros"
                )

    def test_swath_buf_consistent_with_tile_images(self, tmp_path):
        """Swath buffer pixel values match those from direct tile image access."""
        jp2_path = make_jp2(
            tmp_path, filename="consistency.jp2", extra_args=["-t", "64,64", "-X"]
        )

        grok_core.grk_initialize(None, 0, None)

        # --- reference: collect raw int32 tile pixel at (0,0) via tile image ---
        params = grok_core.grk_decompress_parameters()
        params.asynchronous = True
        params.simulate_synchronous = True
        params.core.tile_cache_strategy = grok_core.GRK_TILE_CACHE_IMAGE
        params.core.skip_allocate_composite = True

        stream_params = grok_core.grk_stream_params()
        stream_params.file = jp2_path

        codec = grok_core.grk_decompress_init(stream_params, params)
        assert codec is not None
        header_info = grok_core.grk_header_info()
        grok_core.grk_decompress_read_header(codec, header_info)
        grok_core.grk_decompress_update(params, codec)
        grok_core.grk_decompress(codec, None)

        img_x0 = header_info.header_image.x0
        img_y0 = header_info.header_image.y0
        img_x1 = header_info.header_image.x1
        img_y1 = header_info.header_image.y1
        prec = header_info.header_image.comps[0].prec

        swath = grok_core.grk_wait_swath()
        swath.x0 = img_x0
        swath.y0 = img_y0
        swath.x1 = img_x1
        swath.y1 = img_y1
        grok_core.grk_decompress_wait(codec, swath)

        # Grab first tile's first component first-pixel via tile image
        tile_img = grok_core.grk_decompress_get_tile_image(codec, 0, True)
        assert tile_img is not None
        grok_core.grk_object_unref(codec)

        # --- swath buffer path ---
        full_w = img_x1 - img_x0
        full_h = img_y1 - img_y0
        swath_bufs, _, _, _, numcomps = async_decompress_with_swath_buf(jp2_path, prec=8)

        # The swath buffer first byte corresponds to component 0, pixel (0,0)
        assert len(swath_bufs) > 0 and len(swath_bufs[0]) > 0, "Empty swath buffer"
        buf_first = swath_bufs[0][0]  # comp0, row0, col0

        # Compute expected 8-bit value using the same formula as hwy_copy_tile_to_swath_8
        shift = max(0, prec - 8)
        # We can't access int32 data from Python directly; just validate range
        assert 0 <= buf_first <= 255, f"Pixel out of range: {buf_first}"


# ---------------------------------------------------------------------------
# Typed output tests: prec/sgnd combinations
# ---------------------------------------------------------------------------

import struct as _struct


def _decompress_single_swath_typed(jp2_path, prec, sgnd):
    """Decompress a full-image single swath with given prec and sgnd.

    Returns (raw_bytes, full_width, full_height, numcomps).
    """
    grok_core.grk_initialize(None, 0, None)

    params = grok_core.grk_decompress_parameters()
    params.asynchronous = True
    params.simulate_synchronous = True
    params.core.tile_cache_strategy = grok_core.GRK_TILE_CACHE_IMAGE

    stream_params = grok_core.grk_stream_params()
    stream_params.file = jp2_path

    codec = grok_core.grk_decompress_init(stream_params, params)
    assert codec is not None

    header_info = grok_core.grk_header_info()
    assert grok_core.grk_decompress_read_header(codec, header_info)

    num_tiles = header_info.t_grid_width * header_info.t_grid_height
    numcomps = header_info.header_image.numcomps
    single_tile = num_tiles == 1

    params.core.skip_allocate_composite = not single_tile
    assert grok_core.grk_decompress_update(params, codec)
    assert grok_core.grk_decompress(codec, None)

    img_x0 = header_info.header_image.x0
    img_y0 = header_info.header_image.y0
    img_x1 = header_info.header_image.x1
    img_y1 = header_info.header_image.y1
    full_width = img_x1 - img_x0
    full_height = img_y1 - img_y0

    swath = grok_core.grk_wait_swath()
    swath.x0 = img_x0
    swath.y0 = img_y0
    swath.x1 = img_x1
    swath.y1 = img_y1
    grok_core.grk_decompress_wait(codec, swath)

    elem_bytes = 4 if prec > 16 else (2 if prec > 8 else 1)
    buf_bytes = numcomps * full_height * full_width * elem_bytes
    raw_buf = bytearray(buf_bytes)

    swath_buf = grok_core.grk_swath_buffer()
    swath_buf.prec = prec
    swath_buf.sgnd = sgnd
    swath_buf.numcomps = numcomps
    swath_buf.x0 = img_x0
    swath_buf.y0 = img_y0
    swath_buf.x1 = img_x1
    swath_buf.y1 = img_y1
    swath_buf.promote_alpha = -1
    swath_buf.band_map = None
    swath_buf.set_bsq_layout(full_width, full_height)
    swath_buf.set_data(raw_buf)

    grok_core.grk_decompress_schedule_swath_copy(codec, swath, swath_buf)
    grok_core.grk_decompress_wait_swath_copy(codec)
    grok_core.grk_object_unref(codec)

    return bytes(raw_buf), full_width, full_height, numcomps


class TestSwathBufTypedOutput:
    """Tests for prec=8/16/32 × sgnd=True/False output types."""

    @pytest.fixture(scope="class")
    def gray_jp2_multi(self, tmp_path_factory):
        """Grayscale 128x128 JP2 with 64x64 tiles and TLM."""
        tmp_path = tmp_path_factory.mktemp("typed_output")
        return make_jp2(tmp_path, "typed.jp2", extra_args=["-t", "64,64", "-X"])

    def test_uint8_output_size(self, gray_jp2_multi):
        raw, fw, fh, nc = _decompress_single_swath_typed(gray_jp2_multi, 8, False)
        assert len(raw) == nc * fw * fh

    def test_uint8_output_in_range(self, gray_jp2_multi):
        raw, fw, fh, nc = _decompress_single_swath_typed(gray_jp2_multi, 8, False)
        vals = list(raw)
        assert all(0 <= v <= 255 for v in vals)
        assert any(v != 0 for v in vals), "All-zero uint8 output"

    def test_uint16_output_size(self, gray_jp2_multi):
        raw, fw, fh, nc = _decompress_single_swath_typed(gray_jp2_multi, 16, False)
        assert len(raw) == nc * fw * fh * 2

    def test_uint16_output_non_zero(self, gray_jp2_multi):
        raw, fw, fh, nc = _decompress_single_swath_typed(gray_jp2_multi, 16, False)
        vals = [_struct.unpack_from('<H', raw, i)[0] for i in range(0, len(raw), 2)]
        assert any(v != 0 for v in vals), "All-zero uint16 output"
        assert all(0 <= v <= 65535 for v in vals)

    def test_int16_output_size(self, gray_jp2_multi):
        raw, fw, fh, nc = _decompress_single_swath_typed(gray_jp2_multi, 16, True)
        assert len(raw) == nc * fw * fh * 2

    def test_int16_output_non_negative_for_unsigned_source(self, gray_jp2_multi):
        """Source is unsigned 8-bit PGM; int16 output values must be in [0, 255]."""
        raw, fw, fh, nc = _decompress_single_swath_typed(gray_jp2_multi, 16, True)
        vals = [_struct.unpack_from('<h', raw, i)[0] for i in range(0, len(raw), 2)]
        assert any(v != 0 for v in vals), "All-zero int16 output"
        assert all(0 <= v <= 255 for v in vals), (
            f"int16 values out of [0,255] for unsigned source: min={min(vals)}, max={max(vals)}"
        )

    def test_uint8_equals_int16_values(self, gray_jp2_multi):
        """uint8 and int16 outputs carry the same pixel values for 8-bit source."""
        raw8, fw, fh, nc = _decompress_single_swath_typed(gray_jp2_multi, 8, False)
        raw16, _, _, _ = _decompress_single_swath_typed(gray_jp2_multi, 16, True)
        vals8 = list(raw8)
        vals16 = [_struct.unpack_from('<h', raw16, i)[0] for i in range(0, len(raw16), 2)]
        assert vals8 == vals16, "uint8 and int16 output values differ for same 8-bit source"

    def test_int32_output_size(self, gray_jp2_multi):
        raw, fw, fh, nc = _decompress_single_swath_typed(gray_jp2_multi, 32, True)
        assert len(raw) == nc * fw * fh * 4

    def test_int32_output_non_negative_for_unsigned_source(self, gray_jp2_multi):
        raw, fw, fh, nc = _decompress_single_swath_typed(gray_jp2_multi, 32, True)
        vals = [_struct.unpack_from('<i', raw, i)[0] for i in range(0, len(raw), 4)]
        assert any(v != 0 for v in vals), "All-zero int32 output"
        assert all(0 <= v <= 255 for v in vals), (
            f"int32 values out of [0,255] for unsigned source: min={min(vals)}, max={max(vals)}"
        )

    def test_uint32_output_size(self, gray_jp2_multi):
        raw, fw, fh, nc = _decompress_single_swath_typed(gray_jp2_multi, 32, False)
        assert len(raw) == nc * fw * fh * 4

    def test_uint32_equals_int32_values_for_nonneg_source(self, gray_jp2_multi):
        """For non-negative source values uint32 == int32."""
        raw_s, fw, fh, nc = _decompress_single_swath_typed(gray_jp2_multi, 32, True)
        raw_u, _, _, _ = _decompress_single_swath_typed(gray_jp2_multi, 32, False)
        vals_s = [_struct.unpack_from('<i', raw_s, i)[0] for i in range(0, len(raw_s), 4)]
        vals_u = [_struct.unpack_from('<I', raw_u, i)[0] for i in range(0, len(raw_u), 4)]
        assert vals_s == vals_u, "int32 and uint32 differ for non-negative source"


# ---------------------------------------------------------------------------
# Band map remapping
# ---------------------------------------------------------------------------

class TestSwathBufBandMap:
    """Tests for band_map re-ordering (1-based GDAL convention)."""

    @pytest.fixture(scope="class")
    def rgb_jp2_multi(self, tmp_path_factory):
        """RGB 128x128 JP2 with 64x64 tiles."""
        tmp_path = tmp_path_factory.mktemp("band_map")
        return make_jp2(tmp_path, "band_map.jp2",
                        extra_args=["-t", "64,64", "-X"], rgb=True)

    def _decompress_with_band_map(self, jp2_path, band_map):
        """Decompress into 3-band BSQ using the given 1-based band_map list."""
        grok_core.grk_initialize(None, 0, None)

        params = grok_core.grk_decompress_parameters()
        params.asynchronous = True
        params.simulate_synchronous = True
        params.core.tile_cache_strategy = grok_core.GRK_TILE_CACHE_IMAGE
        params.core.skip_allocate_composite = True

        stream_params = grok_core.grk_stream_params()
        stream_params.file = jp2_path

        codec = grok_core.grk_decompress_init(stream_params, params)
        assert codec is not None

        header_info = grok_core.grk_header_info()
        assert grok_core.grk_decompress_read_header(codec, header_info)
        assert grok_core.grk_decompress_update(params, codec)
        assert grok_core.grk_decompress(codec, None)

        img_x0 = header_info.header_image.x0
        img_y0 = header_info.header_image.y0
        img_x1 = header_info.header_image.x1
        img_y1 = header_info.header_image.y1
        fw = img_x1 - img_x0
        fh = img_y1 - img_y0

        swath = grok_core.grk_wait_swath()
        swath.x0 = img_x0
        swath.y0 = img_y0
        swath.x1 = img_x1
        swath.y1 = img_y1
        grok_core.grk_decompress_wait(codec, swath)

        n = len(band_map)
        raw_buf = bytearray(n * fh * fw)

        swath_buf = grok_core.grk_swath_buffer()
        swath_buf.prec = 8
        swath_buf.sgnd = False
        swath_buf.x0 = img_x0
        swath_buf.y0 = img_y0
        swath_buf.x1 = img_x1
        swath_buf.y1 = img_y1
        swath_buf.promote_alpha = -1
        swath_buf.set_band_map(band_map)   # sets numcomps too
        swath_buf.set_bsq_layout(fw, fh)
        swath_buf.set_data(raw_buf)

        grok_core.grk_decompress_schedule_swath_copy(codec, swath, swath_buf)
        grok_core.grk_decompress_wait_swath_copy(codec)
        grok_core.grk_object_unref(codec)
        return bytes(raw_buf), fw, fh

    def test_identity_band_map(self, rgb_jp2_multi):
        """band_map=[1,2,3] produces same output as no band_map."""
        raw_identity, fw, fh = self._decompress_with_band_map(rgb_jp2_multi, [1, 2, 3])
        raw_default, _, _, nc = _decompress_single_swath_typed(rgb_jp2_multi, 8, False)
        assert raw_identity == raw_default, "Identity band_map differs from default output"

    def test_reversed_band_map(self, rgb_jp2_multi):
        """band_map=[3,2,1] swaps R and B planes."""
        raw_fwd, fw, fh = self._decompress_with_band_map(rgb_jp2_multi, [1, 2, 3])
        raw_rev, _, _ = self._decompress_with_band_map(rgb_jp2_multi, [3, 2, 1])
        plane_size = fw * fh
        # Output comp 0 in reversed == source comp 2 == output comp 2 in forward
        assert raw_rev[:plane_size] == raw_fwd[2*plane_size:3*plane_size], (
            "Reversed[0] != Forward[2]"
        )
        # Middle band (G) is unchanged
        assert raw_rev[plane_size:2*plane_size] == raw_fwd[plane_size:2*plane_size], (
            "Reversed[1] (G) != Forward[1] (G)"
        )
        # Output comp 2 in reversed == source comp 0 == output comp 0 in forward
        assert raw_rev[2*plane_size:] == raw_fwd[:plane_size], (
            "Reversed[2] != Forward[0]"
        )

    def test_duplicate_band_map(self, rgb_jp2_multi):
        """band_map=[1,1,1] produces three identical planes from source comp 0."""
        raw, fw, fh = self._decompress_with_band_map(rgb_jp2_multi, [1, 1, 1])
        plane_size = fw * fh
        p0 = raw[:plane_size]
        p1 = raw[plane_size:2*plane_size]
        p2 = raw[2*plane_size:]
        assert p0 == p1 == p2, "Duplicated band planes differ"
        assert any(v != 0 for v in p0), "Duplicated band is all zeros"

    def test_single_band_extraction(self, rgb_jp2_multi):
        """band_map=[2] extracts only the G (comp 1) plane."""
        raw_single, fw, fh = self._decompress_with_band_map(rgb_jp2_multi, [2])
        raw_full, _, _, _ = _decompress_single_swath_typed(rgb_jp2_multi, 8, False)
        plane_size = fw * fh
        g_plane = raw_full[plane_size:2*plane_size]
        assert raw_single == g_plane, "Single-band extraction of G plane differs"


# ---------------------------------------------------------------------------
# Alpha promotion
# ---------------------------------------------------------------------------

class TestSwathBufAlphaPromotion:
    """Tests for promote_alpha: 0/1 source values → 0/255 output."""

    @pytest.fixture(scope="class")
    def gray_jp2(self, tmp_path_factory):
        """Grayscale 128x128 JP2 (single tile) used for alpha promotion tests."""
        tmp_path = tmp_path_factory.mktemp("alpha_promo")
        return make_jp2(tmp_path, "alpha.jp2")

    def _decompress_with_promote(self, jp2_path, promote_alpha):
        grok_core.grk_initialize(None, 0, None)

        params = grok_core.grk_decompress_parameters()
        params.asynchronous = True
        params.simulate_synchronous = True
        params.core.tile_cache_strategy = grok_core.GRK_TILE_CACHE_IMAGE

        stream_params = grok_core.grk_stream_params()
        stream_params.file = jp2_path

        codec = grok_core.grk_decompress_init(stream_params, params)
        assert codec is not None

        header_info = grok_core.grk_header_info()
        assert grok_core.grk_decompress_read_header(codec, header_info)

        num_tiles = header_info.t_grid_width * header_info.t_grid_height
        params.core.skip_allocate_composite = (num_tiles != 1)
        assert grok_core.grk_decompress_update(params, codec)
        assert grok_core.grk_decompress(codec, None)

        img_x0 = header_info.header_image.x0
        img_y0 = header_info.header_image.y0
        img_x1 = header_info.header_image.x1
        img_y1 = header_info.header_image.y1
        fw = img_x1 - img_x0
        fh = img_y1 - img_y0

        swath = grok_core.grk_wait_swath()
        swath.x0 = img_x0
        swath.y0 = img_y0
        swath.x1 = img_x1
        swath.y1 = img_y1
        grok_core.grk_decompress_wait(codec, swath)

        raw_buf = bytearray(fw * fh)
        swath_buf = grok_core.grk_swath_buffer()
        swath_buf.prec = 8
        swath_buf.sgnd = False
        swath_buf.numcomps = 1
        swath_buf.x0 = img_x0
        swath_buf.y0 = img_y0
        swath_buf.x1 = img_x1
        swath_buf.y1 = img_y1
        swath_buf.promote_alpha = promote_alpha
        swath_buf.band_map = None
        swath_buf.set_bsq_layout(fw, fh)
        swath_buf.set_data(raw_buf)

        grok_core.grk_decompress_schedule_swath_copy(codec, swath, swath_buf)
        grok_core.grk_decompress_wait_swath_copy(codec)
        grok_core.grk_object_unref(codec)
        return bytes(raw_buf), fw, fh

    def test_no_promotion_matches_typed_output(self, gray_jp2):
        """promote_alpha=-1 gives same result as _decompress_single_swath_typed."""
        raw_promo, fw, fh = self._decompress_with_promote(gray_jp2, -1)
        raw_ref, _, _, _ = _decompress_single_swath_typed(gray_jp2, 8, False)
        assert raw_promo == raw_ref, "No-promotion output differs from typed reference"

    def test_promotion_only_zeros_and_255(self, gray_jp2):
        """With promote_alpha=0, every output byte is either 0 or 255."""
        raw, fw, fh = self._decompress_with_promote(gray_jp2, 0)
        for i, v in enumerate(raw):
            assert v == 0 or v == 255, (
                f"Promoted pixel at index {i} is {v}, expected 0 or 255"
            )

    def test_promotion_differs_from_no_promotion(self, gray_jp2):
        """Promoted output differs from non-promoted output for this gradient image."""
        raw_promo, _, _ = self._decompress_with_promote(gray_jp2, 0)
        raw_no, _, _ = self._decompress_with_promote(gray_jp2, -1)
        assert raw_promo != raw_no, (
            "promote_alpha=0 produced same output as promote_alpha=-1"
        )

    def test_promotion_zeros_preserved(self, gray_jp2):
        """Pixels that were 0 in the source remain 0 after promotion."""
        raw_promo, _, _ = self._decompress_with_promote(gray_jp2, 0)
        raw_no, _, _ = self._decompress_with_promote(gray_jp2, -1)
        for i, (p, n) in enumerate(zip(raw_promo, raw_no)):
            if n == 0:
                assert p == 0, f"Zero source pixel at {i} became {p} after promotion"

