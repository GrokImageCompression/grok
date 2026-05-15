#!/usr/bin/env python3
"""Verify region decompression against full-image decompression.

Decompresses the entire image into memory, then repeatedly decompresses random
regions (with both dimensions <= max_dimension) and compares the region pixels
against the corresponding pixels from the full image.

Usage (from the grok project root):

    PYTHONPATH=build/bin LD_LIBRARY_PATH=build/bin:$LD_LIBRARY_PATH \\
        python tests/python/region_verify.py <image> <max_dim> [options]

Examples:

    # Run until Ctrl+C, regions up to 256x256
    PYTHONPATH=build/bin LD_LIBRARY_PATH=build/bin:$LD_LIBRARY_PATH \\
        python tests/python/region_verify.py /path/to/image.jp2 256

    # Run 50 iterations with a fixed seed for reproducibility
    PYTHONPATH=build/bin LD_LIBRARY_PATH=build/bin:$LD_LIBRARY_PATH \\
        python tests/python/region_verify.py /path/to/image.jp2 128 -n 50 --seed 42

    # Max verbosity from grok library
    GRK_DEBUG=5 PYTHONPATH=build/bin LD_LIBRARY_PATH=build/bin:$LD_LIBRARY_PATH \\
        python tests/python/region_verify.py /path/to/image.jp2 64

Environment variables:
    GRK_DEBUG   Grok log verbosity (1=errors, 2=+warnings, 3=+info, 5=max).
                Defaults to 2 if not set.

Arguments:
    image       Path to a JPEG 2000 file (.jp2, .j2k, .j2c)
    max_dim     Maximum width/height for random decode regions

Options:
    -n N        Number of iterations (default: run until interrupted)
    --seed N    Random seed for reproducibility
"""

import argparse
import ctypes
import os
import random
import sys

import grok_core

# Enable grok warning/info logging by default (level 3 = error + warning + info)
if "GRK_DEBUG" not in os.environ:
    os.environ["GRK_DEBUG"] = "2"


def decompress_full(path):
    """Decompress the full image synchronously. Returns (image, codec)."""
    stream = grok_core.grk_stream_params()
    stream.file = path

    params = grok_core.grk_decompress_parameters()
    codec = grok_core.grk_decompress_init(stream, params)
    if codec is None:
        sys.exit(f"ERROR: failed to initialize decompressor for {path}")

    header = grok_core.grk_header_info()
    if not grok_core.grk_decompress_read_header(codec, header):
        grok_core.grk_object_unref(codec)
        sys.exit("ERROR: failed to read header")

    image = grok_core.grk_decompress_get_image(codec)
    if image is None:
        grok_core.grk_object_unref(codec)
        sys.exit("ERROR: failed to get image from codec")

    if not grok_core.grk_decompress(codec, None):
        grok_core.grk_object_unref(codec)
        sys.exit("ERROR: decompression failed")

    return image, codec


def decompress_region(path, x0, y0, x1, y1):
    """Decompress a window region. Returns (image, codec)."""
    stream = grok_core.grk_stream_params()
    stream.file = path

    params = grok_core.grk_decompress_parameters()
    params.dw_x0 = float(x0)
    params.dw_y0 = float(y0)
    params.dw_x1 = float(x1)
    params.dw_y1 = float(y1)

    codec = grok_core.grk_decompress_init(stream, params)
    if codec is None:
        return None, None

    header = grok_core.grk_header_info()
    if not grok_core.grk_decompress_read_header(codec, header):
        grok_core.grk_object_unref(codec)
        return None, None

    image = grok_core.grk_decompress_get_image(codec)
    if image is None:
        grok_core.grk_object_unref(codec)
        return None, None

    if not grok_core.grk_decompress(codec, None):
        grok_core.grk_object_unref(codec)
        return None, None

    return image, codec


def get_component_array(comp):
    """Return a ctypes array for a component's pixel data."""
    n_elements = comp.h * comp.stride
    if comp.data_type == grok_core.GRK_INT_16:
        ptr = ctypes.cast(
            int(comp.data), ctypes.POINTER(ctypes.c_int16 * n_elements)
        )
    else:
        ptr = ctypes.cast(
            int(comp.data), ctypes.POINTER(ctypes.c_int32 * n_elements)
        )
    return ptr.contents


def compare_region(full_image, region_image, x0, y0):
    """Compare region_image pixels against the corresponding area in full_image.

    Returns (match, first_diff, stats) where first_diff is None if match is True,
    or (component, x, y, expected, actual) on first mismatch.
    stats is a dict with pixel value statistics for the region.
    """
    min_val = None
    max_val = None
    n_nonzero = 0
    n_total = 0

    for c in range(region_image.numcomps):
        full_comp = full_image.comps[c]
        reg_comp = region_image.comps[c]

        full_arr = get_component_array(full_comp)
        reg_arr = get_component_array(reg_comp)

        for ry in range(reg_comp.h):
            for rx in range(reg_comp.w):
                reg_val = reg_arr[ry * reg_comp.stride + rx]
                n_total += 1
                if reg_val != 0:
                    n_nonzero += 1
                if min_val is None or reg_val < min_val:
                    min_val = reg_val
                if max_val is None or reg_val > max_val:
                    max_val = reg_val
                # Map region coords back to full image coords
                fx = x0 + rx
                fy = y0 + ry
                full_val = full_arr[fy * full_comp.stride + fx]
                if reg_val != full_val:
                    stats = {"min": min_val, "max": max_val,
                             "nonzero": n_nonzero, "total": n_total}
                    return False, (c, fx, fy, full_val, reg_val), stats

    stats = {"min": min_val, "max": max_val,
             "nonzero": n_nonzero, "total": n_total}
    return True, None, stats


def display_first_diff(full_image, region_image, x0, y0, diff_info):
    """Print info about the first differing pixel."""
    comp_idx, fx, fy, expected, actual = diff_info
    print(f"  MISMATCH at component={comp_idx}, "
          f"full_image({fx},{fy}): expected={expected}, got={actual}")
    print(f"  Region origin: ({x0}, {y0}), "
          f"region size: {region_image.comps[0].w}x{region_image.comps[0].h}")


def main():
    parser = argparse.ArgumentParser(
        description="Verify region decompression against full-image decode."
    )
    parser.add_argument("image", help="Path to JPEG 2000 image")
    parser.add_argument("max_dim", type=int, help="Max dimension for random regions")
    parser.add_argument(
        "-n", "--iterations", type=int, default=0,
        help="Number of random regions to test (default: 0 = run until interrupted)"
    )
    parser.add_argument(
        "--seed", type=int, default=None,
        help="Random seed for reproducibility"
    )
    args = parser.parse_args()

    if args.seed is not None:
        random.seed(args.seed)

    grok_core.grk_initialize(None, 0, None)

    print(f"Decompressing full image: {args.image}")
    full_image, full_codec = decompress_full(args.image)

    img_w = full_image.comps[0].w
    img_h = full_image.comps[0].h
    num_comps = full_image.numcomps
    print(f"Image: {img_w}x{img_h}, {num_comps} components")

    # Sanity check: verify full image has non-trivial data
    for c in range(num_comps):
        comp = full_image.comps[c]
        arr = get_component_array(comp)
        sample_vals = [arr[y * comp.stride + x]
                       for y in range(0, comp.h, max(1, comp.h // 8))
                       for x in range(0, comp.w, max(1, comp.w // 8))]
        n_nonzero = sum(1 for v in sample_vals if v != 0)
        mn = min(sample_vals)
        mx = max(sample_vals)
        print(f"  Component {c}: sample min={mn}, max={mx}, "
              f"nonzero={n_nonzero}/{len(sample_vals)}")
        if mn == mx == 0:
            print(f"  WARNING: Component {c} appears to be all zeros!")

    if args.max_dim > img_w or args.max_dim > img_h:
        print(f"WARNING: max_dim ({args.max_dim}) exceeds image dimensions, "
              f"clamping to image size")

    passed = 0
    failed = 0
    i = 0
    limit = args.iterations if args.iterations > 0 else None
    label = str(limit) if limit else "∞"

    try:
        while limit is None or i < limit:
            i += 1
            # Generate random region dimensions
            rw = random.randint(1, min(args.max_dim, img_w))
            rh = random.randint(1, min(args.max_dim, img_h))

            # Random origin ensuring region stays within image
            x0 = random.randint(0, img_w - rw)
            y0 = random.randint(0, img_h - rh)
            x1 = x0 + rw
            y1 = y0 + rh

            region_image, region_codec = decompress_region(args.image, x0, y0, x1, y1)
            if region_image is None:
                print(f"[{i}/{label}] SKIP region ({x0},{y0})-({x1},{y1}) "
                      f"- decompress failed")
                continue

            match, diff_info, stats = compare_region(full_image, region_image, x0, y0)

            if match:
                passed += 1
                all_zero = stats["nonzero"] == 0
                zero_warn = " [ALL ZEROS]" if all_zero else ""
                print(f"[{i}/{label}] OK  region ({x0},{y0})-({x1},{y1}) "
                      f"size {rw}x{rh}  "
                      f"[min={stats['min']} max={stats['max']} "
                      f"nonzero={stats['nonzero']}/{stats['total']}]{zero_warn}")
            else:
                failed += 1
                print(f"[{i}/{label}] FAIL region ({x0},{y0})-({x1},{y1}) "
                      f"size {rw}x{rh}")
                display_first_diff(full_image, region_image, x0, y0, diff_info)

            grok_core.grk_object_unref(region_codec)
    except KeyboardInterrupt:
        print("\n\nInterrupted by user.")

    grok_core.grk_object_unref(full_codec)

    total = passed + failed
    print(f"\nResults: {passed} passed, {failed} failed out of {total}")
    sys.exit(1 if failed > 0 else 0)


if __name__ == "__main__":
    main()
