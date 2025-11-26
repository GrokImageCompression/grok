include_directories(
  ${CMAKE_CURRENT_BINARY_DIR}/../../src/lib/core
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/lib/core
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/include/CLI11/include
  ${CMAKE_CURRENT_BINARY_DIR}/../
  ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty
)

foreach(exe core_decompress
            core_compress
            core_simple
)
  add_executable(${exe} ${exe}.cpp ${common_SRCS})
  target_compile_options(${exe} PRIVATE ${GROK_COMPILE_OPTIONS})
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	  target_link_options(${exe}  PUBLIC "LINKER:-z,now")
  endif()

  target_link_libraries(${exe} ${GROK_CORE_NAME})

  # set emcc options
  if (CMAKE_SYSTEM_NAME STREQUAL Emscripten)
    target_link_options(${exe} PUBLIC -sASSERTIONS=1 -sPROXY_TO_PTHREAD -sTOTAL_MEMORY=1536MB -sEXIT_RUNTIME=1 -sUSE_PTHREADS=1)
    target_compile_options(${exe} PUBLIC -matomics)
  endif()

endforeach()
