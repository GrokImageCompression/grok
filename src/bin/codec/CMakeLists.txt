include_directories(
  ${CMAKE_CURRENT_BINARY_DIR}/../../lib/core
  ${CMAKE_CURRENT_BINARY_DIR}/../common
  ${GROK_SOURCE_DIR}/src/lib/core
  ${GROK_SOURCE_DIR}/src/lib/codec
  )
foreach(exe grk_decompress grk_compress grk_dump)
  add_executable(${exe} ${exe}.cpp)
  target_compile_options(${exe} PRIVATE ${GROK_COMPILE_OPTIONS})
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	target_link_options(${exe}  PUBLIC "LINKER:-z,now")
  endif()

  target_link_libraries(${exe} ${GROK_CODEC_NAME})
  if (GRK_BUILD_PLUGIN_LOADER AND UNIX)
  	target_link_libraries(${exe} ${CMAKE_DL_LIBS} )
  endif(GRK_BUILD_PLUGIN_LOADER AND UNIX)

  if (LTK_USE_OPENCL)
  	target_link_libraries(${exe} ${OPENCL_LIBRARIES} )
  endif(LTK_USE_OPENCL)

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
