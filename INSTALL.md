## Install from Package Manager

1. **Debian** Grok .deb packages can be found [here](https://tracker.debian.org/pkg/libgrokj2k)

1. **Archlinux** Grok Archlinux packages can be found [here](https://aur.archlinux.org/packages/grok-jpeg2000/)

1. **Homebrew** Grok can be installed using the `grokj2k` brew formula

## Install from Release

Grok releases can be found [here](https://github.com/GrokImageCompression/grok/releases)

## Install from Source

Grok uses [cmake](www.cmake.org) to configure builds across multiple platforms.

### compilers

Supported compilers:

1. g++ 10 and higher
1. clang 12 and higher
1. MSVC 2019 and higher (mingw compiler not supported)

To ensure that g++ 10 is the default compiler after installation, execute

$ sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-10 100 --slave /usr/bin/g++ g++ /usr/bin/g++-10

For clang-12, execute:

```
$ sudo update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-12 60
$ sudo update-alternatives --config c++
```

The second line brings up a menu allowing a user to configure the default `c++` compiler, which is
what is used by `cmake` to configure the project compiler.

### configuration

To configure a build using the defaults:

```
   $ mkdir /PATH/TO/BUILD
   $ cd /PATH/TO/BUILD
   $ cmake /PATH/TO/SOURCE
```

The `cmake` GUI is recommended, in order to easily view all `cmake` options. On headless systems, `ccmake` (an ncurses application) may be used to configure the build.


### *NIX

#### SHARED/STATIC

The `BUILD_SHARED_LIBS` `cmake` flag determines if the `grk_compress` and `grk_decompress` binaries
are linked to dynamic or static builds of the codec library `libgrokj2k`.

If both `BUILD_SHARED_LIBS` and `BUILD_STATIC_LIBS` `cmake` flags are set,
then both dynamic and static builds are generated and installed.


#### DEBUG/RELEASE

Default build type is `Release`. For a `Debug` build, run:

`$ cmake -DCMAKE_BUILD_TYPE=Debug /PATH/TO/SOURCE`

#### Build

`$ make -j8`

for a machine with 8 logical cores.

Binaries are located in the `bin` directory.

#### INSTALL

Root users may run:

`$ make install`

those with sudo powers may run:

`$ sudo make install`

and everyone else can run:

`$ DESTDIR=$HOME/local make install`

Note: On Linux, after a shared library build, run

`$ sudo ldconfig`

to update the shared library cache.

#### DOCUMENTATION

To build the Doxygen documentation (Doxygen needs to be found on the system):

`$ make doc`

A `HTML` directory is generated in the `doc` directory

#### CMAKE FLAGS

Important `cmake` flags:

* To specify the install path: use `-DCMAKE_INSTALL_PREFIX=/path`, or use `DESTDIR` env variable (see above)
* To build the shared libraries and link the executables against it:

 `-DBUILD_SHARED_LIBS:bool=on` (default: `ON`)

  Note: when using this option, static libraries are not built and executables are dynamically linked.
* To build the core codec : `-DBUILD_CODEC:bool=on` (default: `ON`)
* To build the documentation: `-DBUILD_DOC:bool=on` (default: `OFF`)
* To enable testing (with results automatically uploaded to http://my.cdash.org/index.php?project=grok)

      $  cmake . -DBUILD_TESTING:BOOL=ON -DGRK_DATA_ROOT:PATH='path/to/the/data/directory'
      $  make
      $  make Experimental

Note : JPEG 2000 test files can be cloned [here](https://github.com/GrokImageCompression/grok-test-data.git)
If the `-DGRK_DATA_ROOT:PATH` option is omitted, test files will be automatically searched for in
 `${CMAKE_SOURCE_DIR}/../grok-test-data`


## OSX

OSX builds are configured similar to Unix builds.
The Xcode project files can be generated using:

`$ cmake -G Xcode ....`


## WINDOWS

### SHARED/STATIC

The `BUILD_SHARED_LIBS` `cmake` flag determines if the `grk_compress` and `grk_decompress` binaries are linked to dynamic or static builds of the codec library `libgrokj2k`, and also if a static or dynamic version of `libgrokj2k` is built on the system.


### Compile

`cmake` can generate project files for various IDEs: Visual Studio, Eclipse CDT, NMake, etc.

Type `cmake --help` for available generators on your platform.

### Third Party Libraries

Make sure to build the third party libs (`libpng`, `zlib` etc.) :

  `-DBUILD_THIRDPARTY:BOOL=ON`

#### JPEG Support

To encode and decode JPEG files, a `libjpeg`-compatible library (`-dev` version) must be installed.
Recommended library : [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo)
On Debian systems, the `libjpeg-turbo8-dev` package will provide a development
version of the library.

##### Grok dynamic build with JPEG support (Windows)

`libjpeg-turbo` must be built with the `WITH_CRT_DLL` flag on, to ensure that the dynamic version of the C runtime libraries is used. Also, if Grok is linking with dynamic build of `libjpeg-turbo`, (`cmake` flag `JPEG_LIBRARY` is set to `LIBJPEG_INSTALL_DIRECTORY/jpeg.lib`), then make sure that `LIBJPEG_INSTALL_DIRECTORY/bin` is on the path.

##### Grok static build with JPEG support (Windows)

`libjpeg-turbo` must be built with the `WITH_CRT_DLL` flag off, to ensure that the static version of the C runtime libraries is used.
