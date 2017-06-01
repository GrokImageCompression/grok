# How to Build and Install Grok binaries


Grok uses cmake (www.cmake.org) to configure builds across multiple platforms.

To configure using defaults, create a build directory `/PATH/TO/BUILD`,
change to this directory, and run:

`$ cmake /PATH/TO/SOURCE`


## UNIX/LINUX/OSX


### DEBUG/RELEASE

Default build type is `Release`. For a `Debug` build, run:

`$ cmake -DCMAKE_BUILD_TYPE=Debug /PATH/TO/SOURCE`

### INSTALL

Root users can run:

`$ make install`

Those with sudo powers can run:
`$ sudo make install`

Everyone else can run:

`$ DESTDIR=$HOME/local make install`

Note: On Linux, for a shared library build, you must run

`$ sudo ldconfig`

to update the shared library cache.

### DOCUMENTATION

To build the Doxygen documentation (Doxygen needs to be found on the system):
(A 'html' directory is generated in the `doc` directory)
`$ make doc`

Binaries are located in the `bin` directory.

### CMAKE FLAGS

Main available cmake flags:
* To specify the install path: use `-DCMAKE_INSTALL_PREFIX=/path`, or use `DESTDIR` env variable (see above)
* To build the shared libraries and links the executables against it: `-DBUILD_SHARED_LIBS:bool=on` (default: `ON`)
  Note: when using this option, static libraries are not built and executables are dynamically linked.
* To build the CODEC executables: `-DBUILD_CODEC:bool=on` (default: `ON`)
* To build the documentation: `-DBUILD_DOC:bool=on` (default: `OFF`)
* To enable testing (and automatic result upload to http://my.cdash.org/index.php?project=grok)

      $  cmake . -DBUILD_TESTING:BOOL=ON -DGROK_DATA_ROOT:PATH='path/to/the/data/directory'
      $  make
      $  make Experimental
  Note : JPEG2000 test files are can be cloned here `https://github.com/GrokImageCompression/grok-test-data.git`
  
  If `-DGROK_DATA_ROOT:PATH` option is omitted, test files will be automatically searched in `${CMAKE_SOURCE_DIR}/../grok-test-data`


## OSX


The xcode project files can be generated using:

`$ cmake -G Xcode ....`



## WINDOWS


### Compile

cmake can generate project files for the IDE you are using (VS2010, NMake, etc).
Type `cmake --help` for available generators on your platform.

### Third Party Libraries

Make sure to build the third party libs (libpng, zlib etc.):

  `-DBUILD_THIRDPARTY:BOOL=ON`
  
To open JPEG files, you will need to build and install a `libjpeg` compatible library (dev version). Recommended : libjpeg-turbo
https://github.com/libjpeg-turbo/libjpeg-turbo

If you are building Grok as a dynamic build, and you are also building `libjpeg-turbo`, then `libjpeg-turbo` should be built
with the `WITH_CRT_DLL` flag on, to ensure that the DLL version of the C runtime libraries is used. Also, ensure that the Grok
cmake flag `JPEG_LIBRARY` is set to `LIBJPEG_INSTALL_DIRECTORY/jpeg.lib`, and that `LIBJPEG_INSTALL_DIRECTORY/bin` is on the path.


