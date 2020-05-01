# Build the demo app, small examples

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

IF(MSVC)
    SET(CMAKE_CXX_FLAGS "/EHsc")
ENDIF(MSVC)

# First thing define the common source:
set(common_SRCS
  ${GROK_SOURCE_DIR}/src/bin/common/convert.cpp
  ${GROK_SOURCE_DIR}/src/bin/common/convert.h
  ${GROK_SOURCE_DIR}/src/bin/image_format/IImageFormat.h
  ${GROK_SOURCE_DIR}/src/bin/image_format/ImageFormat.cpp
  ${GROK_SOURCE_DIR}/src/bin/image_format/ImageFormat.h
  ${GROK_SOURCE_DIR}/src/bin/image_format/PNMFormat.cpp
  ${GROK_SOURCE_DIR}/src/bin/image_format/PNMFormat.h
  ${GROK_SOURCE_DIR}/src/bin/image_format/PGXFormat.cpp
  ${GROK_SOURCE_DIR}/src/bin/image_format/PGXFormat.h
  ${GROK_SOURCE_DIR}/src/bin/image_format/BMPFormat.cpp
  ${GROK_SOURCE_DIR}/src/bin/image_format/BMPFormat.h
  ${GROK_SOURCE_DIR}/src/bin/image_format/TGAFormat.cpp
  ${GROK_SOURCE_DIR}/src/bin/image_format/TGAFormat.h
  ${GROK_SOURCE_DIR}/src/bin/image_format/RAWFormat.cpp
  ${GROK_SOURCE_DIR}/src/bin/image_format/RAWFormat.h
  ${GROK_SOURCE_DIR}/src/bin/image_format/TIFFFormat.h
  ${GROK_SOURCE_DIR}/src/bin/image_format/PNGFormat.h
  ${GROK_SOURCE_DIR}/src/bin/image_format/JPEGFormat.h
  ${GROK_SOURCE_DIR}/src/bin/common/color.cpp
  ${GROK_SOURCE_DIR}/src/bin/common/color.h
  ${GROK_SOURCE_DIR}/src/bin/common/common.cpp
  ${GROK_SOURCE_DIR}/src/bin/common/common.h
  ${GROK_SOURCE_DIR}/src/bin/common/grok_string.h
  
  ${GROK_SOURCE_DIR}/src/bin/common/spdlog/spdlog.cpp
  ${GROK_SOURCE_DIR}/src/bin/common/spdlog/color_sinks.cpp
  ${GROK_SOURCE_DIR}/src/bin/common/spdlog/stdout_sinks.cpp
  ${GROK_SOURCE_DIR}/src/bin/common/spdlog/fmt.cpp
  ${GROK_SOURCE_DIR}/src/bin/common/spdlog/async.cpp     
  ${GROK_SOURCE_DIR}/src/bin/common/spdlog/file_sinks.cpp    
  )

if(GROK_HAVE_LIBTIFF)
	list(APPEND common_SRCS ../image_format/TIFFFormat.cpp)
endif()
if(GROK_HAVE_LIBPNG)
	list(APPEND common_SRCS ../image_format/PNGFormat.cpp)
endif()
if(GROK_HAVE_LIBJPEG)
	list(APPEND common_SRCS ../image_format/JPEGFormat.cpp)
	list(APPEND common_SRCS ../image_format/iccjpeg.c)
	list(APPEND common_SRCS ../image_format/iccjpeg.h)
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
  ${Z_INCLUDE_DIRNAME}
  ${PNG_INCLUDE_DIRNAME}
  ${TIFF_INCLUDE_DIRNAME}
  ${JPEG_INCLUDE_DIRNAME}
  ${GROK_SOURCE_DIR}/src/include
  )

if(WIN32)
  if(BUILD_SHARED_LIBS)
    add_definitions(-DGRK_EXPORTS)
  else()
    add_definitions(-DGRK_STATIC)
  endif()
endif()

add_definitions(-DSPDLOG_COMPILED_LIB)


# Loop over all executables:
foreach(exe grk_decompress grk_compress grk_dump)
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
    DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Applications
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
  FILES       ${GROK_SOURCE_DIR}/doc/man/man1/grk_compress.1
              ${GROK_SOURCE_DIR}/doc/man/man1/grk_decompress.1
              ${GROK_SOURCE_DIR}/doc/man/man1/grk_dump.1
  DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
#
endif()
