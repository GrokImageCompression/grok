	include(FindPkgConfig)

# Try to find lib Z
if(GRK_BUILD_LIBPNG OR GRK_BUILD_LIBTIFF)
  message(STATUS "zlib will be built.")
  set(ZLIB_ENABLE_TESTS OFF)
  set(ZLIB_COMPAT ON)
  set(BUILD_SHARED_LIBS OFF)
  add_subdirectory(libz EXCLUDE_FROM_ALL)
  set(ZLIB_FOUND 1)
else(GRK_BUILD_LIBPNG OR GRK_BUILD_LIBTIFF)
  find_package(ZLIB)
  if(ZLIB_FOUND)
    set(Z_LIBNAME ${ZLIB_LIBRARIES} PARENT_SCOPE)
    message(STATUS "The system seems to have a zlib available; it will be used to build libpng")
    # message(STATUS "DEBUG: ${ZLIB_INCLUDE_DIRS} vs ${ZLIB_INCLUDE_DIR}")
  else(ZLIB_FOUND) 
    message(STATUS "zlib not found, please activate GRK_BUILD_LIBPNG to build it (required by libpng)")
  endif(ZLIB_FOUND)
endif(GRK_BUILD_LIBPNG OR GRK_BUILD_LIBTIFF)

# JPEG library
#if(CMAKE_GENERATOR STREQUAL "Ninja")
#  set(GRK_BUILD_JPEG OFF)
#  message(STATUS "Ninja generator detected. GRK_BUILD_JPEG disabled. For jpeg support, libjeg-turbo can be independantly installed.")
#endif()
if (GRK_BUILD_JPEG)
  message(STATUS "We will build libjpeg-turbo library")
  set(GROK_HAVE_LIBJPEG 1 PARENT_SCOPE)
  # Determine the appropriate library filename based on the platform
	if(WIN32)
	    set(JPEG_LIBNAME jpeg-static.lib)
	else()
	    set(JPEG_LIBNAME libjpeg.a)
  endif()
  if(CMAKE_GENERATOR STREQUAL "Ninja")
    include(FetchContent)
	FetchContent_Declare(
	    libjpeg-turbo
	    GIT_REPOSITORY https://github.com/libjpeg-turbo/libjpeg-turbo.git
	    GIT_TAG        2.1.5.1
	)
	# Set configuration variables before populating and adding the subdirectory
	set(ENABLE_SHARED OFF CACHE BOOL "" FORCE)
	set(ENABLE_STATIC ON CACHE BOOL "" FORCE)
	set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)
	set(WITH_TURBOJPEG OFF CACHE BOOL "" FORCE)
	FetchContent_GetProperties(libjpeg-turbo)
	set(JPEG_LIBNAME ${CMAKE_CURRENT_BINARY_DIR}/../bin/${JPEG_LIBNAME} PARENT_SCOPE)
	if(NOT libjpeg-turbo_POPULATED)
	    FetchContent_Populate(libjpeg-turbo)
	    set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/libjpeg-turbo" CACHE PATH "" FORCE) # Set install prefix after population but before add_subdirectory
	    add_subdirectory(${libjpeg-turbo_SOURCE_DIR} ${libjpeg-turbo_BINARY_DIR} EXCLUDE_FROM_ALL)
	endif()
	# Set the JPEG library path and include directory to be used elsewhere in the project
	set(JPEG_INCLUDE_DIRS
	    ${CMAKE_CURRENT_BINARY_DIR}/../_deps/libjpeg-turbo-src
	    ${CMAKE_CURRENT_BINARY_DIR}/../_deps/libjpeg-turbo-build
	    PARENT_SCOPE)
  else()
	  ExternalProject_Add(libjpeg-turbo
	                      CMAKE_ARGS
	                        -DENABLE_SHARED=OFF
	                        -DENABLE_STATIC=ON
	                        -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/libjpeg-turbo
	                        -DCMAKE_INSTALL_LIBDIR=${CMAKE_CURRENT_BINARY_DIR}/bin
	                        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
	                        -DWITH_TURBOJPEG=0
	  				      GIT_REPOSITORY https://github.com/libjpeg-turbo/libjpeg-turbo.git
	  					  GIT_TAG        2.1.5.1)
    set(JPEG_LIBNAME ${CMAKE_CURRENT_BINARY_DIR}/bin/${JPEG_LIBNAME} PARENT_SCOPE)
    set(JPEG_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/libjpeg-turbo/include/ PARENT_SCOPE)
endif()
else(GRK_BUILD_JPEG)
	find_package(JPEG)
	if(JPEG_FOUND)
		message(STATUS "The system seems to have libjpeg available, it will be used to support JPEG files")
		set(GROK_HAVE_LIBJPEG 1 PARENT_SCOPE)
		set(JPEG_LIBNAME ${JPEG_LIBRARIES} PARENT_SCOPE)
		set(JPEG_INCLUDE_DIRS ${JPEG_INCLUDE_DIR} PARENT_SCOPE)
	else(JPEG_FOUND)
	    set(GROK_HAVE_LIBJPEG 0 PARENT_SCOPE)
	    message(STATUS "libjpeg not found, please install or build from source and ensure install directory is on path")
	endif(JPEG_FOUND)
endif(GRK_BUILD_JPEG)


# Try to find libpng (which depends on zlib)
if(GRK_BUILD_LIBPNG)
  message(STATUS "libpng will be built")
  add_subdirectory(libpng)
  set(GROK_HAVE_LIBPNG 1 PARENT_SCOPE)
  set(PNG_LIBNAME png PARENT_SCOPE)
  set(PNG_INCLUDE_DIRNAME ${GROK_SOURCE_DIR}/thirdparty/libpng PARENT_SCOPE)
else(GRK_BUILD_LIBPNG)
  if(ZLIB_FOUND)
    find_package(PNG)
    # Static only build:
    #   it is not necessary to invoke pkg_check_module on libpng, because libpng
    #   only depends on zlib, which is already checked.
    if(PNG_FOUND)
      message(STATUS "The system seems to have a libpng available, it will be used to support PNG files")
      set(GROK_HAVE_LIBPNG 1 PARENT_SCOPE)
      set(PNG_LIBNAME ${PNG_LIBRARIES} PARENT_SCOPE)
      set(PNG_INCLUDE_DIRNAME ${PNG_PNG_INCLUDE_DIR} PARENT_SCOPE)
    else(PNG_FOUND)
         set(GROK_HAVE_LIBPNG 0 PARENT_SCOPE)
      message(STATUS "libpng not found, please activate GRK_BUILD_LIBPNG to support PNG files")
    endif(PNG_FOUND)
  endif(ZLIB_FOUND)
endif(GRK_BUILD_LIBPNG)

# Try to find lib TIFF
if(GRK_BUILD_LIBTIFF)
  # Try to build it
  message(STATUS "libtiff will be built to support TIFF files.")
  add_subdirectory(libtiff EXCLUDE_FROM_ALL)
  set(TIFF_LIBNAME tiff PARENT_SCOPE)
  set(TIFF_INCLUDE_DIRNAME ${GROK_SOURCE_DIR}/thirdparty/libtiff PARENT_SCOPE)
  set(GROK_HAVE_LIBTIFF 1 PARENT_SCOPE)
else(GRK_BUILD_LIBTIFF)
	if(PKG_CONFIG_FOUND)
	  foreach(pc_tiff_module tiff tiff3 tiff4 tiff-3 tiff-4 libtiff libtiff3 libtiff4 libtiff-3 libtiff-4)
	    pkg_check_modules(PC_TIFF QUIET ${pc_tiff_module})
	    if(PC_TIFF_FOUND)
	      break()
	    endif(PC_TIFF_FOUND)
	  endforeach()
	endif()
	if(TIFF_FOUND)
	  message(STATUS "The system seems to have a libtiff available, it will be used to support TIFF files.")
	  set(GROK_HAVE_LIBTIFF 1 PARENT_SCOPE)
	  # Check if it's a static build
	  if(NOT BUILD_SHARED_LIBS)
	    message(STATUS "Static build detected. Using PC_TIFF_STATIC_LIBRARIES and PC_TIFF_STATIC_INCLUDE_DIRS.")
	    set(TIFF_LIBNAME ${TIFF_LIBRARIES} ${PC_TIFF_STATIC_LIBRARIES} PARENT_SCOPE)
	    set(TIFF_INCLUDE_DIRNAME ${TIFF_INCLUDE_DIR} ${PC_TIFF_STATIC_INCLUDE_DIRS} PARENT_SCOPE)
	  else()
	    message(STATUS "Shared build detected. Using standard TIFF libraries and include directories.")
	    set(TIFF_LIBNAME ${TIFF_LIBRARIES} PARENT_SCOPE)
	    set(TIFF_INCLUDE_DIRNAME ${TIFF_INCLUDE_DIR} PARENT_SCOPE)
	  endif()
	else()
	  message(STATUS "libtiff not found. No TIFF support will be available.")
	  set(GROK_HAVE_LIBTIFF 0 PARENT_SCOPE)
	endif()
endif(GRK_BUILD_LIBTIFF)

if(GRK_BUILD_LCMS2)
  message(STATUS "We will build LCMS2 library")
  add_subdirectory(liblcms2)
  set(LCMS_LIBNAME lcms2 PARENT_SCOPE)
  set(LCMS_INCLUDE_DIRNAME ${GROK_SOURCE_DIR}/thirdparty/liblcms2/include PARENT_SCOPE)
else(GRK_BUILD_LCMS2)
  find_package(LCMS2 REQUIRED 2.15)
  # Static only build:
  #   it is necessary to invoke pkg_check_module on lcms2 since it may have
  #   several other dependencies not declared by its cmake module, but which are
  #   in its pkgconfig module.
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_LCMS2 QUIET lcms2)
  endif(PKG_CONFIG_FOUND)
  if(LCMS2_FOUND)
    message(STATUS "Your system seems to have a LCMS2 library available, we will use it")
    set(LCMS_LIBNAME ${LCMS2_LIBRARIES} ${PC_LCMS2_STATIC_LIBRARIES} PARENT_SCOPE)
    set(LCMS_INCLUDE_DIRNAME ${LCMS2_INCLUDE_DIRS} ${PC_LCMS2_STATIC_INCLUDE_DIRS} PARENT_SCOPE)
  else(LCMS2_FOUND)
      message(FATAL_ERROR "LCMS2 library with minimum version not found - please activate GRK_BUILD_LCMS2")
  endif(LCMS2_FOUND)
endif(GRK_BUILD_LCMS2)
