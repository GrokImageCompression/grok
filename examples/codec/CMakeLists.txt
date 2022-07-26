set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

IF(MSVC)
    string(APPEND CMAKE_CXX_FLAGS " /EHsc")
ENDIF(MSVC)

include_directories(
  ${GROK_BINARY_DIR}/src/lib/codec
  ${GROK_SOURCE_DIR}/src/lib/codec
  ${GROK_BINARY_DIR}/src/lib/core
  ${GROK_SOURCE_DIR}/src/lib/core
  ${CMAKE_CURRENT_BINARY_DIR}/../
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

foreach(exe codec_decompress_from_file_to_tiff)
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

  target_link_libraries(${exe}
    ${PNG_LIBNAME} ${TIFF_LIBNAME} ${LCMS_LIBNAME}
    ${JPEG_LIBNAME} ${CMAKE_THREAD_LIBS_INIT} ${GROK_CORE_NAME} ${GROK_CODEC_NAME} ${CMAKE_DL_LIBS} )

  # To support universal exe:
  if(ZLIB_FOUND AND APPLE)
    target_link_libraries(${exe} z)
  else(ZLIB_FOUND AND APPLE)
    target_link_libraries(${exe} ${Z_LIBNAME})
  endif()

  if(URING)
	  target_link_libraries(${exe} uring)
  endif()

  if (PERLLIBS_FOUND)
  	include_directories(${PERL_INCLUDE_PATH})
   	target_link_libraries(${exe} ${PERL_LIBRARY} )
  endif()

  install(TARGETS ${exe}
    EXPORT GrokTargets
    DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Applications
  )
endforeach()
