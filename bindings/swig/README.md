Grok has Python bindings through [SWIG](https://github.com/swig/swig)

## Dependencies

### Ubuntu
sudo apt install python3-dev swig

### Fedora
sudo dnf install python3-devel swig

## Using Python bindings in Python script

1. Set `CMAKE_INSTALL_PREFIX` to `$INSTALL_DIR`
1. enable CMake option `GRK_BUILD_CORE_SWIG_BINDINGS`
1. make install
1. `export PYTHONPATH=$INSTALL_DIR/bin:$PYTHONPATH`

Now Grok api methods can be imported, for example

```
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
```

See the `viewer` folder for a working example of how to use the bindings.