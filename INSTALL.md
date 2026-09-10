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

`$ make -j$(nproc)`

for a machine with multiple logical cores.

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
* To enable testing : see [TESTING.md](TESTING.md)

### macOS

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

  The Python modules install under the install prefix. When using a custom
  `CMAKE_INSTALL_PREFIX`, add them to `PYTHONPATH`, e.g.:

  `export PYTHONPATH=$HOME/bin/grok/lib64/python3.14/site-packages:$PYTHONPATH`
- **Rust** (bindgen): see [bindings/rust/README.md](bindings/rust/README.md) for build and usage instructions.

## GPU Plugin Integration

Grok supports GPU-accelerated Tier-1 encode and decode via the
closed source `grok-gpu-plugin` project (submodule
`extern/grok-gpu-plugin`). The plugin offloads DWT, bit-plane coding, and
MQ arithmetic coding to an NVIDIA CUDA GPU or Apple Silicon Metal GPU
while Grok handles file I/O, header parsing, and Tier-2 packet assembly.

Full Metal prerequisites and CMake options live in
[extern/grok-gpu-plugin/README.md](extern/grok-gpu-plugin/README.md).

### Requirements

| Dependency     | Version   | Notes                                      |
|----------------|-----------|--------------------------------------------|
| CMake          | ≥ 3.21    |                                            |
| C++23 compiler | GCC 13+ / Clang 17+ / Apple Clang |                       |
| CUDA Toolkit   | ≥ 11.0    | CUDA backend only                          |
| NVIDIA GPU     | CC ≥ 6.0  | CUDA. Tested on Ampere (CC 8.6)            |
| Xcode          | ≥ 15      | Metal. Full Xcode.app + Metal toolchain    |
| Apple Silicon  | M1+       | Metal. Tested on M1 Pro, M2 Max, M4, M5    |
| libtiff        | any recent | `brew install libtiff` on macOS           |

### Building the plugin with Grok (recommended)

The submodule is marked `update = none`, so a recursive clone skips it:

```bash
git submodule update --init --checkout extern/grok-gpu-plugin
git -C extern/grok-gpu-plugin submodule update --init --recursive
```

Then configure Grok with the plugin loader. On Apple Silicon, disable CUDA
and enable Metal. CMake names the library `libgrokj2k_plugin` and copies
`grok_kernels.metallib` next to it — no extra symlink.

```bash
# NVIDIA
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DGRK_BUILD_PLUGIN_LOADER=ON \
    -DGPUP_USE_CUDA=ON \
    -DGPUP_CUDA_ARCH=86          # your GPU compute capability

# Apple Silicon
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DGRK_BUILD_PLUGIN_LOADER=ON \
    -DGPUP_USE_CUDA=OFF \
    -DGPUP_USE_METAL=ON

cmake --build build --parallel
```

On current macOS, Command Line Tools often have no `metal` compiler. Point
`xcode-select` at Xcode.app and, if needed, run
`xcodebuild -downloadComponent MetalToolchain`. Details are in the plugin
README.

### Plugin discovery

Grok looks for `libgrokj2k_plugin` (`.so` / `.dylib` / `.dll`) in:

1. the directory `GRK_PLUGIN_PATH` names
2. the current working directory
3. the directory of the Grok executable

It does **not** search `LD_LIBRARY_PATH`, `DYLD_LIBRARY_PATH`, or `PATH`.
On Metal the same directory must also contain `grok_kernels.metallib`.

```bash
export GRK_PLUGIN_PATH=/PATH/TO/grok/build/bin   # or $PREFIX/lib after install
export GRK_DEBUG=3                               # log the load path and device
```

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
| `GRK_PLUGIN_PATH`     | dir     | Directory that contains `libgrokj2k_plugin` (and `grok_kernels.metallib` on Metal) |
| `CUDA_MODULE_LOADING` | `EAGER` | CUDA only. Ensures CUDA modules load at init time |
| `GRK_DEBUG`           | `1`–`5` | Optional. Verbosity: 1=error 2=warn 3=info 4=debug 5=trace. Level ≥ 3 enables plugin verbose output |

### Plugin Constraints

- Single tile per image (no multi-tile images)
- Precision: 8, 10, 12, or 16 bits per component
- No subsampling (`dx = dy = 1`)
- Maximum 4 components
- No code block style extensions (`cblk_sty = 0`)
- Unsigned samples only
- **Colour on the device** (16-bit Rec.709 RGB→XYZ, planar 8/10-bit YUV→RGB/XYZ,
  inverse XYZ→sRGB, and the display LUT) runs on CUDA, HIP and Metal. See the
  plugin README Metal notes.

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