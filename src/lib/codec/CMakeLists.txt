set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

IF(MSVC)
    string(APPEND CMAKE_CXX_FLAGS " /EHsc")
ENDIF(MSVC)

IF(UNIX)
IF(BUILD_SHARED_LIBS)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         string(APPEND CMAKE_CXX_FLAGS " -fvisibility=hidden")
    ENDIF()
ENDIF()
ENDIF(UNIX)


include_directories(
  ${LCMS_INCLUDE_DIRNAME}
  ${PNG_INCLUDE_DIRNAME}
  ${TIFF_INCLUDE_DIRNAME}
  ${JPEG_INCLUDE_DIRNAME}
  ${GROK_BINARY_DIR}/src/bin
  ${GROK_BINARY_DIR}/src/lib/jp2 # grk_config.h and grk_config_private.h
  ${GROK_SOURCE_DIR}/src/lib/jp2
  ${GROK_SOURCE_DIR}/src/include
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/image_format
  ${CMAKE_CURRENT_SOURCE_DIR}/common
  ${CMAKE_CURRENT_SOURCE_DIR}/jp2
)

set(GROK_CODEC_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/grok_codec.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/convert.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/convert.h
  ${CMAKE_CURRENT_SOURCE_DIR}/image_format/Serializer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/image_format/MemManager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/image_format/BufferPool.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/image_format/IImageFormat.h
  ${CMAKE_CURRENT_SOURCE_DIR}/image_format/ImageFormat.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/image_format/FileStreamIO.h
  ${CMAKE_CURRENT_SOURCE_DIR}/image_format/FileStreamIO.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/image_format/ImageFormat.h
  ${CMAKE_CURRENT_SOURCE_DIR}/image_format/PNMFormat.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/image_format/PNMFormat.h
  ${CMAKE_CURRENT_SOURCE_DIR}/image_format/PGXFormat.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/image_format/PGXFormat.h
  ${CMAKE_CURRENT_SOURCE_DIR}/image_format/BMPFormat.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/image_format/BMPFormat.h
  ${CMAKE_CURRENT_SOURCE_DIR}/image_format/RAWFormat.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/image_format/RAWFormat.h
  ${CMAKE_CURRENT_SOURCE_DIR}/image_format/TIFFFormat.h
  ${CMAKE_CURRENT_SOURCE_DIR}/image_format/PNGFormat.h
  ${CMAKE_CURRENT_SOURCE_DIR}/image_format/JPEGFormat.h
  ${CMAKE_CURRENT_SOURCE_DIR}/common/common.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/codec_common.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/common.h
  ${CMAKE_CURRENT_SOURCE_DIR}/common/grk_string.h
  ${CMAKE_CURRENT_SOURCE_DIR}/common/exif.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/spdlog/spdlog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/spdlog/color_sinks.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/spdlog/stdout_sinks.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/spdlog/fmt.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/spdlog/async.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/spdlog/file_sinks.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/jp2/GrkCompress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/jp2/GrkDecompress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/jp2/GrkDump.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/jp2/GrkTestTileEncoder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/jp2/GrkTestTileDecoder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/jp2/GrkCompareImages.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/jp2/GrkRandomTileAccess.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/jp2/GrkCompareDumpFiles.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/jp2/GrkCompareRawFiles.cpp
)


if(GROK_HAVE_LIBTIFF)
	list(APPEND GROK_CODEC_SRCS image_format/TIFFFormat.cpp)
endif()
if(GROK_HAVE_LIBPNG)
	list(APPEND GROK_CODEC_SRCS image_format/PNGFormat.cpp)
endif()
if(GROK_HAVE_LIBJPEG)
	list(APPEND GROK_CODEC_SRCS image_format/JPEGFormat.cpp)
	list(APPEND GROK_CODEC_SRCS image_format/iccjpeg.cpp)
	list(APPEND GROK_CODEC_SRCS image_format/iccjpeg.h)
endif()
if(GROK_HAVE_URING)
	list(APPEND GROK_CODEC_SRCS image_format/FileUringIO.cpp)
endif()

add_definitions(-DSPDLOG_COMPILED_LIB)

# Build the library
if (GRK_BUILD_PLUGIN_LOADER)
    add_definitions(-DGRK_BUILD_PLUGIN_LOADER)
endif()
if(WIN32)
  if(BUILD_SHARED_LIBS)
    add_definitions(-DGRK_EXPORTS)
  else()
    add_definitions(-DGRK_STATIC)
  endif()
endif()
add_library(${GROK_CODEC_NAME} ${GROK_CODEC_SRCS})
set(INSTALL_LIBS ${GROK_CODEC_NAME})
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	target_link_options(${GROK_CODEC_NAME} PUBLIC "LINKER:-z,now")
endif()
target_link_libraries(${GROK_CODEC_NAME} PRIVATE ${GROK_LIBRARY_NAME} hwy lcms2 ${Z_LIBNAME} ${PNG_LIBNAME} ${TIFF_LIBNAME} ${LCMS_LIBNAME}
    ${JPEG_LIBNAME})
if(UNIX)
  target_link_libraries(${GROK_CODEC_NAME} PUBLIC ${CMAKE_THREAD_LIBS_INIT})
endif()
set_target_properties(${GROK_CODEC_NAME} PROPERTIES ${GROK_LIBRARY_PROPERTIES})
target_compile_options(${GROK_CODEC_NAME} PRIVATE ${GROK_COMPILE_OPTIONS})

# Install library
if(BUILD_SHARED_LIBS)
install(TARGETS ${INSTALL_LIBS}
  EXPORT GrokTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Applications
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Libraries
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Libraries
)
endif()

# Install includes files
install(FILES grok_codec.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${GROK_INSTALL_SUBDIR} COMPONENT Headers
)
