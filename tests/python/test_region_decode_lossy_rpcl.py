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

"""Regression test for a lossy RPCL region-decode corruption.

A windowed (region) decode must match the corresponding crop of a full-image
decode (within the ~1 LSB tolerance inherent to irreversible reconstruction
over a window).

This guards against a decoder bug where the packet-header parser's segment
pass accounting (Segment::totalPasses_) was only advanced by parsePacketData.
For precincts outside the decode window, packet *data* is intentionally not
parsed, yet their packet *headers* must still be parsed to keep the codestream
byte-aligned (no PLT skipping). The stale pass count made readPacketHeader
miscompute segment boundaries / length bits at higher layers, desyncing the
stream and producing a "Corrupt inclusion tag tree" warning followed by a
grey/garbled block.

The bug requires the same ingredients as the original report (issue #409):
irreversible transform, RPCL progression, multiple explicit-rate quality
layers, bypass coding mode (small per-segment pass limits), and small
precincts so the window leaves whole precincts unparsed.
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


def make_ppm(path, width, height):
    """Write a high-entropy RGB PPM (P6).

    The pattern is deterministic but compresses poorly, so code blocks span
    many bit-planes / coding passes across layers, which is what exercises the
    segment-boundary accounting in the packet-header parser.
    """
    buf = bytearray(width * height * 3)
    i = 0
    for y in range(height):
        for x in range(width):
            buf[i] = (x * 37 + y * 17 + ((x * y) >> 3)) & 0xFF
            buf[i + 1] = (x * 11 + y * 53 + ((x ^ y) << 1)) & 0xFF
            buf[i + 2] = (x * 101 + y * 7 + (x * x + y * y)) & 0xFF
            i += 3
    with open(path, "wb") as f:
        f.write(f"P6\n{width} {height}\n255\n".encode())
        f.write(buf)
    return path


def make_lossy_rpcl_jp2(tmp_path, size):
    """Compress a textured image with the bug-triggering parameter set."""
    ppm = str(tmp_path / "input.ppm")
    make_ppm(ppm, size, size)
    jp2 = str(tmp_path / "lossy_rpcl.jp2")
    args = [
        "grk_compress",
        "-i",
        ppm,
        "-o",
        jp2,
        "-I",  # irreversible (lossy)
        "-p",
        "RPCL",  # resolution-position-component-layer
        "-r",
        "100,80,60,40,30,20,12,8,4,2",  # 10 explicit-rate quality layers
        "-M",
        "1",  # bypass (lazy) coding mode -> small segments
        "-n",
        "6",  # 6 resolutions
        "-c",
        "[128,128]",  # small precincts -> window skips whole precincts
        "-b",
        "64,64",  # 64x64 code blocks (4 per precinct)
        "-t",
        f"{size},{size}",  # single tile == whole image
    ]
    rc = grok_codec.grk_codec_compress(args, None, None)
    assert rc == 0, f"grk_codec_compress failed with rc={rc}"
    return jp2


def _component_array(comp):
    n = comp.h * comp.stride
    if comp.data_type == grok_core.GRK_INT_16:
        ptr = ctypes.cast(int(comp.data), ctypes.POINTER(ctypes.c_int16 * n))
    else:
        ptr = ctypes.cast(int(comp.data), ctypes.POINTER(ctypes.c_int32 * n))
    return ptr.contents


def _decompress_full(path):
    stream = grok_core.grk_stream_params()
    stream.file = path
    params = grok_core.grk_decompress_parameters()
    codec = grok_core.grk_decompress_init(stream, params)
    assert codec is not None
    header = grok_core.grk_header_info()
    assert grok_core.grk_decompress_read_header(codec, header)
    image = grok_core.grk_decompress_get_image(codec)
    assert image is not None
    assert grok_core.grk_decompress(codec, None)
    return image, codec


def _decompress_region(path, x0, y0, x1, y1):
    stream = grok_core.grk_stream_params()
    stream.file = path
    params = grok_core.grk_decompress_parameters()
    params.dw_x0 = float(x0)
    params.dw_y0 = float(y0)
    params.dw_x1 = float(x1)
    params.dw_y1 = float(y1)
    codec = grok_core.grk_decompress_init(stream, params)
    assert codec is not None
    header = grok_core.grk_header_info()
    assert grok_core.grk_decompress_read_header(codec, header)
    image = grok_core.grk_decompress_get_image(codec)
    assert image is not None
    assert grok_core.grk_decompress(codec, None)
    return image, codec


# Irreversible (9/7) reconstruction over a windowed region is not guaranteed to
# be bit-identical to the full decode: a few pixels near the window's wavelet
# support may differ by ~1 LSB. That is expected lossy behaviour. The bug being
# guarded against corrupts a large contiguous block by tens to hundreds of
# levels, so anything beyond a small per-pixel tolerance is treated as a
# failure.
GROSS_DELTA = 4


def _region_diff_stats(full_image, region_image, x0, y0):
    """Compare a region against the corresponding full-image crop.

    Returns (max_delta, num_gross, worst) where num_gross counts pixels whose
    absolute difference exceeds GROSS_DELTA and worst is
    (comp, x, y, full_val, region_val) for the largest difference (or None)."""
    max_delta = 0
    num_gross = 0
    worst = None
    for c in range(region_image.numcomps):
        full_comp = full_image.comps[c]
        reg_comp = region_image.comps[c]
        full_arr = _component_array(full_comp)
        reg_arr = _component_array(reg_comp)
        for ry in range(reg_comp.h):
            frow = (y0 + ry) * full_comp.stride
            rrow = ry * reg_comp.stride
            for rx in range(reg_comp.w):
                fv = full_arr[frow + x0 + rx]
                rv = reg_arr[rrow + rx]
                d = abs(rv - fv)
                if d > GROSS_DELTA:
                    num_gross += 1
                if d > max_delta:
                    max_delta = d
                    worst = (c, x0 + rx, y0 + ry, fv, rv)
    return max_delta, num_gross, worst


SIZE = 512

# Region origin/size variants. Each window leaves whole high-resolution
# precincts unparsed (the condition that triggered the bug).
REGIONS = [
    (0, 0, 128, 128),
    (0, 0, 200, 200),
    (64, 64, 320, 320),
]


@pytest.fixture(scope="module")
def lossy_rpcl_jp2(tmp_path_factory):
    tmp_path = tmp_path_factory.mktemp("region_decode")
    return make_lossy_rpcl_jp2(tmp_path, SIZE)


@pytest.fixture(scope="module")
def full_image(lossy_rpcl_jp2):
    grok_core.grk_initialize(None, 0, None)
    image, codec = _decompress_full(lossy_rpcl_jp2)
    yield image
    grok_core.grk_object_unref(codec)


@pytest.mark.parametrize(
    "x0,y0,x1,y1", REGIONS, ids=[f"{r[0]}_{r[1]}_{r[2]}x{r[3]}" for r in REGIONS]
)
def test_region_matches_full_decode(lossy_rpcl_jp2, full_image, x0, y0, x1, y1):
    """A windowed decode equals the same crop of the full decode."""
    region_image, region_codec = _decompress_region(lossy_rpcl_jp2, x0, y0, x1, y1)
    try:
        assert region_image.comps[0].w == x1 - x0
        assert region_image.comps[0].h == y1 - y0
        max_delta, num_gross, worst = _region_diff_stats(
            full_image, region_image, x0, y0
        )
        assert num_gross == 0, (
            f"region ({x0},{y0})-({x1},{y1}) differs grossly from full decode: "
            f"{num_gross} pixels exceed delta {GROSS_DELTA} (max_delta={max_delta}); "
            f"worst at component={worst[0]} ({worst[1]},{worst[2]}): "
            f"full={worst[3]} region={worst[4]}"
        )
    finally:
        grok_core.grk_object_unref(region_codec)
