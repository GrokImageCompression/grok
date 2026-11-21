#!/usr/bin/env python3
"""Generate a folder of 12-bit RGB TIFFs with gradually shifted colours.

Takes one source TIFF and produces N copies where each frame has its
colour channels shifted by an increasing amount, making every output
image visually distinct.

Usage:
    python3 scripts/shift_colours.py [--input FILE] [--outdir DIR] [--count N]

Defaults:
    --input   ~/temp/rgb_12/sample_12 (Copy 10).tiff
    --outdir  ~/temp/rgb_12
    --count   33
"""

import argparse
import os
from pathlib import Path

import numpy as np
import tifffile


def shift_image(img: np.ndarray, index: int, total: int, max_val: int) -> np.ndarray:
    """Return a copy of *img* with colour channels shifted for frame *index*.

    Strategy (all operations stay in [0, max_val]):
      - Red:   circular shift up   by  index/total * max_val * 0.4
      - Green: circular shift down by  index/total * max_val * 0.25
      - Blue:  circular shift up   by  index/total * max_val * 0.15

    The multipliers are chosen so the shift is clearly visible but
    doesn't wrap around too aggressively.
    """
    out = img.copy()
    frac = index / max(total - 1, 1)

    r_shift = int(frac * max_val * 0.40)
    g_shift = int(frac * max_val * 0.25)
    b_shift = int(frac * max_val * 0.15)

    out[:, :, 0] = np.clip(out[:, :, 0].astype(np.int32) + r_shift, 0, max_val).astype(
        img.dtype
    )
    out[:, :, 1] = np.clip(out[:, :, 1].astype(np.int32) - g_shift, 0, max_val).astype(
        img.dtype
    )
    out[:, :, 2] = np.clip(out[:, :, 2].astype(np.int32) + b_shift, 0, max_val).astype(
        img.dtype
    )
    return out


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    default_input = os.path.expanduser("~/temp/rgb_12/sample_12 (Copy 10).tiff")
    parser.add_argument(
        "--input", "-i", default=default_input, help="Source 12-bit RGB TIFF"
    )
    parser.add_argument(
        "--outdir",
        "-o",
        default=os.path.expanduser("~/temp/rgb_12"),
        help="Output directory",
    )
    parser.add_argument(
        "--count", "-n", type=int, default=33, help="Number of output frames"
    )
    args = parser.parse_args()

    src = Path(args.input)
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    print(f"Reading {src}")
    img = tifffile.imread(str(src))
    print(f"  shape={img.shape}  dtype={img.dtype}  range=[{img.min()}, {img.max()}]")

    # Detect bit depth from TIFF tag or fall back to data range
    with tifffile.TiffFile(str(src)) as tif:
        bits = tif.pages[0].bitspersample
    max_val = (1 << bits) - 1
    print(f"  bits_per_sample={bits}  max_val={max_val}")

    for i in range(args.count):
        shifted = shift_image(img, i, args.count, max_val)
        name = f"frame_{i:04d}.tiff"
        out_path = outdir / name
        # Write as uncompressed TIFF preserving original bit depth in uint16
        tifffile.imwrite(
            str(out_path),
            shifted,
            photometric="rgb",
            bitspersample=bits,
            compression=None,
        )
        r_mean = shifted[:, :, 0].mean()
        g_mean = shifted[:, :, 1].mean()
        b_mean = shifted[:, :, 2].mean()
        print(
            f"  [{i:3d}/{args.count}] {name}  R={r_mean:.0f} G={g_mean:.0f} B={b_mean:.0f}"
        )

    print(f"\nWrote {args.count} frames to {outdir}")


if __name__ == "__main__":
    main()
