set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

IF(MSVC)
    string(APPEND CMAKE_CXX_FLAGS " /EHsc")
ENDIF(MSVC)

# First thing define the common source:
set(common_SRCS
  ${GROK_SOURCE_DIR}/src/bin/common/convert.cpp
  ${GROK_SOURCE_DIR}/src/bin/common/convert.h
  ${GROK_SOURCE_DIR}/src/bin/image_format/Serializer.cpp
  ${GROK_SOURCE_DIR}/src/bin/image_format/MemManager.cpp
  ${GROK_SOURCE_DIR}/src/bin/image_format/BufferPool.cpp
  ${GROK_SOURCE_DIR}/src/bin/image_format/IImageFormat.h
  ${GROK_SOURCE_DIR}/src/bin/image_format/ImageFormat.cpp
  ${GROK_SOURCE_DIR}/src/bin/image_format/FileStreamIO.h
  ${GROK_SOURCE_DIR}/src/bin/image_format/FileStreamIO.cpp
  ${GROK_SOURCE_DIR}/src/bin/image_format/ImageFormat.h
  ${GROK_SOURCE_DIR}/src/bin/image_format/PNMFormat.cpp
  ${GROK_SOURCE_DIR}/src/bin/image_format/PNMFormat.h
  ${GROK_SOURCE_DIR}/src/bin/image_format/PGXFormat.cpp
  ${GROK_SOURCE_DIR}/src/bin/image_format/PGXFormat.h
  ${GROK_SOURCE_DIR}/src/bin/image_format/BMPFormat.cpp
  ${GROK_SOURCE_DIR}/src/bin/image_format/BMPFormat.h
  ${GROK_SOURCE_DIR}/src/bin/image_format/RAWFormat.cpp
  ${GROK_SOURCE_DIR}/src/bin/image_format/RAWFormat.h
  ${GROK_SOURCE_DIR}/src/bin/image_format/TIFFFormat.h
  ${GROK_SOURCE_DIR}/src/bin/image_format/PNGFormat.h
  ${GROK_SOURCE_DIR}/src/bin/image_format/JPEGFormat.h
  ${GROK_SOURCE_DIR}/src/bin/common/common.cpp
  ${GROK_SOURCE_DIR}/src/bin/common/codec_common.cpp
  ${GROK_SOURCE_DIR}/src/bin/common/common.h
  ${GROK_SOURCE_DIR}/src/bin/common/grk_string.h
  ${GROK_SOURCE_DIR}/src/bin/common/exif.cpp
  ${GROK_SOURCE_DIR}/src/bin/common/spdlog/spdlog.cpp
  ${GROK_SOURCE_DIR}/src/bin/common/spdlog/color_sinks.cpp
  ${GROK_SOURCE_DIR}/src/bin/common/spdlog/stdout_sinks.cpp
  ${GROK_SOURCE_DIR}/src/bin/common/spdlog/fmt.cpp
  ${GROK_SOURCE_DIR}/src/bin/common/spdlog/async.cpp     
  ${GROK_SOURCE_DIR}/src/bin/common/spdlog/file_sinks.cpp
  ${GROK_SOURCE_DIR}/src/bin/io/FileIO.cpp
  ${GROK_SOURCE_DIR}/src/bin/io/FileIOUnix.cpp
  ${GROK_SOURCE_DIR}/src/bin/io/FileIOWin32.cpp
  ${GROK_SOURCE_DIR}/src/bin/io/FileIOUring.cpp
  ${GROK_SOURCE_DIR}/src/bin/io/Serializer.cpp
  ${GROK_SOURCE_DIR}/src/bin/io/ImageFormat.cpp
  ${GROK_SOURCE_DIR}/src/bin/io/TIFFFormat.cpp
  ${GROK_SOURCE_DIR}/src/bin/jp2/GrkCompress.cpp
  ${GROK_SOURCE_DIR}/src/bin/jp2/GrkDecompress.cpp
  ${GROK_SOURCE_DIR}/src/bin/jp2/GrkDump.cpp
 )

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
  ${GROK_BINARY_DIR}/src/lib/jp2
  ${GROK_BINARY_DIR}/src/bin/common
  ${GROK_SOURCE_DIR}/src/lib/jp2
  ${GROK_SOURCE_DIR}/src/bin/common
  ${GROK_SOURCE_DIR}/src/bin/image_format
  ${GROK_SOURCE_DIR}/src/bin/jp2
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
  else()
    add_definitions(-DGRK_STATIC)
  endif()
endif()

add_definitions(-DSPDLOG_COMPILED_LIB)


