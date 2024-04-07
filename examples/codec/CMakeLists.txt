include_directories(
  ${GROK_SOURCE_DIR}/src/lib/codec
  ${CMAKE_CURRENT_BINARY_DIR}/../../src/lib/core
  ${GROK_SOURCE_DIR}/src/lib/core
  ${CMAKE_CURRENT_BINARY_DIR}/../
)

foreach(exe codec_decompress_from_file
			codec_compress
)
  add_executable(${exe} ${exe}.cpp)
  target_compile_options(${exe} PRIVATE ${GROK_COMPILE_OPTIONS})
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	target_link_options(${exe}  PUBLIC "LINKER:-z,now")
  endif()

  # set emcc options
  if (CMAKE_SYSTEM_NAME STREQUAL Emscripten)
    target_link_options(${exe} PUBLIC -sASSERTIONS=1 -sPROXY_TO_PTHREAD -sTOTAL_MEMORY=1536MB -sEXIT_RUNTIME=1 -sUSE_PTHREADS=1)
    target_compile_options(${exe} PUBLIC -matomics)
  endif()

  target_link_libraries(${exe} ${GROK_CODEC_NAME} ${GROK_CORE_NAME})

  if(URING)
	  target_link_libraries(${exe} uring)
  endif()

  if (PERLLIBS_FOUND)
  	include_directories(${PERL_INCLUDE_PATH})
   	target_link_libraries(${exe} ${PERL_LIBRARY} )
  endif()

endforeach()
