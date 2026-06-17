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

"""Single-threaded (inline executor) decode tests.

When grk_initialize() is called with num_threads == 1 the Taskflow executor
is created with zero workers and the whole decode pipeline runs inline on the
calling thread (no worker pool, no producer/consumer threads).  These tests
verify that:

  * single-threaded mode really has zero workers, and
  * decoding single-threaded produces pixel-identical output to multi-threaded
    across tiling, component count, precision, reversibility, reduced
    resolution and the asynchronous decode path used by the GDAL driver.
"""

import ctypes

import pytest

import grok_core

MULTI = 4  # worker count used for the multi-threaded reference


def _init(num_threads):
    grok_core.grk_initialize(None, num_threads, None)


def _compress(
    path,
    width,
    height,
    num_comps,
    prec,
    color_space,
    irreversible=False,
    tile=0,
    numres=6,
):
    """Compress a synthetic gradient image. Returns True on success."""
    params = grok_core.grk_cparameters()
    grok_core.grk_compress_set_default_params(params)
    params.cod_format = (
        grok_core.GRK_FMT_JP2 if path.endswith(".jp2") else grok_core.GRK_FMT_J2K
    )
    params.irreversible = irreversible
    params.numresolution = numres
    if tile:
        params.tile_size_on = True
        params.t_width = tile
        params.t_height = tile

    image = grok_core.grk_image_new_uniform(
        num_comps, width, height, 1, 1, prec, False, color_space
    )
    if image is None:
        return False
    max_val = (1 << prec) - 1
    for c in range(num_comps):
        comp = image.comps[c]
        arr = ctypes.cast(
            int(comp.data), ctypes.POINTER(ctypes.c_int32 * (comp.h * comp.stride))
        ).contents
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


def _extract(image):
    """Return a list (one entry per component) of stride-stripped pixel lists."""
    out = []
    for c in range(image.numcomps):
        comp = image.comps[c]
        n = comp.h * comp.stride
        if comp.data_type == grok_core.GRK_INT_16:
            ptr = ctypes.cast(int(comp.data), ctypes.POINTER(ctypes.c_int16 * n))
        else:
            ptr = ctypes.cast(int(comp.data), ctypes.POINTER(ctypes.c_int32 * n))
        arr = ptr.contents
        out.append(
            [arr[y * comp.stride + x] for y in range(comp.h) for x in range(comp.w)]
        )
    return out


def _decompress(path, num_threads, reduce=0, asynchronous=False):
    """Decode a file with the given thread count; return extracted pixels."""
    _init(num_threads)
    params = grok_core.grk_decompress_parameters()
    params.core.reduce = reduce
    params.asynchronous = asynchronous
    if asynchronous:
        params.simulate_synchronous = True

    stream = grok_core.grk_stream_params()
    stream.file = path
    codec = grok_core.grk_decompress_init(stream, params)
    assert codec is not None
    header = grok_core.grk_header_info()
    assert grok_core.grk_decompress_read_header(codec, header)
    assert grok_core.grk_decompress_update(params, codec)
    assert grok_core.grk_decompress(codec, None)
    if asynchronous:
        grok_core.grk_decompress_wait(codec, None)
    image = grok_core.grk_decompress_get_image(codec)
    assert image is not None
    pixels = _extract(image)
    grok_core.grk_object_unref(codec)
    return pixels


# (name, width, height, num_comps, prec, color_space, irreversible, tile)
_CASES = [
    ("gray8_singletile_rev", 64, 64, 1, 8, grok_core.GRK_CLRSPC_GRAY, False, 0),
    ("gray8_singletile_irrev", 64, 64, 1, 8, grok_core.GRK_CLRSPC_GRAY, True, 0),
    ("rgb8_singletile", 48, 40, 3, 8, grok_core.GRK_CLRSPC_SRGB, False, 0),
    ("gray16_singletile", 50, 50, 1, 16, grok_core.GRK_CLRSPC_GRAY, False, 0),
    ("gray8_multitile", 96, 96, 1, 8, grok_core.GRK_CLRSPC_GRAY, False, 32),
    ("rgb8_multitile", 96, 80, 3, 8, grok_core.GRK_CLRSPC_SRGB, False, 32),
    ("gray16_multitile_irrev", 80, 80, 1, 16, grok_core.GRK_CLRSPC_GRAY, True, 32),
]


@pytest.fixture(autouse=True)
def _restore_threads():
    """Each test sets the thread count; restore the session default afterwards."""
    yield
    grok_core.grk_initialize(None, 0, None)


def test_thread_count_reflects_single_vs_multi():
    # num_threads == 1 selects the inline (zero-worker) executor; the public
    # thread count reports the single calling thread.
    _init(1)
    assert grok_core.grk_num_workers() == 1
    _init(MULTI)
    assert grok_core.grk_num_workers() == MULTI


@pytest.mark.parametrize("case", _CASES, ids=[c[0] for c in _CASES])
def test_single_vs_multi_identical(tmp_path, case):
    name, w, h, nc, prec, cs, irr, tile = case
    jp2 = str(tmp_path / f"{name}.jp2")
    assert _compress(jp2, w, h, nc, prec, cs, irreversible=irr, tile=tile)

    st = _decompress(jp2, 1)
    mt = _decompress(jp2, MULTI)
    assert st == mt, f"{name}: single-threaded output differs from multi-threaded"


@pytest.mark.parametrize("case", _CASES, ids=[c[0] for c in _CASES])
def test_single_threaded_is_deterministic(tmp_path, case):
    name, w, h, nc, prec, cs, irr, tile = case
    jp2 = str(tmp_path / f"{name}.jp2")
    assert _compress(jp2, w, h, nc, prec, cs, irreversible=irr, tile=tile)
    a = _decompress(jp2, 1)
    b = _decompress(jp2, 1)
    assert a == b, f"{name}: single-threaded decode is not deterministic"


@pytest.mark.parametrize("reduce", [1, 2])
def test_reduced_resolution_single_vs_multi(tmp_path, reduce):
    jp2 = str(tmp_path / f"reduce{reduce}.jp2")
    assert _compress(jp2, 128, 128, 1, 8, grok_core.GRK_CLRSPC_GRAY, numres=6)
    st = _decompress(jp2, 1, reduce=reduce)
    mt = _decompress(jp2, MULTI, reduce=reduce)
    assert st == mt


@pytest.mark.parametrize("tile", [0, 32])
def test_async_single_threaded_matches_sync(tmp_path, tile):
    """The asynchronous decode path (used by the GDAL driver) must run inline
    on the calling thread and produce the same pixels as the synchronous path."""
    jp2 = str(tmp_path / f"async_t{tile}.jp2")
    assert _compress(jp2, 96, 96, 3, 8, grok_core.GRK_CLRSPC_SRGB, tile=tile)
    sync_st = _decompress(jp2, 1, asynchronous=False)
    async_st = _decompress(jp2, 1, asynchronous=True)
    assert async_st == sync_st
