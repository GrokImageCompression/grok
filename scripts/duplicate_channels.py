# Copyright (C) 2016-2026 Grok Image Compression Inc.
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
import cv2
import numpy as np
import argparse
import tifffile as tiff


def apply_filters_16bit(input_image):
    # Apply Scharr filter for edge detection in 16-bit images
    image_16s = input_image.astype(np.int16)
    scharrx = cv2.Scharr(image_16s, cv2.CV_16S, 1, 0)  # Gradient in x-direction
    scharry = cv2.Scharr(image_16s, cv2.CV_16S, 0, 1)  # Gradient in y-direction

    # Combine the x and y gradients to get the magnitude of edges
    scharr_magnitude = cv2.magnitude(
        scharrx.astype(np.float32), scharry.astype(np.float32)
    )
    scharr_magnitude = cv2.normalize(
        scharr_magnitude, None, 0, 65535, cv2.NORM_MINMAX
    ).astype(np.uint16)

    # Apply Gaussian Blur directly to 16-bit image
    blurred = cv2.GaussianBlur(input_image, (5, 5), 0)

    return scharr_magnitude, blurred, scharr_magnitude, blurred, scharr_magnitude


def duplicate_channels_with_yuv_and_filters(input_file, output_file, num_channels):
    # Load the 16-bit grayscale TIFF image
    image = cv2.imread(input_file, cv2.IMREAD_UNCHANGED)
    if image is None or image.dtype != np.uint16:
        print(f"Error: Unable to open input file '{input_file}' as a 16-bit image.")
        return

    # Simulate YUV channels by scaling the grayscale image
    y = image  # Use the original image as the Y channel
    u = cv2.normalize(image, None, 0, 32767, cv2.NORM_MINMAX)  # Simulated U channel
    v = cv2.normalize(image, None, 0, 16383, cv2.NORM_MINMAX)  # Simulated V channel

    # Initialize the list of channels with YUV channels
    channels = [y, u, v]

    # Apply filters to create additional distinct channels
    filters = apply_filters_16bit(image)
    # Add filters to reach the desired number of channels
    while len(channels) < num_channels:
        for f in filters:
            if len(channels) < num_channels:
                channels.append(f)
            else:
                break

    # Stack only the requested number of channels
    multi_channel_image = np.stack(channels[:num_channels], axis=-1)

    # Define extrasamples for channels beyond the first three (YUV)
    if num_channels > 3:
        extrasamples = [0] * (num_channels - 1)  # Extra channels are general data
    else:
        extrasamples = None

    # Debugging print statements
    print(f"Number of channels: {num_channels}")
    print(f"Extrasamples: {extrasamples}")

    # Save as a multi-channel TIFF using tifffile
    tiff.imwrite(
        output_file,
        multi_channel_image,
        dtype=np.uint16,
        photometric="minisblack",
        planarconfig="contig",
        extrasamples=extrasamples,
    )
    print(f"Multi-channel image saved as '{output_file}' with {num_channels} channels.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Duplicate a 16-bit grayscale image to a specified number of channels with transformations."
    )
    parser.add_argument(
        "input_file", type=str, help="Path to the input 16-bit grayscale TIFF file"
    )
    parser.add_argument(
        "output_file", type=str, help="Path to save the output multi-channel TIFF file"
    )
    parser.add_argument(
        "num_channels", type=int, help="Number of channels for the output image"
    )

    args = parser.parse_args()
    duplicate_channels_with_yuv_and_filters(
        args.input_file, args.output_file, args.num_channels
    )
