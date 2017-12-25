# Build the demo app, small examples

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

IF(MSVC)
    SET(CMAKE_CXX_FLAGS "/EHsc")
ENDIF(MSVC)

# First thing define the common source:
set(common_SRCS
  convert.cpp
  convert.h
  ${GROK_SOURCE_DIR}/src/bin/jp2/IImageFormat.h
  ${GROK_SOURCE_DIR}/src/bin/jp2/ImageFormat.cpp
  ${GROK_SOURCE_DIR}/src/bin/jp2/ImageFormat.h
  ${GROK_SOURCE_DIR}/src/bin/jp2/PNMFormat.cpp
  ${GROK_SOURCE_DIR}/src/bin/jp2/PNMFormat.h
  ${GROK_SOURCE_DIR}/src/bin/jp2/PGXFormat.cpp
  ${GROK_SOURCE_DIR}/src/bin/jp2/PGXFormat.h
  ${GROK_SOURCE_DIR}/src/bin/jp2/BMPFormat.cpp
  ${GROK_SOURCE_DIR}/src/bin/jp2/BMPFormat.h
  ${GROK_SOURCE_DIR}/src/bin/jp2/TGAFormat.cpp
  ${GROK_SOURCE_DIR}/src/bin/jp2/TGAFormat.h
  ${GROK_SOURCE_DIR}/src/bin/jp2/RAWFormat.cpp
  ${GROK_SOURCE_DIR}/src/bin/jp2/RAWFormat.h
  ${GROK_SOURCE_DIR}/src/bin/jp2/TIFFFormat.h
  ${GROK_SOURCE_DIR}/src/bin/jp2/PNGFormat.h
  ${GROK_SOURCE_DIR}/src/bin/jp2/JPEGFormat.h
  ${GROK_SOURCE_DIR}/src/bin/common/color.cpp
  ${GROK_SOURCE_DIR}/src/bin/common/color.h
  ${GROK_SOURCE_DIR}/src/bin/common/common.cpp
  ${GROK_SOURCE_DIR}/src/bin/common/common.h
  ${GROK_SOURCE_DIR}/src/bin/common/grok_getopt.cpp
  ${GROK_SOURCE_DIR}/src/bin/common/grok_getopt.h
  ${GROK_SOURCE_DIR}/src/bin/common/grok_string.h
  )

if(GROK_HAVE_LIBTIFF)
	list(APPEND common_SRCS TIFFFormat.cpp)
endif()
if(GROK_HAVE_LIBPNG)
	list(APPEND common_SRCS PNGFormat.cpp)
endif()
if(GROK_HAVE_LIBJPEG)
	list(APPEND common_SRCS JPEGFormat.cpp)
endif()

# Headers file are located here:
include_directories(
  ${GROK_BINARY_DIR}/src/lib/openjp2 
  ${GROK_BINARY_DIR}/src/bin/common 
  ${GROK_SOURCE_DIR}/src/lib/openjp2
  ${GROK_SOURCE_DIR}/src/bin/common
  ${LCMS_INCLUDE_DIRNAME}
  ${Z_INCLUDE_DIRNAME}
  ${PNG_INCLUDE_DIRNAME}
  ${TIFF_INCLUDE_DIRNAME}
  ${JPEG_INCLUDE_DIRNAME}
  )

if(WIN32)
  if(BUILD_SHARED_LIBS)
    add_definitions(-DOPJ_EXPORTS)
  else()
    add_definitions(-DOPJ_STATIC)
  endif()
endif()

# Loop over all executables:
foreach(exe opj_decompress opj_compress opj_dump)
  add_executable(${exe} ${exe}.cpp ${common_SRCS})
  if(${CMAKE_VERSION} VERSION_GREATER "2.8.11")
    target_compile_options(${exe} PRIVATE ${GROK_COMPILE_OPTIONS})
  endif()
  target_link_libraries(${exe} ${GROK_LIBRARY_NAME}
    ${PNG_LIBNAME} ${TIFF_LIBNAME} ${LCMS_LIBNAME}  ${JPEG_LIBNAME} ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT}
    )

	

  # To support universal exe:
  if(ZLIB_FOUND AND APPLE)
    target_link_libraries(${exe} z)
  else(ZLIB_FOUND AND APPLE)
    target_link_libraries(${exe} ${Z_LIBNAME})
  endif()

  # Install exe
  install(TARGETS ${exe}
    EXPORT GrokTargets
    DESTINATION ${GROK_INSTALL_BIN_DIR} COMPONENT Applications
  )
  if(GROK_USE_DSYMUTIL)
    add_custom_command(TARGET ${exe} POST_BUILD
    COMMAND "dsymutil" "$<TARGET_FILE:${exe}>"
    COMMENT "dsymutil $<TARGET_FILE:${exe}>"
    DEPENDS ${exe})
  endif()
endforeach()

if(BUILD_DOC)
# Install man pages
install(
  FILES       ${GROK_SOURCE_DIR}/doc/man/man1/opj_compress.1
              ${GROK_SOURCE_DIR}/doc/man/man1/opj_decompress.1
              ${GROK_SOURCE_DIR}/doc/man/man1/opj_dump.1
  DESTINATION ${GROK_INSTALL_MAN_DIR}/man1)
#
endif()
