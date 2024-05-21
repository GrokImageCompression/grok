IF(UNIX)
IF(BUILD_SHARED_LIBS)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         string(APPEND CMAKE_CXX_FLAGS " -fvisibility=hidden")
    ENDIF()
ENDIF()
ENDIF(UNIX)

include_directories(
  ${PNG_INCLUDE_DIRNAME}
  ${TIFF_INCLUDE_DIRNAME}
  ${JPEG_INCLUDE_DIRS}
  ${CMAKE_CURRENT_BINARY_DIR}/../../bin
  ${CMAKE_CURRENT_BINARY_DIR}/../../lib/core # grk_config.h and grk_config_private.h
  ${GROK_SOURCE_DIR}/src/lib/core
  ${GROK_SOURCE_DIR}/src/include
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
  ${CMAKE_CURRENT_SOURCE_DIR}/common/common.h
  ${CMAKE_CURRENT_SOURCE_DIR}/common/grk_string.h
  ${CMAKE_CURRENT_SOURCE_DIR}/common/exif.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/spdlog/spdlog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/spdlog/color_sinks.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/spdlog/stdout_sinks.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/spdlog/async.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/spdlog/file_sinks.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/spdlog/bundled_fmtlib_format.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/spdlog/cfg.cpp  
  ${CMAKE_CURRENT_SOURCE_DIR}/jp2/GrkCompress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/jp2/GrkDecompress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/jp2/GrkDump.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/jp2/GrkCompareImages.cpp
)

if(GROK_HAVE_LIBTIFF)
	list(APPEND GROK_CODEC_SRCS image_format/TIFFFormat.cpp)
endif()
if(GROK_HAVE_LIBPNG)
	list(APPEND GROK_CODEC_SRCS image_format/PNGFormat.cpp)
	include_directories(${CMAKE_CURRENT_BINARY_DIR}/../../../thirdparty/libpng)
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

add_library(${GROK_CODEC_NAME} ${GROK_CODEC_SRCS})
set(INSTALL_LIBS ${GROK_CODEC_NAME})

target_link_libraries(${GROK_CODEC_NAME} PRIVATE ${GROK_CORE_NAME}
                       ${PNG_LIBNAME} ${TIFF_LIBNAME}
                       ${JPEG_LIBNAME})

if (PERLLIBS_FOUND)
   include_directories(${PERL_INCLUDE_PATH})
   target_link_libraries(${GROK_CODEC_NAME} PUBLIC ${PERL_LIBRARY} )
endif()

if(UNIX)
  target_link_libraries(${GROK_CODEC_NAME} PUBLIC ${CMAKE_THREAD_LIBS_INIT})
endif()
set_target_properties(${GROK_CODEC_NAME} PROPERTIES ${GROK_LIBRARY_PROPERTIES})
target_compile_options(${GROK_CODEC_NAME} PRIVATE ${GROK_COMPILE_OPTIONS})

# hardening
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	target_link_options(${GROK_CODEC_NAME} PRIVATE "LINKER:-z,now")
endif()

# mips needs explicit linker flag to disable executable stack
if (GRK_ARCH MATCHES "mips")
  target_link_options(${GROK_CORE_NAME} PRIVATE "LINKER:-z,noexecstack")
endif (GRK_ARCH MATCHES "mips")

# Install library
install(TARGETS ${INSTALL_LIBS}
  EXPORT GrokTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Applications
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Libraries
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Libraries
)

# Install includes files
install(FILES grok_codec.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${GROK_INSTALL_SUBDIR} COMPONENT Headers
)
