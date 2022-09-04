set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

IF(MSVC)
    string(APPEND CMAKE_CXX_FLAGS " /EHsc")
ENDIF(MSVC)

include_directories(
  ${GROK_BINARY_DIR}/src/lib/core
  ${GROK_BINARY_DIR}/src/bin/common
  ${GROK_SOURCE_DIR}/src/lib/core
  ${GROK_SOURCE_DIR}/src/lib/codec
  )

if(WIN32)
  add_definitions(-DNOMINMAX)
  if(BUILD_SHARED_LIBS)
    add_definitions(-DGRK_EXPORTS)
  else()
    add_definitions(-DGRK_STATIC)
  endif()
endif()

foreach(exe grk_decompress grk_compress grk_dump)
  add_executable(${exe} ${exe}.cpp ${common_SRCS})
  target_compile_options(${exe} PRIVATE ${GROK_COMPILE_OPTIONS})
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	target_link_options(${exe}  PUBLIC "LINKER:-z,now")
  endif()

  target_link_libraries(${exe} 
    ${PNG_LIBNAME} ${TIFF_LIBNAME} ${LCMS_LIBNAME}
    ${JPEG_LIBNAME} ${CMAKE_THREAD_LIBS_INIT} 
    ${GROK_CORE_NAME} ${GROK_CODEC_NAME} )

  if (GRK_BUILD_PLUGIN_LOADER)
    target_link_libraries(${exe} ${CMAKE_DL_LIBS} )
  endif(GRK_BUILD_PLUGIN_LOADER)

  if(URING)
	  target_link_libraries(${exe} uring)
  endif(URING)

  if (PERLLIBS_FOUND)
  	include_directories(${PERL_INCLUDE_PATH})
   	target_link_libraries(${exe} ${PERL_LIBRARY} )
  endif()
 
  install(TARGETS ${exe}
    EXPORT GrokTargets
    DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Applications
  )
endforeach()

if(GRK_BUILD_DOC)
install(
  FILES       ${GROK_SOURCE_DIR}/doc/man/man1/grk_compress.1
              ${GROK_SOURCE_DIR}/doc/man/man1/grk_decompress.1
              ${GROK_SOURCE_DIR}/doc/man/man1/grk_dump.1
  DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endif()
