# Copyright (C) 2016-2025 Grok Image Compression Inc.
#
# This source code is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License, version 3,
# as published by the Free Software Foundation.
#
# This source code is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
import os
import argparse
import numpy as np
from PIL import Image, ImageChops  # Assumes Pillow is installed
from grok_core import (
    grk_initialize,
    grk_decompress_parameters,
    grk_image_comp,
    grk_object,
    grk_decompress_init,
    grk_decompress_update,
    grk_decompress_read_header,
    grk_decompress_get_image,
    grk_decompress_tile,
    grk_decompress,
    grk_object_unref,
    grk_stream_params,
    grk_header_info,
)
import ctypes

# Constants
data_root = "."
file_type = ".tif"


def write_image(image, filename):
    num_comps = image.numcomps
    width, height = image.comps[0].w, image.comps[0].h

    # Initialize a final numpy array to hold the image data in 16-bit format
    data = np.zeros((height, width, num_comps), dtype=np.uint16)

    for c in range(num_comps):
        comp = image.comps[c]
        expected_length = comp.h * comp.stride

        # Access data, cast it as int32, then view as uint16
        data_ptr = ctypes.cast(
            int(comp.data), ctypes.POINTER(ctypes.c_int32 * expected_length)
        )
        comp_data = np.ctypeslib.as_array(data_ptr.contents)

        # Convert int32 to uint16 by taking only the lower 16 bits
        comp_data = (comp_data & 0xFFFF).astype(np.uint16)  # Mask to keep only 16 bits
        comp_data = comp_data.reshape((comp.h, comp.stride))[
            :, :width
        ]  # Apply stride and slice width

        data[:, :, c] = comp_data

    # Convert the data array to a 16-bit PIL image
    if num_comps == 1:
        img = Image.fromarray(data[:, :, 0], mode="I;16")  # Grayscale 16-bit
    else:
        img = Image.fromarray(data, mode="RGB;16")  # RGB 16-bit

    img.save(filename)
    print(f"Image saved as {filename}")
    return True


# Helper function to compare images using PSNR
def compare_images(image1_path, image2_path):
    img1 = Image.open(image1_path)
    img2 = Image.open(image2_path)

    # Compute Mean Squared Error (MSE)
    diff = ImageChops.difference(img1, img2)
    mse = np.mean(np.square(np.array(diff)))
    # Calculate PSNR
    psnr = 10 * np.log10(255**2 / mse) if mse != 0 else float("inf")
    print(f"PSNR between {image1_path} and {image2_path}: {psnr} dB")
    return psnr


# Core decompression logic
def decompress_image(
    input_file,
    output_file,
    decompress_tile=False,
    tile_index=0,
    decompress_window=False,
):
    grk_initialize(None, 0, None)

    decompress_params = grk_decompress_parameters()

    stream_params = grk_stream_params()
    stream_params.file = input_file

    codec = grk_decompress_init(stream_params, decompress_params)
    if codec is None:
        print("Failed to initialize codec.")
        return False

    grk_decompress_update(decompress_params, codec)

    header_info = grk_header_info()
    if not grk_decompress_read_header(codec, header_info):
        print("Failed to read header.")
        return False

    image = grk_decompress_get_image(codec)
    if image is None:
        print("Failed to retrieve image.")
        return False

    if decompress_tile:
        if not grk_decompress_tile(codec, tile_index):
            print("Tile decompression failed.")
            return False
    else:
        if not grk_decompress(codec, None):
            print("Full decompression failed.")
            return False

    if not write_image(image, output_file):
        print("Failed to write image.")
        return False

    grk_object_unref(codec)
    return True


# Progressive decompression with comparison
def progressive_decompression(input_file, tile_index=0):
    max_layers = 0xFFFF
    max_resolutions = 0xFF

    decompress_params = grk_decompress_parameters()
    decompress_params.verbose_ = True
    stream_params = grk_stream_params()
    stream_params.file = input_file
    codec_diff = None
    header_info = grk_header_info()

    decompress_params.core.tile_cache_strategy = (
        grk_decompress_parameters.GRK_TILE_CACHE_NONE
    )
    if not decompress_image(
        input_file,
        "reference_full" + file_type,
        decompress_tile=False,
        tile_index=tile_index,
    ):
        print("Failed full reference decompression.")
        return False

    for layer in range(1, max_layers + 1, 5):
        decompress_params.core.layers_to_decompress_ = layer
        output_filename = f"progressive_layer_{layer}+file_type"
        if not decompress_image(
            input_file, output_filename, decompress_tile=True, tile_index=tile_index
        ):
            print(f"Failed progressive decompression for layer {layer}")
            return False
        compare_images("reference_full" + file_type, output_filename)

    for res in range(1, max_resolutions + 1):
        decompress_params.core.reduce = header_info.numresolutions - res
        output_filename = f"progressive_resolution_{res} + file_type"
        if not decompress_image(
            input_file, output_filename, decompress_tile=True, tile_index=tile_index
        ):
            print(f"Failed progressive decompression for resolution {res}")
            return False
        compare_images("reference_full" + file_type, output_filename)

    return True


# Main CLI handling
def main():
    parser = argparse.ArgumentParser(description="Core Decompressor")
    parser.add_argument(
        "-i", "--input", default="boats_cprl.j2k", help="Input file path"
    )
    parser.add_argument(
        "-o", "--output", default="output_image" + file_type, help="Output file path"
    )
    parser.add_argument(
        "-t", "--tile", type=int, default=0, help="Tile index to decompress"
    )
    parser.add_argument(
        "--progressive", action="store_true", help="Enable progressive decompression"
    )
    args = parser.parse_args()

    input_file = os.path.join(data_root, args.input)
    output_file = os.path.join(data_root, args.output)

    if args.progressive:
        progressive_decompression(input_file, args.tile)
    else:
        success = decompress_image(
            input_file, output_file, decompress_tile=True, tile_index=args.tile
        )
        if success:
            print("Decompression successful.")
        else:
            print("Decompression failed.")


if __name__ == "__main__":
    main()
