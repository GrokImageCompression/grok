## Install from Package Manager

1. **Debian** Grok .deb packages can be found [here](https://tracker.debian.org/pkg/libgrokj2k)

1. **Archlinux** Grok Archlinux packages can be found [here](https://aur.archlinux.org/packages/grok-jpeg2000/)

1. **Homebrew** Grok can be installed using the `grokj2k` brew formula

## Install from Release

Grok releases can be found [here](https://github.com/GrokImageCompression/grok/releases)

## Install from Source

Grok uses [cmake](www.cmake.org) to configure builds across multiple platforms.
It requires version 3.16 and higher.


### Compilers

Supported compilers:

1. g++ 10 and higher
1. clang 12 and higher
1. MSVC 2019 and higher (mingw compiler not supported)
1. Binaryen for WebAssembly

#### g++

To ensure that g++ 10 is the default compiler after installation, execute

$ sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-10 100 --slave /usr/bin/g++ g++ /usr/bin/g++-10

#### Clang

For clang-12, execute:

```
$ sudo update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-12 60
$ sudo update-alternatives --config c++
```

The second line brings up a menu allowing a user to configure the default `c++` compiler, which is
what is used by `cmake` to configure the project compiler.

#### Binaryen

Emscripten SDK can installed following [these instructions](https://emscripten.org/docs/getting_started/downloads.html)
The SDK includes a helper script, `emcmake`, to configure cmake.

`emcmake` command:

`$ emcmake cmake -DBUILD_SHARED_LIBS=OFF -DGRK_BUILD_CODEC=OFF -DGRK_BUILD_LIBPNG=OFF -DBUILD_TESTING=OFF -DGRK_BUILD_CORE_EXAMPLES=ON  GROK/SRC/FOLDER`

Now the library example can be runs as follows:

`$ node --experimental-wasm-threads --experimental-wasm-bulk-memory bin/library_decompress_from_file.js`


### Configuration

To configure a build using the defaults:

```
   $ mkdir /PATH/TO/BUILD
   $ cd /PATH/TO/BUILD
   $ cmake /PATH/TO/SOURCE
```

The `cmake` GUI is recommended, in order to easily view all `cmake` options.
On Linux distributions, `cmake-gui` will launch the cmake GUI.
On system without a window manager, `ccmake` (an ncurses application)
may be used to configure the build.


### *NIX

#### Shared vs. Static

The `BUILD_SHARED_LIBS` `cmake` flag determines if the `grk_compress`
and `grk_decompress` binaries are linked to dynamic or static builds
of the library `libgrokj2k`.


##### Fedora

1. if the Grok library has been installed and you would still like to run the binaries
from the build folder, then
`export LD_LIBRARY_PATH=/PATH/TO/BUILD/bin:/usr/local/lib64`
must be added to the `.bashrc` file. Note that the build binary folder is
entered before the system binary folder, so that build shared libraries
are given priority when loading at run time.
1. for a static build, the following library must be installed:
`sudo dnf install libstdc++-static`

#### Debug/Release

Default build type is `Release`. For a `Debug` build, run:

`$ cmake -DCMAKE_BUILD_TYPE=Debug /PATH/TO/SOURCE`

#### Build

`$ make -j8`

for a machine with 8 logical cores.

Binaries are located in the `bin` directory.

#### Install

Root users may run:

`$ make install`

those with sudo powers may run:

`$ sudo make install`

and everyone else can run:

`$ DESTDIR=$HOME/local make install`

Note: On Linux, after a shared library build, run

`$ sudo ldconfig`

to update the shared library cache.

#### Documentation

To build the Doxygen documentation (Doxygen needs to be found on the system):

`$ make doc`

A `HTML` directory is generated in the `doc` directory

#### Cmake Flags

Important `cmake` flags:

* To specify the install path: use `-DCMAKE_INSTALL_PREFIX=/path`, or use `DESTDIR` env variable (see above)
* To build the shared libraries and link the executables against it:

 `-DBUILD_SHARED_LIBS:bool=on` (default: `ON`)

  Note: when using this option, static libraries are not built and executables are dynamically linked.
* To build the core codec : `-DGRK_BUILD_CODEC:bool=ON` (default: `ON`)
* To build the documentation: `-GRK_BUILD_DOC=ON` (default: `OFF`)
* To enable testing :

      $  cmake . -BUILD_TESTING=ON -DGRK_DATA_ROOT:PATH='PATH/TO/DATA/DIRECTORY'
      $  make -j8
      $  ctest -D NightlyMemCheck

Note : JPEG 2000 test files can be cloned
[here](https://github.com/GrokImageCompression/grok-test-data.git)
If the `-DGRK_DATA_ROOT:PATH` option is omitted,
test files will be automatically searched for in
`${CMAKE_SOURCE_DIR}/../grok-test-data`


## Macos

Macos builds are configured similar to *NIX builds.
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
these library builds and use the version installed on your system, set :

  `-DGRK_BUILD_LiBTIFF:BOOL=OFF`

#### JPEG Support

To encode and decode JPEG files, a `libjpeg`-compatible library
(`-dev` version) must be installed.
Recommended library : [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo)
On Debian systems, the `libjpeg-turbo8-dev` package will provide a development
version of the library.

##### Grok dynamic build with JPEG support (Windows)

`libjpeg-turbo` must be built with the `WITH_CRT_DLL` flag on, to ensure that the dynamic version of the C runtime libraries is used. Also, if Grok is linking with dynamic build of `libjpeg-turbo`, (`cmake` flag `JPEG_LIBRARY` is set to `LIBJPEG_INSTALL_DIRECTORY/jpeg.lib`), then make sure that `LIBJPEG_INSTALL_DIRECTORY/bin` is on the path.

##### Grok static build with JPEG support (Windows)

`libjpeg-turbo` must be built with the `WITH_CRT_DLL` flag off, to ensure that the static version of the C runtime libraries is used.
