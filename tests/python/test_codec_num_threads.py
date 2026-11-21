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

"""Per-codec thread pool tests.

grk_decompress_parameters.num_threads and grk_cparameters.num_threads give a
single codec its own thread pool, independent of the global pool created by
grk_initialize(): 0 uses the global pool, 1 an inline executor on the calling
thread, N > 1 a private N-worker pool.  Per-worker scratch (wavelet buffers,
coder pools, tile-part state) is sized and indexed by the codec's own thread
count, so a mismatch shows up as wrong pixels or a failed decode.  These tests
verify that every thread count yields pixel-identical output, that a private
pool is deterministic across repeats, and that compression on a private pool
round-trips.
"""

import pytest

import grok_core

from test_single_threaded import _compress, _extract

# (name, width, height, num_comps, prec, color_space, irreversible, tile)
# the 12-bit irreversible case decodes through the int32 9/7 wavelet, the
# configuration of the DCI frames in issue #420; odd dimensions exercise the
# sub-vector remainder of the DWT strips
_CASES = [
    ("rgb8_multitile_rev", 96, 80, 3, 8, grok_core.GRK_CLRSPC_SRGB, False, 32),
    ("rgb12_singletile_irrev", 99, 67, 3, 12, grok_core.GRK_CLRSPC_SRGB, True, 0),
]

_CODEC_THREAD_COUNTS = [1, 2, 3, 4]


@pytest.fixture(autouse=True)
def _default_global_pool():
    """Run every test against the default global pool so the per-codec value
    is the only thread-count variable."""
    grok_core.grk_initialize(None, 0, None)
    yield


def _decompress(path, codec_num_threads):
    """Decode with a per-codec thread count, leaving the global pool alone."""
    params = grok_core.grk_decompress_parameters()
    params.num_threads = codec_num_threads
    stream = grok_core.grk_stream_params()
    stream.file = path
    codec = grok_core.grk_decompress_init(stream, params)
    assert codec is not None
    header = grok_core.grk_header_info()
    assert grok_core.grk_decompress_read_header(codec, header)
    assert grok_core.grk_decompress(codec, None)
    image = grok_core.grk_decompress_get_image(codec)
    assert image is not None
    pixels = _extract(image)
    grok_core.grk_object_unref(codec)
    return pixels


@pytest.mark.parametrize("case", _CASES, ids=[c[0] for c in _CASES])
def test_codec_pool_matches_global_pool(tmp_path, case):
    name, w, h, nc, prec, cs, irr, tile = case
    jp2 = str(tmp_path / f"{name}.jp2")
    assert _compress(jp2, w, h, nc, prec, cs, irreversible=irr, tile=tile)
    reference = _decompress(jp2, 0)
    for num_threads in _CODEC_THREAD_COUNTS:
        pixels = _decompress(jp2, num_threads)
        assert pixels == reference, f"{name}: num_threads={num_threads} differs"


@pytest.mark.parametrize("case", _CASES, ids=[c[0] for c in _CASES])
def test_codec_pool_is_deterministic(tmp_path, case):
    name, w, h, nc, prec, cs, irr, tile = case
    jp2 = str(tmp_path / f"{name}.jp2")
    assert _compress(jp2, w, h, nc, prec, cs, irreversible=irr, tile=tile)
    runs = [_decompress(jp2, 3) for _ in range(3)]
    assert runs[0] == runs[1] == runs[2], f"{name}: private pool not deterministic"


@pytest.mark.parametrize("num_threads", [1, 2, 4])
def test_compress_codec_pool_roundtrip(tmp_path, num_threads):
    """A reversible compress on a private pool must decode to the same pixels
    as one on the global pool."""
    jp2 = str(tmp_path / f"compress_nt{num_threads}.jp2")
    ref_jp2 = str(tmp_path / "compress_ref.jp2")
    args = (96, 80, 1, 8, grok_core.GRK_CLRSPC_GRAY)
    assert _compress(jp2, *args, tile=32, num_threads=num_threads)
    assert _compress(ref_jp2, *args, tile=32)
    assert _decompress(jp2, 0) == _decompress(ref_jp2, 0)
