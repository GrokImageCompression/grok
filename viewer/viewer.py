# Copyright (C) 2016-2025 Grok Image Compression Inc.

# This source code is free software: you can redistribute it and/or  modify
# it under the terms of the GNU Affero General Public License, version 3,
# as published by the Free Software Foundation.

# This source code is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.

# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import numpy as np
import argparse
import ctypes
from vispy import scene, app
from vispy.scene.visuals import Text
from grok_core import (
    GRK_TILE_CACHE_ALL,
    grk_initialize,
    grk_deinitialize,
    grk_decompress_parameters,
    grk_decompress_set_default_params,
    grk_decompress_init,
    grk_decompress_read_header,
    grk_decompress_set_window,
    grk_decompress_tile,
    grk_object_unref,
    grk_stream_params,
    grk_header_info,
    grk_decompress_get_image,
)

class TileViewerContext:
    def __init__(self, input_file):
        self.vispy_image = None
        self.zoom_level = 1.0
        self.pan_x, self.pan_y = 0, 0
        self.input_file = input_file

        grk_initialize(None, 0, True)
        self.codec = None
        self.header_info = grk_header_info()
        self.image = None
        self.tx, self.ty = 0, 0  # Tile grid coordinates

        self.stream_params = grk_stream_params()
        self.stream_params.file = input_file

        self.decompress_params = grk_decompress_parameters()
        grk_decompress_set_default_params(self.decompress_params)
        self.decompress_params.verbose_ = True
        #self.layers_to_decompress = 1
        #self.decompress_params.core.layers_to_decompress_ = self.layers_to_decompress
        self.codec = grk_decompress_init(self.stream_params, self.decompress_params)

        if not grk_decompress_read_header(self.codec, self.header_info):
            print("Failed to read header.")
        self.t_grid_width, self.t_grid_height = (
            self.header_info.t_grid_width,
            self.header_info.t_grid_height,
        )
        self.tx = self.t_grid_width // 2
        self.ty = self.t_grid_height // 2
        self.max_resolutions = self.header_info.numresolutions;
        self.max_layers = self.header_info.num_layers_
        self.layers_to_decompress = self.max_layers
        self.reduce = 0
        self.resolutions_to_decompress = self.max_resolutions - self.reduce
        self.decompress_params.core.reduce = self.reduce
        grk_object_unref(self.codec)
        self.codec = None

    def cleanup(self):
        """Explicit cleanup to release codec and other resources."""
        if self.codec is not None:
            grk_object_unref(self.codec)
            self.codec = None
        grk_deinitialize()

    def __del__(self):
        self.cleanup()

    def decompress_tile(self, input_file, tile_index=0):
        self.codec = grk_decompress_init(self.stream_params, self.decompress_params)
        if self.codec is None:
            print("Failed to update codec.")
            return None

        if not grk_decompress_read_header(self.codec, self.header_info):
            print("Failed to read header.")
        
        if not grk_decompress_tile(self.codec, tile_index):
            print("Tile decompression failed.")
            return None

        # Retrieve the decompressed image with the components
        self.image = grk_decompress_get_image(self.codec)
        if self.image is None:
            print("Failed to retrieve image.")
            return None

        # Access the components from the grk_image structure
        num_comps = self.image.numcomps
        width, height = self.image.comps[0].w, self.image.comps[0].h

        # Prepare the NumPy array for the data
        data = np.zeros((height, width, num_comps), dtype=np.uint16)

        for c in range(num_comps):
            comp = self.image.comps[c]
            expected_length = comp.h * comp.stride

            # Access data, cast it as int32, then view as uint16
            data_ptr = ctypes.cast(
                int(comp.data), ctypes.POINTER(ctypes.c_int32 * expected_length)
            )
            comp_data = np.ctypeslib.as_array(data_ptr.contents)

            # Convert int32 to uint16 by taking only the lower 16 bits
            comp_data = (comp_data & 0xFFFF).astype(np.uint16)
            comp_data = comp_data.reshape((comp.h, comp.stride))[:, :width]

            data[:, :, c] = comp_data

        grk_object_unref(self.codec)
        self.codec = None    

        return data

    def display_image(self, canvas, image_data, view):
        """Display 16-bit image data using VisPy on an existing canvas and view."""
        # Normalize the data to [0, 1] for VisPy rendering
        image_data_normalized = np.flipud(image_data.astype(np.float32) / 65535.0)

        # Remove the existing image if there is one
        if self.vispy_image:
            self.vispy_image.parent = None

        # Display the 16-bit image with ImageVisual
        self.vispy_image = scene.visuals.Image(
            image_data_normalized, parent=view.scene, cmap="gray"
        )

    def _format_osd(self):
         self.osd_text.text = f"Resolutions: {self.resolutions_to_decompress:02}/{self.max_resolutions:02}  Layers: {self.layers_to_decompress:02}/{self.max_layers:02}"


    def setup_osd(self, canvas):
        """Setup the OSD text in the bottom left corner."""
        self.osd_text = Text(
            color='white',
            font_size=12,
            anchor_x='left',
            anchor_y='bottom',
            parent=canvas.scene
        )
        # Position text in the bottom left
        self.osd_text.pos = (10, canvas.size[1] - 20)
        self.set_layers(self.layers_to_decompress)

    def set_layers(self, layers):
        self.layers_to_decompress = 1 if layers > self.max_layers else layers
        self.decompress_params.core.layers_to_decompress_ = self.layers_to_decompress
        self._format_osd()
 
    def update_view(self, canvas, view):
        tile_index = self.tx + self.ty * self.t_grid_width
        tile_data = self.decompress_tile(self.input_file, tile_index)
        if tile_data is None:
            print(f"Failed to decompress tile {tile_index}.")
            return

        img_height, img_width = tile_data.shape[:2]
        view.camera.set_range(x=(0, img_width), y=(0, img_height))
        self.display_image(canvas, tile_data, view)

    def key_press(self, canvas, view, event):
        if event.key == "Right" and self.tx < self.t_grid_width - 1:
            self.tx += 1
            self.update_view(canvas, view)
        elif event.key == "Left" and self.tx > 0:
            self.tx -= 1
            self.update_view(canvas, view)
        elif event.key == "Up" and self.ty > 0:
            self.ty -= 1
            self.update_view(canvas, view)
        elif event.key == "Down" and self.ty < self.t_grid_height - 1:
            self.ty += 1
            self.update_view(canvas, view)
        elif event.key == "L":
            self.set_layers(self.layers_to_decompress + 1)
            self.update_view(canvas, view)

def main():
    parser = argparse.ArgumentParser(
        description="16-bit Tile Viewer with Decompression"
    )
    parser.add_argument("-i", "--input", required=True, help="Input file path")
    args = parser.parse_args()

    # Create a VisPy canvas
    canvas = scene.SceneCanvas(
        keys="interactive", size=(1600, 1200), title="Tile Viewer"
    )
    # Set up the image view
    view = canvas.central_widget.add_view()
    view.camera = scene.PanZoomCamera(aspect=1)

    context = TileViewerContext(args.input)
    context.setup_osd(canvas)

    # Create OSD text for displaying filename
    osd_text = Text(
        text=f"File: {args.input}",
        color="white",
        font_size=12,
        pos=(10, 20),  # Position in the upper left corner
        parent=canvas.scene,
        anchor_x="left",
        anchor_y="top",
    )

    # Handle arrow keys to navigate through the tile grid
    def on_key_press(event):
        context.key_press(canvas, view, event)

    # Connect keyboard events for navigating tiles
    canvas.events.key_press.connect(on_key_press)

    # Initial display of the middle tile
    context.update_view(canvas, view)
    canvas.show()

    # Run the VisPy app
    app.run()

    # Explicitly clean up resources after the app closes
    context.cleanup()
    canvas.close()
    app.quit()

if __name__ == "__main__":
    main()
