set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

IF(MSVC)
    string(APPEND CMAKE_CXX_FLAGS " /EHsc")
ENDIF(MSVC)


# Headers file are located here:
include_directories(
  ${GROK_BINARY_DIR}/src/lib/jp2
  ${GROK_BINARY_DIR}/src/bin/common
  ${GROK_SOURCE_DIR}/src/lib/jp2
  ${GROK_SOURCE_DIR}/src/lib/codec
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
  ${CMAKE_CURRENT_SOURCE_DIR}/../image_format
  ${CMAKE_CURRENT_SOURCE_DIR}/../jp2
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


# Loop over all executables:
foreach(exe grk_decompress grk_compress grk_dump)
  add_executable(${exe} ${exe}.cpp ${common_SRCS})
  target_compile_options(${exe} PRIVATE ${GROK_COMPILE_OPTIONS})
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	target_link_options(${exe}  PUBLIC "LINKER:-z,now")
  endif()

  target_link_libraries(${exe} 
    ${PNG_LIBNAME} ${TIFF_LIBNAME} ${LCMS_LIBNAME}
    ${JPEG_LIBNAME} ${CMAKE_THREAD_LIBS_INIT} )
    target_link_libraries(${exe} ${GROK_LIBRARY_NAME} ${GROK_CODEC_NAME} ${CMAKE_DL_LIBS} )

  # To support universal exe:
  if(ZLIB_FOUND AND APPLE)
    target_link_libraries(${exe} z)
  else(ZLIB_FOUND AND APPLE)
    target_link_libraries(${exe} ${Z_LIBNAME})
  endif()
  
  # liburing
  if(URING)
	  target_link_libraries(${exe} uring)
  endif()
  
  if (PERLLIBS_FOUND)
  	include_directories(${PERL_INCLUDE_PATH})
   	target_link_libraries(${exe} ${PERL_LIBRARY} )
  endif()
  
  # Install exe
  install(TARGETS ${exe}
    EXPORT GrokTargets
    DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Applications
  )
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
