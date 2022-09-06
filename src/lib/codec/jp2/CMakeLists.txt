# First thing define the common source:
if(GROK_HAVE_LIBTIFF)
	list(APPEND common_SRCS ../image_format/TIFFFormat.cpp)
endif()
if(GROK_HAVE_LIBPNG)
	list(APPEND common_SRCS ../image_format/PNGFormat.cpp)
endif()
if(GROK_HAVE_LIBJPEG)
	list(APPEND common_SRCS ../image_format/JPEGFormat.cpp)
	list(APPEND common_SRCS ../image_format/iccjpeg.cpp)
	list(APPEND common_SRCS ../image_format/iccjpeg.h)
endif()
if(GROK_HAVE_URING)
	list(APPEND common_SRCS ../image_format/FileUringIO.cpp)
endif()

# Headers file are located here:
include_directories(
  ${GROK_BINARY_DIR}/src/lib/core
  ${GROK_BINARY_DIR}/src/bin/common
  ${GROK_SOURCE_DIR}/src/lib/core
  ${GROK_SOURCE_DIR}/src/bin/common
  ${GROK_SOURCE_DIR}/src/bin/image_format
  ${GROK_SOURCE_DIR}/src/bin/codec
  ${LCMS_INCLUDE_DIRNAME}
  ${PNG_INCLUDE_DIRNAME}
  ${TIFF_INCLUDE_DIRNAME}
  ${JPEG_INCLUDE_DIRNAME}
  ${GROK_SOURCE_DIR}/src/include
  )

if(WIN32)
  add_definitions(-DNOMINMAX)
  if(BUILD_SHARED_LIBS)
    add_definitions(-DGRK_EXPORTS)
  endif()
endif()

add_definitions(-DSPDLOG_COMPILED_LIB)


