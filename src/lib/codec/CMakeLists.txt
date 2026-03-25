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
  ${CMAKE_CURRENT_BINARY_DIR}/../../bin # grk_apps_config.h
  ${CMAKE_CURRENT_BINARY_DIR}/../../lib/core # grk_config.h and grk_config_private.h
  ${GROK_SOURCE_DIR}/src/lib/core
  ${GROK_SOURCE_DIR}/src/include
  ${GROK_SOURCE_DIR}/src/include/CLI11/include
  ${CMAKE_CURRENT_SOURCE_DIR}/formats
  ${CMAKE_CURRENT_SOURCE_DIR}/formats/fileio
  ${CMAKE_CURRENT_SOURCE_DIR}/common
  ${CMAKE_CURRENT_SOURCE_DIR}/apps
)

set(GROK_CODEC_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/grok_codec.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/formats/BufferPool.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/formats/ImageFormat.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/formats/fileio/FileStandardIO.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/formats/fileio/FileOrchestratorIO.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/formats/YUVFormat.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/common.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/exif.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/spdlog/spdlog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/spdlog/color_sinks.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/spdlog/stdout_sinks.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/spdlog/async.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/spdlog/file_sinks.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/spdlog/bundled_fmtlib_format.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/spdlog/cfg.cpp  
  ${CMAKE_CURRENT_SOURCE_DIR}/apps/GrkCompress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/apps/GrkDecompress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/apps/GrkDump.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/apps/GrkCompareImages.cpp
)

if(GROK_HAVE_LIBTIFF)
	list(APPEND GROK_CODEC_SRCS formats/TIFFFormat.cpp)
endif()
if(GROK_HAVE_LIBPNG)
	list(APPEND GROK_CODEC_SRCS formats/PNGFormat.cpp)
	include_directories(${CMAKE_CURRENT_BINARY_DIR}/../../../thirdparty/libpng)
endif()
if(GROK_HAVE_LIBJPEG)
	list(APPEND GROK_CODEC_SRCS formats/iccjpeg.cpp)
endif()


add_definitions(-DSPDLOG_COMPILED_LIB)

# Generate help text headers from markdown man pages
find_program(GRK_PYTHON3 NAMES python3 python)
if(GRK_PYTHON3)
  set(GEN_HELP_SCRIPT ${GROK_SOURCE_DIR}/scripts/gen_help_text.py)
  set(GEN_HELP_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

  add_custom_command(
    OUTPUT ${GEN_HELP_OUTPUT_DIR}/compress_help.h
    COMMAND ${GRK_PYTHON3} ${GEN_HELP_SCRIPT}
      ${GROK_SOURCE_DIR}/doc/man/md/man1/grk_compress.1.md
      ${GEN_HELP_OUTPUT_DIR}/compress_help.h
      compress_help_text
    DEPENDS ${GROK_SOURCE_DIR}/doc/man/md/man1/grk_compress.1.md ${GEN_HELP_SCRIPT}
    COMMENT "Generating compress help text from markdown"
  )

  add_custom_command(
    OUTPUT ${GEN_HELP_OUTPUT_DIR}/decompress_help.h
    COMMAND ${GRK_PYTHON3} ${GEN_HELP_SCRIPT}
      ${GROK_SOURCE_DIR}/doc/man/md/man1/grk_decompress.1.md
      ${GEN_HELP_OUTPUT_DIR}/decompress_help.h
      decompress_help_text
    DEPENDS ${GROK_SOURCE_DIR}/doc/man/md/man1/grk_decompress.1.md ${GEN_HELP_SCRIPT}
    COMMENT "Generating decompress help text from markdown"
  )

  add_custom_target(generate_help_headers
    DEPENDS
      ${GEN_HELP_OUTPUT_DIR}/compress_help.h
      ${GEN_HELP_OUTPUT_DIR}/decompress_help.h
  )
else()
  message(WARNING "Python3 not found - help text headers will not be regenerated from markdown")
endif()

add_library(${GROK_CODEC_NAME} ${GROK_CODEC_SRCS})
set(INSTALL_LIBS ${GROK_CODEC_NAME})

if(GRK_PYTHON3)
  add_dependencies(${GROK_CODEC_NAME} generate_help_headers)
  target_include_directories(${GROK_CODEC_NAME} PRIVATE ${GEN_HELP_OUTPUT_DIR})
endif()

target_link_libraries(${GROK_CODEC_NAME} PRIVATE ${GROK_CORE_NAME})
if(GROK_HAVE_LIBPNG)
target_link_libraries(${GROK_CODEC_NAME} PRIVATE ${PNG_LIBNAME})
endif()
if(GROK_HAVE_LIBTIFF)
	target_link_libraries(${GROK_CODEC_NAME} PRIVATE ${TIFF_LIBNAME})
endif()
if(GROK_HAVE_LIBJPEG)
	target_link_libraries(${GROK_CODEC_NAME} PRIVATE ${JPEG_LIBNAME})
endif()

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
