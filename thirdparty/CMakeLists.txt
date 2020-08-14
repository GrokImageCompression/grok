# Third Party Libraries

if(NOT BUILD_THIRDPARTY)
  include(FindPkgConfig)
endif(NOT BUILD_THIRDPARTY)

#------------
# Try to find lib Z
if(BUILD_THIRDPARTY)
  message(STATUS "zlib will be built.")
  add_subdirectory(libz)
  set(Z_LIBNAME z PARENT_SCOPE)
  set(Z_INCLUDE_DIRNAME ${GROK_SOURCE_DIR}/thirdparty/include PARENT_SCOPE)
  set(ZLIB_FOUND 1)
else(BUILD_THIRDPARTY)
  find_package(ZLIB)
  if(ZLIB_FOUND)
    set(Z_LIBNAME ${ZLIB_LIBRARIES} PARENT_SCOPE)
    set(Z_INCLUDE_DIRNAME ${ZLIB_INCLUDE_DIRS} PARENT_SCOPE)
    message(STATUS "The system seems to have a zlib available; it will be used to build libpng")
    # message(STATUS "DEBUG: ${ZLIB_INCLUDE_DIRS} vs ${ZLIB_INCLUDE_DIR}")
  else(ZLIB_FOUND) 
    message(STATUS "zlib not found, please activate BUILD_THIRDPARTY to build it (required by libpng)")
  endif(ZLIB_FOUND)
endif(BUILD_THIRDPARTY)


#------------
# Try to find libpng (which depends on zlib)
if(BUILD_THIRDPARTY)
  message(STATUS "libpng will be built")
  add_subdirectory(libpng)
  set(GROK_HAVE_LIBPNG 1 PARENT_SCOPE)
  set(PNG_LIBNAME png PARENT_SCOPE)
  set(PNG_INCLUDE_DIRNAME ${GROK_SOURCE_DIR}/thirdparty/libpng PARENT_SCOPE)
else(BUILD_THIRDPARTY)
  if(ZLIB_FOUND)
    find_package(PNG)
    # Static only build:
    #   it is not necessary to invoke pkg_check_module on libpng, because libpng
    #   only depends on zlib, which is already checked.
    if(PNG_FOUND)
      message(STATUS "The system seems to have a libpnb available, it will be used to support PNG files")
      set(GROK_HAVE_LIBPNG 1 PARENT_SCOPE)
      set(PNG_LIBNAME ${PNG_LIBRARIES} PARENT_SCOPE)
      set(PNG_INCLUDE_DIRNAME ${PNG_PNG_INCLUDE_DIR} PARENT_SCOPE)
    else(PNG_FOUND)
         set(GROK_HAVE_LIBPNG 0 PARENT_SCOPE)
      message(STATUS "libpng not found, please activate BUILD_THIRDPARTY to support PNG files")
    endif(PNG_FOUND)
  endif(ZLIB_FOUND)
endif(BUILD_THIRDPARTY)

#------------
# Try to find lib TIFF

if(BUILD_THIRDPARTY)
  # Try to build it
  message(STATUS "libtiff will be built to support TIFF files.")
  add_subdirectory(libtiff)
  set(TIFF_LIBNAME tiff PARENT_SCOPE)
  set(TIFF_INCLUDE_DIRNAME
    ${GROK_SOURCE_DIR}/thirdparty/libtiff
    ${GROK_BINARY_DIR}/thirdparty/libtiff
    PARENT_SCOPE)
  set(GROK_HAVE_LIBTIFF 1 PARENT_SCOPE)
else(BUILD_THIRDPARTY)
  find_package(TIFF)
  # Static only build:
  #   it is necessary to invoke pkg_check_module on libtiff since it may have
  #   several other dependencies not declared by its cmake module, but they are
  #   in its pkgconfig module.
  if(PKG_CONFIG_FOUND)
    foreach(pc_tiff_module tiff tiff3 tiff4 tiff-3 tiff-4 libtiff libtiff3 libtiff4 libtiff-3 libtiff-4)
      pkg_check_modules(PC_TIFF QUIET ${pc_tiff_module})
      if(PC_TIFF_FOUND)
        break()
      endif(PC_TIFF_FOUND)
    endforeach()
  endif(PKG_CONFIG_FOUND)
  if(TIFF_FOUND)
    message(STATUS "The system seems to have a libtiff available, it will be used to support TIFF files.")
    set(GROK_HAVE_LIBTIFF 1 PARENT_SCOPE)
    set(TIFF_LIBNAME ${TIFF_LIBRARIES} ${PC_TIFF_STATIC_LIBRARIES} PARENT_SCOPE)
    set(TIFF_INCLUDE_DIRNAME ${TIFF_INCLUDE_DIR} ${PC_TIFF_STATIC_INCLUDE_DIRS} PARENT_SCOPE)
  else(TIFF_FOUND)
    set(GROK_HAVE_LIBTIFF 0 PARENT_SCOPE)
    message(STATUS "libtiff not found, please activate BUILD_THIRDPARTY to support TIFF files.")
  endif(TIFF_FOUND)
endif(BUILD_THIRDPARTY)

#------------
# Try to find lib LCMS2

if( BUILD_THIRDPARTY)
  message(STATUS "We will build LCMS2 lib from thirdparty")
  add_subdirectory(liblcms2)
  set(LCMS_LIBNAME lcms2 PARENT_SCOPE)
  set(LCMS_INCLUDE_DIRNAME ${GROK_SOURCE_DIR}/thirdparty/liblcms2/include PARENT_SCOPE) #
  set(GROK_HAVE_LIBLCMS 1 PARENT_SCOPE)
else(BUILD_THIRDPARTY)
  find_package(LCMS2)
  if(LCMS2_FOUND)
    message(STATUS "The system seems to have a liblsmc2 available, it will be used to support ICC profiles.")
    set(GROK_HAVE_LIBLCMS 1 PARENT_SCOPE)
    set(LCMS_LIBNAME ${LCMS2_LIBRARIES} PARENT_SCOPE)
    set(LCMS_INCLUDE_DIRNAME ${LCMS2_INCLUDE_DIRS} PARENT_SCOPE)
  else(LCMS2_FOUND) 
      set(GROK_HAVE_LIBLCMS 0 PARENT_SCOPE)
      message(STATUS "liblcms2 not found, please activate BUILD_THIRDPARTY to support ICC profiles")
  endif(LCMS2_FOUND)
endif(BUILD_THIRDPARTY)


# JPEG library
if (GRK_USE_LIBJPEG)
	find_package(JPEG)
	if(JPEG_FOUND)
		message(STATUS "The system seems to have libjpeg available, it will be used to support JPEG files")
		set(GROK_HAVE_LIBJPEG 1 PARENT_SCOPE)
		set(JPEG_LIBNAME ${JPEG_LIBRARIES} PARENT_SCOPE)
		set(JPEG_INCLUDE_DIRNAME ${JPEG_INCLUDE_DIR} PARENT_SCOPE)
	else(JPEG_FOUND)
	    set(GROK_HAVE_LIBJPEG 0 PARENT_SCOPE)
	    message(STATUS "libjpeg not found, please install or build from source and ensure install directory is on path")
	endif(JPEG_FOUND)
endif(GRK_USE_LIBJPEG)