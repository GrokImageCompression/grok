# Install from Package Manager

1. **Debian** Grok `.deb` packages can be found [here](https://tracker.debian.org/pkg/libgrokj2k)

1. **Archlinux** Grok Archlinux packages can be found [here](https://aur.archlinux.org/packages/grok-jpeg2000/)

1. **Homebrew** Grok can be installed using the `grokj2k` brew formula

# Install from Release

Grok releases can be found [here](https://github.com/GrokImageCompression/grok/releases)

# Install from Source

## Perform a recursive clone, as there are submodules

`git clone --recursive https://github.com/GrokImageCompression/grok.git`

## Build

Grok uses [cmake](https://www.cmake.org) to configure builds across multiple platforms. It requires version 3.20 or higher.

## Compilers

Supported compilers:

1. g++ : version 12 or higher (C++23 required)
1. clang : version 16 or higher (C++23 required)
1. MSVC : 2022 or higher
1. Binaryen for WebAssembly

### g++

To ensure that g++ 12 is the default compiler after installation, execute:

`$ sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 100 --slave /usr/bin/g++ g++ /usr/bin/g++-12`

### Clang

To ensure that clang-16 is the default compiler after installation, execute:

```
$ sudo update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-16 60
$ sudo update-alternatives --config c++
```

The second line brings up a menu allowing a user to configure the default `c++` compiler, which is what is used by `cmake` to configure the project compiler.

### Binaryen

The Emscripten SDK can installed by following [these instructions](https://emscripten.org/docs/getting_started/downloads.html)
The SDK includes a helper script, `emcmake`, to configure cmake.

`emcmake` command:

`$ emcmake cmake -DBUILD_SHARED_LIBS=OFF -DGRK_BUILD_CODEC=OFF -DGRK_BUILD_LIBPNG=OFF -DBUILD_TESTING=OFF -DGRK_BUILD_CORE_EXAMPLES=ON  PATH/TO/SOURCE`

Now the core example that decompresses from a buffer can be runs as follows:

`$ node --experimental-wasm-threads bin/core_decompress_from_buf.js`

Note: WebAssembly by default is sand-boxed and not allowed to access the file system, so
only the `core_decompress_from_buf` example will run.

## Configuration

To configure a build using the defaults:

```
   $ mkdir /PATH/TO/BUILD
   $ cd /PATH/TO/BUILD
   $ cmake /PATH/TO/SOURCE
```

The `cmake` GUI is recommended, in order to view all `cmake` options.
On Linux distributions, `cmake-gui` will launch the cmake GUI.
On headless systems, `ccmake` (an ncurses application)
may be used to configure the build.


## *NIX

### Shared vs. Static

The `BUILD_SHARED_LIBS` `cmake` flag determines if the `grk_compress`
and `grk_decompress` binaries are linked dynamically or statically.

A static build on most systems will still link dynamically
to `glibc`. For a purely static build, the library can be built
on [Alpine Linux](https://www.alpinelinux.org/). Alpine uses
[musl libc](https://musl.libc.org/), which can be linked to statically.

Note: `cmake` must also be configured with `-DCMAKE_EXE_LINKER_FLAGS="-static"`.

### Fedora

1. if the Grok library has been installed and you would still like to run the binaries
from the build folder, then
`export LD_LIBRARY_PATH=/PATH/TO/BUILD/bin:/usr/local/lib64`
must be added to the `.bashrc` file. Note that the build binary folder is
entered before the system binary folder, so that build shared libraries
are given priority when loading at run time.
1. for a static build, the following library must be installed:
`sudo dnf install libstdc++-static`

### Debug/Release

Default build type is `Release`. For a `Debug` build, configure
`cmake` with `-DCMAKE_BUILD_TYPE=Debug`

### Build

`$ make -j8`

for a machine with 8 logical cores.

Binaries are located in the `bin` directory.

### Install

Root users may run:

`$ make install`

those with sudo powers can run:

`$ sudo make install`

and everyone else can run:

`$ DESTDIR=$HOME/local make install`

Note: On Linux, after a shared library build, run

`$ sudo ldconfig`

to update the shared library cache.

### Documentation

To build the Doxygen documentation (Doxygen needs to be found on the system):

`$ make doc`

A `HTML` directory is generated in the `doc` directory

### CMake Flags

Important `cmake` flags:

* To specify the install path: use `-DCMAKE_INSTALL_PREFIX=/path`, or use `DESTDIR` env variable (see above)
* To build the shared libraries and link the executables against it:

 `-DBUILD_SHARED_LIBS:bool=on` (default: `ON`)

  Note: when using this option, static libraries are not built and executables are dynamically linked.
* To build the core codec : `-DGRK_BUILD_CODEC:bool=ON` (default: `ON`)
* To build the documentation: `-DGRK_BUILD_DOC=ON` (default: `OFF`)
* To enable testing :

      $  cmake . -DBUILD_TESTING=ON -DGRK_DATA_ROOT:PATH='/PATH/TO/DATA/DIRECTORY'
      $  make -j8
      $  ctest -D NightlyMemCheck

Note : JPEG 2000 test files can be cloned
[here](https://github.com/GrokImageCompression/grok-test-data.git)
If the `-DGRK_DATA_ROOT:PATH` option is omitted,
test files will be automatically searched for in
`${CMAKE_SOURCE_DIR}/../grok-test-data`

### Python Tests

Python tests are automatically enabled when `BUILD_TESTING=ON` and SWIG bindings are built
(`GRK_BUILD_CORE_SWIG_BINDINGS=ON`). They require [pytest](https://docs.pytest.org/) to be installed.

To run the Python tests via `ctest`:

    $  cd /PATH/TO/BUILD
    $  ctest -R python_tests -V

To run them directly with `pytest`:

    $  cd /PATH/TO/BUILD/bin
    $  PYTHONPATH=/PATH/TO/BUILD/bin python -m pytest /PATH/TO/SOURCE/tests/python -v --tb=short

To run a single test file:

    $  cd /PATH/TO/BUILD/bin
    $  PYTHONPATH=/PATH/TO/BUILD/bin python -m pytest /PATH/TO/SOURCE/tests/python/test_roundtrip.py -v

Note: if the build has address sanitizer enabled (`-DWITH_SANITIZER=Address`),
Python tests will fail because the ASAN runtime must be loaded before Python.
Either rebuild without ASAN (`-DWITH_SANITIZER=OFF`) or preload the runtime:

    $  LD_PRELOAD=$(gcc -print-file-name=libasan.so) PYTHONPATH=/PATH/TO/BUILD/bin python -m pytest ...

### GPU Plugin Tests

GPU tests exercise the GPU plugin via `grk_compress` and `grk_decompress` binaries.
They require a CUDA-capable GPU with the plugin built. Tests are disabled by default.

To run GPU tests via `ctest`:

    $  cd /PATH/TO/BUILD
    $  ctest -R python_gpu_tests -V

Or filter by the `gpu` label:

    $  cd /PATH/TO/BUILD
    $  ctest -L gpu -V

To run them directly with `pytest`:

    $  cd /PATH/TO/SOURCE
    $  PYTHONPATH=/PATH/TO/BUILD/bin LD_LIBRARY_PATH=/PATH/TO/BUILD/bin \
       python -m pytest tests/python/test_gpu.py --run-gpu -v

All GPU compress operations use `-b 32,32` code block size (required for GPU decompress).
The `CUDA_MODULE_LOADING=EAGER` environment variable is set automatically by the CTest target.

Tests cover: single/batch compress and decompress, lossy/lossless, bit depths 8/10/12/16,
mono and RGB, cinema 2K profile, and CPU-only fallback (`-G -2`).

## macOS

macOS builds are configured similar to *NIX builds.
The Xcode project files can be generated using:

`$ cmake -G Xcode ....`


## Windows

### Shared vs. Static

The `BUILD_SHARED_LIBS` `cmake` flag determines if the `grk_compress` and `grk_decompress`
binaries are linked to dynamic or static builds of the codec library `libgrokj2k`,
and also if a static or dynamic version of `libgrokj2k` is built on the system.


### Compile

`cmake` can generate project files for various IDEs: Visual Studio, Eclipse CDT, NMake, etc.

Type `cmake --help` for available generators on your platform.

### Third Party Libraries

Third party libraries such as `libtiff` are built by default. To disable
`libtiff` library build and use the version installed on your system, set :

  `-DGRK_BUILD_LIBTIFF:BOOL=OFF`

  ## Linking with other Cmake Projects

  1. set `CMAKE_INSTALL_PREFIX` to `/PATH/TO/INSTALL/DIR`
  1. build and install
  1. on other project, set `CMAKE_PREFIX_PATH` to `/PATH/TO/INSTALL/DIR`

## Language Bindings

Grok provides bindings for Python, C#, Java, and Rust.

- **Python/C#/Java** (SWIG): see [bindings/swig/README.md](bindings/swig/README.md) for build instructions, API reference, and test information.
- **Rust** (bindgen): see [bindings/rust/README.md](bindings/rust/README.md) for build and usage instructions.

## GPU Plugin Integration

Grok supports GPU-accelerated Tier-1 encode and decode via the
closed source `grok-gpu-plugin` project.
The plugin offloads DWT, bit-plane coding, and MQ arithmetic coding to
NVIDIA CUDA GPUs while Grok handles file I/O, header parsing, and Tier-2
packet assembly.

### Requirements

| Dependency     | Version   | Notes                          |
|----------------|-----------|--------------------------------|
| CUDA Toolkit   | ≥ 11.0    | CUDA 12.x recommended          |
| NVIDIA GPU     | CC ≥ 6.0  | Tested on Ampere (CC 8.6)      |
| C++23 compiler | GCC 13+   | Also tested with Clang 17+     |
| CMake          | ≥ 3.21    |                                |

### Building the Plugin

The plugin is built independently from Grok as a shared library:

```bash
# Clone with submodules
git clone --recurse-submodules <plugin-repo-url>
cd grok-gpu-plugin

# Configure (shared library, CUDA enabled)
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Debug \
    -DGRK_GPU_BUILD_SHARED=ON \
    -DGRK_GPU_USE_CUDA=ON \
    -DGRK_GPU_CUDA_ARCH=86 \
    -DGRK_GPU_BUILD_TESTS=ON \
    -DGRK_GPU_BUILD_TOOLS=OFF

# Build
cmake --build build -j$(nproc)

# Verify (optional)
ctest --test-dir build --output-on-failure
```

Replace `86` with your GPU's compute capability (e.g., `75` for Turing,
`90` for Hopper).

### Installing the Plugin

Grok discovers the plugin by searching for `libgrokj2k_plugin.so` in the
same directory as the Grok executables. The plugin builds as
`libgrok_gpu_plugin.so`, so a symlink is required:

```bash
ln -sf /PATH/TO/grok-gpu-plugin/build/libgrok_gpu_plugin.so \
       /PATH/TO/grok/build/bin/libgrokj2k_plugin.so
```

The symlink must be recreated if the plugin or Grok build directory changes.

### Using the GPU Plugin

Pass `-k 1` to `grk_compress` or `grk_decompress` to enable GPU acceleration.

#### Single-file compress

```bash
grk_compress -i input.tiff -o output.jp2 -k 1
```

#### Single-file decompress

```bash
grk_decompress -i input.jp2 -o output.tif -k 1
```

#### Batch compress (directory of TIFFs)

```bash
grk_compress --batch-src /path/to/tiffs \
             --out-dir /path/to/output \
             --out-fmt jp2 \
             -k 1 -e 10
```

#### Batch decompress

```bash
grk_decompress --batch-src /path/to/jp2s \
               --out-dir /path/to/output \
               --out-fmt tif \
               -k 1 -e 10
```

The `-e` flag sets the number of executor threads for batch file I/O.

### Environment Variables

| Variable              | Value   | Purpose                                          |
|-----------------------|---------|--------------------------------------------------|
| `CUDA_MODULE_LOADING` | `EAGER` | Required. Ensures CUDA modules load at init time |
| `GRK_DEBUG`           | `1`–`5` | Optional. Verbosity: 1=error 2=warn 3=info 4=debug 5=trace. Level ≥ 3 enables plugin verbose output |

### Plugin Constraints

- Single tile per image (no multi-tile images)
- Precision: 8, 10, 12, or 16 bits per component
- No subsampling (`dx = dy = 1`)
- Maximum 4 components
- No code block style extensions (`cblk_sty = 0`)
- Unsigned samples only

### Known Issues

- **Python bindings**: When `libgrokj2k_plugin.so` is present in `build/bin/`,
  the plugin's auto-discovery intercepts Python SWIG binding calls to
  `grk_codec_compress()`. Remove or rename the symlink before running Python
  tests via `ctest`.

- **CUDA cleanup at exit**: The executables call `grk_deinitialize()` before
  returning from `main()` to ensure CUDA resources are released before the
  CUDA runtime unloads. This is handled automatically by `grk_compress` and
  `grk_decompress`; library consumers should call `grk_deinitialize()` at
  process exit if using the GPU plugin.

### Debugging with VS Code

The repository includes GPU launch configurations in `.vscode/launch.json`:

| Configuration               | Description                                          |
|-----------------------------|------------------------------------------------------|
| `single compress GPU`       | Single-file GPU compress                             |
| `single decompress GPU`     | Single-file GPU decompress                           |
| `compress GPU`              | Batch GPU compress (directory of TIFFs)              |
| `decompress GPU`            | Batch GPU decompress (directory of JP2s)             |

These configs set `CUDA_MODULE_LOADING=EAGER` and `GRK_DEBUG=3` automatically.