set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

IF(MSVC)
    string(APPEND CMAKE_CXX_FLAGS " /EHsc")
ENDIF(MSVC)

include_directories(
  ${GROK_BINARY_DIR}/src/lib/codec
  ${GROK_SOURCE_DIR}/src/lib/codec
  ${GROK_BINARY_DIR}/src/lib/jp2
  ${GROK_SOURCE_DIR}/src/lib/jp2
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

foreach(exe grk_example_library_decompress)
  add_executable(${exe} ${exe}.cpp ${common_SRCS})
  target_compile_options(${exe} PRIVATE ${GROK_COMPILE_OPTIONS})
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	target_link_options(${exe}  PUBLIC "LINKER:-z,now")
  endif()

  target_link_libraries(${exe} ${CMAKE_THREAD_LIBS_INIT} ${GROK_LIBRARY_NAME} ${CMAKE_DL_LIBS} )

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
