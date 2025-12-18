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
import numpy as np
import time
import argparse
import ctypes
from vispy import scene, app
from vispy.scene.visuals import Text
from grok_core import (
    GRK_TILE_CACHE_ALL,
    grk_initialize,
    grk_decompress_parameters,
    grk_decompress_init,
    grk_decompress_read_header,
    grk_decompress_tile,
    grk_object_unref,
    grk_stream_params,
    grk_header_info,
    grk_decompress_get_tile_image,
    grk_decompress_get_image,
    grk_decompress_get_progression_state,
    grk_decompress_set_progression_state,
)


class TileCacheEntry:
    def __init__(self, data):
        self.data = data
        self.dirty = False


class TileCache:
    def __init__(self, max_tiles=100):
        self.cache = {}
        self.max_tiles = max_tiles
        self.lru_order = []  # For LRU tracking

    def get(self, tile_index):
        if tile_index in self.cache:
            # Move the tile to the front of the LRU list
            self.lru_order.remove(tile_index)
            self.lru_order.insert(0, tile_index)
            return self.cache[tile_index]
        return None

    def put(self, tile_index, data):
        # Evict the least recently used tile if cache is full
        if len(self.cache) >= self.max_tiles:
            oldest_tile = self.lru_order.pop()
            if oldest_tile in self.cache:
                del self.cache[oldest_tile]
            else:
                print(
                    f"Warning: Attempted to evict nonexistent cache entry: {oldest_tile}"
                )

        # Add the tile to the cache and LRU list
        entry = TileCacheEntry(data)
        self.cache[tile_index] = entry
        self.lru_order.insert(0, tile_index)
        return entry

    def set_dirty(self, tile_index, dirty=True):
        if tile_index in self.cache:
            self.cache[tile_index].dirty = dirty

    def is_dirty(self, tile_index):
        return self.cache.get(tile_index, TileCacheEntry(None)).dirty


class TileViewerContext:
    def __init__(self, input_file, layers):
        self.cache = TileCache(max_tiles=50)  # Set cache size as needed
        self.vispy_images = []  # To hold images for each viewport dynamically
        self.pan_x, self.pan_y = 0, 0
        self.initialized = False
        self.current_zoom = None
        self.current_center = None

        self.input_file = input_file
        self.default_layers = layers

        grk_initialize(None, 0, None)
        self.codec = None
        self.header_info = grk_header_info()
        self.tx, self.ty = 0, 0  # Tile grid coordinates

        self.stream_params = grk_stream_params()
        self.stream_params.file = input_file

        self.decompress_params = grk_decompress_parameters()
        self.decompress_params.verbose_ = True
        self.decompress_params.core.tile_cache_strategy = GRK_TILE_CACHE_ALL
        self.decompress_params.core.layers_to_decompress = self.default_layers
        self.codec = grk_decompress_init(self.stream_params, self.decompress_params)

        if not grk_decompress_read_header(self.codec, self.header_info):
            print("Failed to read header.")
        self.t_grid_width, self.t_grid_height = (
            self.header_info.t_grid_width,
            self.header_info.t_grid_height,
        )
        self.tx = self.t_grid_width // 2
        self.ty = self.t_grid_height // 2
        self.max_resolutions = self.header_info.numresolutions
        self.reduce = 0
        self.resolutions_to_decompress = self.max_resolutions - self.reduce
        self.decompress_params.core.reduce = self.reduce

        canvas = scene.SceneCanvas(
            keys="interactive", size=(1600, 1200), title="Tile Viewer"
        )
        self.setup_osd(canvas)
        self.num_components = self.header_info.header_image.numcomps

    def cleanup(self):
        """Explicit cleanup to release codec and other resources."""
        if self.codec is not None:
            grk_object_unref(self.codec)
            self.codec = None

    def __del__(self):
        self.cleanup()

    def update_decomression_time(self, elapsed):
        self.decompression_time_text.text = f"Decompression Time: {elapsed:.2f} ms"

    def decompress_tile(self, tile_index=0):
        """Decompress and cache the specified tile."""
        cache_entry = self.cache.get(tile_index)
        if cache_entry and not cache_entry.dirty:
            self.update_decomression_time(0)
            return cache_entry.data

        start_time = time.time()

        if not grk_decompress_tile(self.codec, tile_index):
            print("Tile decompression failed.")
            return None

        end_time = time.time()
        decompression_time = (end_time - start_time) * 1000
        self.update_decomression_time(decompression_time)

        prog = grk_decompress_get_progression_state(self.codec, tile_index)
        image = grk_decompress_get_tile_image(self.codec, tile_index)
        if image is None:
            print("Failed to retrieve image.")
            return None

        width, height = image.comps[0].w, image.comps[0].h
        data = np.zeros((height, width, self.num_components), dtype=np.uint16)

        for c in range(self.num_components):
            comp = image.comps[c]
            expected_length = comp.h * comp.stride
            data_ptr = ctypes.cast(
                int(comp.data), ctypes.POINTER(ctypes.c_int32 * expected_length)
            )
            comp_data = np.ctypeslib.as_array(data_ptr.contents)

            if comp.prec == 8:
                comp_data = (comp_data & 0xFF).astype(np.uint8)
            elif comp.prec == 16:
                comp_data = (comp_data & 0xFFFF).astype(np.uint16)
            else:
                max_value = (1 << comp.prec) - 1
                comp_data = ((comp_data & max_value) / max_value * 65535).astype(
                    np.uint16
                )

            comp_data = comp_data.reshape((comp.h, comp.stride))[:, :width]
            data[:, :, c] = comp_data

        cache_entry = self.cache.put(tile_index, data)
        cache_entry.dirty = False
        return data

    def setup_osd(self, canvas):
        self.tile_coords_text = Text(
            text=f"Tile: ({self.tx}, {self.ty})",
            color="white",
            font_size=12,
            anchor_x="right",
            anchor_y="top",
            parent=canvas.scene,
        )
        self.res_layers_text = Text(
            text=f"Resolutions: {self.resolutions_to_decompress}/{self.max_resolutions}  Layers: {self.default_layers}/{self.header_info.num_layers}",
            color="white",
            font_size=12,
            anchor_x="right",
            anchor_y="top",
            parent=canvas.scene,
        )
        self.decompression_time_text = Text(
            text="Decompression Time: -- ms",
            color="white",
            font_size=12,
            anchor_x="right",
            anchor_y="top",
            parent=canvas.scene,
        )
        self.position_osd(canvas)
        canvas.update()

    def position_osd(self, canvas):
        canvas_width = canvas.size[0]
        self.tile_coords_text.pos = (canvas_width - 10, canvas.size[1] - 20)
        self.res_layers_text.pos = (canvas_width - 10, canvas.size[1] - 40)
        self.decompression_time_text.pos = (canvas_width - 10, canvas.size[1] - 60)

    def update_osd(self):
        self.tile_coords_text.text = f"Tile: ({self.tx}, {self.ty})"
        tile_index = self.tx + self.ty * self.t_grid_width
        prog = grk_decompress_get_progression_state(self.codec, tile_index)
        self.res_layers_text.text = f"Resolutions: {self.resolutions_to_decompress}/{self.max_resolutions}  Layers: {prog.get_max_layers()}/{self.header_info.num_layers}"

    def update_views(self, canvas, views):
        tile_index = self.tx + self.ty * self.t_grid_width
        tile_data = self.decompress_tile(tile_index)
        if tile_data is None:
            print(f"Failed to decompress tile {tile_index}.")
            return

        img_height, img_width = tile_data.shape[:2]

        if not self.initialized:
            for view in views:
                view.camera.set_range(x=(0, img_width), y=(0, img_height))
            self.initialized = True

        self.display_components_in_views(canvas, tile_data, views)

    def display_components_in_views(self, canvas, image_data, views):
        max_value = 65535 if image_data.dtype == np.uint16 else 255

        for i, view in enumerate(views):
            if i >= image_data.shape[2]:
                print(f"Warning: Not enough components for view {i}.")
                break

            component_data = np.flipud(
                image_data[:, :, i].astype(np.float32) / max_value
            )

            if i < len(self.vispy_images) and self.vispy_images[i]:
                self.vispy_images[i].parent = None

            image_visual = scene.visuals.Image(
                component_data, parent=view.scene, cmap="gray"
            )
            if i < len(self.vispy_images):
                self.vispy_images[i] = image_visual
            else:
                self.vispy_images.append(image_visual)

    def key_press(self, canvas, views, event):
        needs_update = False
        """Handle arrow key presses to navigate the tile grid and update OSD."""
        if event.key == "Right" and self.tx < self.t_grid_width - 1:
            self.tx += 1
            needs_update = True
        elif event.key == "Left" and self.tx > 0:
            self.tx -= 1
            needs_update = True
        elif event.key == "Up" and self.ty > 0:
            self.ty -= 1
            needs_update = True
        elif event.key == "Down" and self.ty < self.t_grid_height - 1:
            self.ty += 1
            needs_update = True
        elif event.key == "l":
            tile_index = self.tx + self.ty * self.t_grid_width
            prog = grk_decompress_get_progression_state(self.codec, tile_index)
            if prog.get_max_layers() < self.header_info.num_layers:
                self.set_layers(prog.get_max_layers() + 1)
                tile_index = self.tx + self.ty * self.t_grid_width
                self.cache.set_dirty(tile_index)  # Mark tile as dirty
                needs_update = True

        # Update the view and OSD text after any key press
        if needs_update:
            self.update_views(canvas, views)
        else:
            self.update_decomression_time(0)
        self.update_osd()

    def set_layers(self, layers):
        if layers > self.header_info.num_layers:
            layers = self.header_info.num_layers
        tile_index = self.tx + self.ty * self.t_grid_width
        prog = grk_decompress_get_progression_state(self.codec, tile_index)
        prog.set_max_layers(layers)
        if grk_decompress_set_progression_state(self.codec, prog):
            self.update_osd()


def main():
    parser = argparse.ArgumentParser(
        description="16-bit Tile Viewer with Decompression"
    )
    parser.add_argument("-i", "--in-file", required=True, help="Input file path")
    parser.add_argument(
        "-l",
        "--layers",
        type=int,
        default=1,
        help="Initial number of decompressed layers for tile (default: 1)",
    )

    args = parser.parse_args()

    canvas = scene.SceneCanvas(
        keys="interactive", size=(1600, 1200), title="Tile Viewer"
    )
    context = TileViewerContext(args.in_file, args.layers)

    # Create a grid with enough cells for each component
    grid_size = int(np.ceil(np.sqrt(context.num_components)))
    grid = canvas.central_widget.add_grid(margin=10)
    views = []

    for i in range(context.num_components):
        row, col = divmod(i, grid_size)
        view = grid.add_view(row=row, col=col)
        view.camera = scene.PanZoomCamera(aspect=1)
        views.append(view)

    context.setup_osd(canvas)

    def on_key_press(event):
        context.key_press(canvas, views, event)

    def on_resize(event):
        context.position_osd(canvas)
        event.source.update()

    canvas.events.resize.connect(on_resize)
    canvas.events.key_press.connect(on_key_press)

    # Initial update to load and display the image in all views
    context.update_views(canvas, views)
    canvas.show()

    app.run()

    context.cleanup()
    canvas.close()
    app.quit()


if __name__ == "__main__":
    main()
