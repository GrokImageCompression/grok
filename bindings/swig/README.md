Grok has Python bindings through [SWIG](https://github.com/swig/swig).
The bindings are enabled by default and will build automatically if SWIG and
Python 3 development files are installed.

## Dependencies

### Ubuntu
```
sudo apt install python3-dev swig
```

### Fedora
```
sudo dnf install python3-devel swig
```

## Build

The bindings are controlled by `GRK_BUILD_CORE_SWIG_BINDINGS` (ON by default).
If SWIG or Python3 development headers are missing the build will skip the
bindings and print a status message.

```
cmake -B build
cmake --build build --target grok_core
```

## Setup

After building, add the build output directory to your Python path:

```
export PYTHONPATH=$PWD/build/bin:$PYTHONPATH
export LD_LIBRARY_PATH=$PWD/build/bin:$LD_LIBRARY_PATH
```

## Quick Start

```python
import grok_core

# Initialize the library (call once)
grok_core.grk_initialize(None, 0, None)

# Get version
print(grok_core.grk_version())
```

## API Reference

### Library lifecycle

| Function | Description |
|----------|-------------|
| `grk_initialize(plugin_path, num_threads, plugin_initialized)` | Initialize library. Pass `None, 0, None` for defaults. |
| `grk_version()` | Returns version string, e.g. `"20.2.1"` |
| `grk_object_unref(obj)` | Release a Grok object. For images use `img.obj`. |

### Image creation

| Function | Description |
|----------|-------------|
| `grk_image_new(numcmpts, cmptparms, clrspc, alloc_data)` | Create image from component descriptors (single-component). |
| `grk_image_new_uniform(numcmpts, w, h, dx, dy, prec, sgnd, clrspc)` | Create a multi-component image where all components share the same dimensions. |

### Parameter helpers

SWIG does not support item assignment on C arrays.  Use these helpers to set
`layer_rate` and `layer_distortion` values on `grk_cparameters`:

| Function | Description |
|----------|-------------|
| `grk_cparameters_set_layer_rate(params, layer, rate)` | Set `params.layer_rate[layer] = rate`. |
| `grk_cparameters_set_layer_distortion(params, layer, distortion)` | Set `params.layer_distortion[layer] = distortion`. |

#### Lossy compression example

```python
params = grok_core.grk_cparameters()
grok_core.grk_compress_set_default_params(params)
params.irreversible = True
params.numlayers = 1
params.allocation_by_rate_distortion = True
grok_core.grk_cparameters_set_layer_rate(params, 0, 20.0)
```

### Compression

```python
params = grok_core.grk_cparameters()
grok_core.grk_compress_set_default_params(params)
params.cod_format = grok_core.GRK_FMT_JP2  # or GRK_FMT_J2K

image = grok_core.grk_image_new_uniform(
    3, 640, 480, 1, 1, 8, False, grok_core.GRK_CLRSPC_SRGB
)
# ... fill image component data via ctypes ...

stream = grok_core.grk_stream_params()
stream.file = "output.jp2"

codec = grok_core.grk_compress_init(stream, params, image)
length = grok_core.grk_compress(codec, None)

grok_core.grk_object_unref(codec)
grok_core.grk_object_unref(image.obj)
```

### Decompression

```python
stream = grok_core.grk_stream_params()
stream.file = "input.jp2"

params = grok_core.grk_decompress_parameters()
codec = grok_core.grk_decompress_init(stream, params)

header = grok_core.grk_header_info()
grok_core.grk_decompress_read_header(codec, header)

image = grok_core.grk_decompress_get_image(codec)
grok_core.grk_decompress(codec, None)

# Access pixel data via ctypes
import ctypes
comp = image.comps[0]
data_ptr = ctypes.cast(
    int(comp.data),
    ctypes.POINTER(ctypes.c_int32 * (comp.h * comp.stride)),
)
pixels = data_ptr.contents

grok_core.grk_object_unref(codec)
```

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `GRK_CLRSPC_GRAY` | 3 | Grayscale |
| `GRK_CLRSPC_SRGB` | 2 | sRGB |
| `GRK_FMT_J2K` | 1 | Raw JPEG 2000 codestream |
| `GRK_FMT_JP2` | 2 | JP2 container format |
| `GRK_LRCP` | 0 | Layer-resolution-component-precinct progression |
| `GRK_TILE_CACHE_NONE` | 0 | No tile caching |
| `GRK_TILE_CACHE_IMAGE` | 1 | Cache tile images |
| `GRK_TILE_CACHE_ALL` | 2 | Cache everything |

## Running Tests

Python tests require `pytest`:

```
pip install pytest
```

Tests run automatically as part of `make test` (or `ctest`) when `BUILD_TESTING=ON`:

```
cmake -B build -DBUILD_TESTING=ON
cmake --build build
cd build && ctest -R python_tests -V
```

To run tests independently of the C++ test suite:

```
cmake -B build -DGRK_BUILD_CORE_SWIG_BINDINGS=ON -DGRK_BUILD_PYTHON_TESTS=ON
cmake --build build --target grok_core
cd build && ctest -R python_tests -V
```

## Examples

See the `viewer` folder for an interactive JP2000 tile viewer, and
`examples/core/core_decompress.py` for a decompression example.