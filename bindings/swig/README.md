Grok has Python, C#, and Java bindings through [SWIG](https://github.com/swig/swig).
Python bindings are enabled by default and will build automatically if SWIG and
Python 3 development files are installed. C# and Java bindings are opt-in.

## Dependencies

### Ubuntu / Debian
```
sudo apt install python3-dev swig
# For C#: install .NET SDK 8.0+ (https://dotnet.microsoft.com/download)
# For Java: sudo apt install default-jdk  (JDK 17+)
```

### Fedora
```
sudo dnf install python3-devel swig
# For C#: install .NET SDK 8.0+ (https://dotnet.microsoft.com/download)
# For Java: sudo dnf install java-latest-openjdk-devel
```

### macOS
```
brew install swig python
# For C#: install .NET SDK 8.0+ (https://dotnet.microsoft.com/download)
# For Java: brew install openjdk  (JDK 17+)
```

### Windows
- Install [SWIG](https://www.swig.org/download.html) and add it to `PATH`
- Install Python 3 from [python.org](https://www.python.org/)
- For C#: install [.NET SDK 8.0+](https://dotnet.microsoft.com/download)
- For Java: install a JDK 17+ and ensure `JAVA_HOME` is set

## Build

### Python (default)

The Python bindings are controlled by `GRK_BUILD_CORE_SWIG_BINDINGS` (ON by default).
If SWIG or Python3 development headers are missing the build will skip the
bindings and print a status message.

```
cmake -B build
cmake --build build --target grok_core
```

### C#

Enable with `GRK_BUILD_CSHARP_SWIG_BINDINGS`:

```
cmake -B build -DGRK_BUILD_CSHARP_SWIG_BINDINGS=ON
cmake --build build --target grok_core_csharp
```

The generated C# sources and native library are placed in `build/bin/csharp/`.
A .NET project can reference these files directly, or use the provided
`bindings/swig/csharp/GrokCore.csproj` as a library project.

### Java

Enable with `GRK_BUILD_JAVA_SWIG_BINDINGS`:

```
cmake -B build -DGRK_BUILD_JAVA_SWIG_BINDINGS=ON
cmake --build build --target grok_core_java
```

The generated Java sources are under `build/bin/java/org/grok/core/`.
The native JNI library (`libgrok_core_java.so` / `.dylib` / `.dll`)
is in `build/bin/`.

To compile and use:
```
javac -d classes build/bin/java/org/grok/core/*.java
java -Djava.library.path=build/bin -cp classes org.grok.core.YourApp
```

## Environment Setup

After building, you need to ensure that the native libraries are findable
at runtime. The build places all output under `build/bin/`.

### Python — Linux / macOS
```bash
export PYTHONPATH=$PWD/build/bin:$PYTHONPATH
export LD_LIBRARY_PATH=$PWD/build/bin:$LD_LIBRARY_PATH   # Linux
export DYLD_LIBRARY_PATH=$PWD/build/bin:$DYLD_LIBRARY_PATH  # macOS
```

### Python — Windows (PowerShell)
```powershell
$env:PYTHONPATH = "$PWD\build\bin;$env:PYTHONPATH"
$env:PATH = "$PWD\build\bin;$env:PATH"
```

### Python — Windows (cmd)
```cmd
set PYTHONPATH=%CD%\build\bin;%PYTHONPATH%
set PATH=%CD%\build\bin;%PATH%
```

### C# — all platforms
The native library (`libgrok_core_csharp.so` / `.dylib` / `grok_core_csharp.dll`)
must be next to your executable or on the library search path.
The build copies it into `build/bin/csharp/` alongside the generated sources.

### Java — all platforms
Pass `-Djava.library.path=build/bin` when running `java`, or copy the
native library (`libgrok_core_java.so` / `.dylib` / `grok_core_java.dll`)
to a directory on your system library path.

## Quick Start

### Python

```python
import grok_core

# Initialize the library (call once)
grok_core.grk_initialize(None, 0, None)

# Get version
print(grok_core.grk_version())
```

### C#

Create a console project and reference the generated sources:

```bash
dotnet new console -n GrokDemo
cd GrokDemo
```

Add the native library and generated `.cs` files to your project, then:

```csharp
using System;
using System.Runtime.InteropServices;

class Program {
    static void Main() {
        grok_core.grk_initialize(null, 0, null);
        Console.WriteLine(grok_core.grk_version());
        grok_core.grk_deinitialize();
    }
}
```

### Java

```java
package org.grok.core;

public class GrokDemo {
    static { System.loadLibrary("grok_core_java"); }

    public static void main(String[] args) {
        grok_core.grk_initialize(null, 0, null);
        System.out.println(grok_core.grk_version());
        grok_core.grk_deinitialize();
    }
}
```

Compile and run:
```bash
javac -cp build/bin/java -d classes GrokDemo.java
java -Djava.library.path=build/bin -cp classes:build/bin/java org.grok.core.GrokDemo
```

## API Reference

The SWIG bindings expose the same C API across all three languages. The
examples below use Python; the function names and constants are identical
in C# and Java (accessed via the `grok_core` wrapper class).

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

# Fill pixel data via ctypes
import ctypes
for c in range(3):
    comp = image.comps[c]
    n = comp.h * comp.stride
    data_ptr = ctypes.cast(int(comp.data), ctypes.POINTER(ctypes.c_int32 * n))
    for i in range(n):
        data_ptr.contents[i] = 128  # or your pixel values

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

### Python

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

### C#

Requires .NET SDK 8.0+:

```
cmake -B build -DBUILD_TESTING=ON -DGRK_BUILD_CSHARP_SWIG_BINDINGS=ON
cmake --build build
cd build && ctest -R csharp_tests -V
```

### Java

Requires JDK 17+:

```
cmake -B build -DBUILD_TESTING=ON -DGRK_BUILD_JAVA_SWIG_BINDINGS=ON
cmake --build build
cd build && ctest -R java_tests -V
```

### All bindings at once

```
cmake -B build -DBUILD_TESTING=ON \
  -DGRK_BUILD_CSHARP_SWIG_BINDINGS=ON \
  -DGRK_BUILD_JAVA_SWIG_BINDINGS=ON
cmake --build build
cd build && ctest -R "python_tests|csharp_tests|java_tests" -V
```

## Examples

See the `viewer` folder for an interactive JP2000 tile viewer, and
`examples/core/core_decompress.py` for a decompression example.