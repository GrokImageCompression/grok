include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")

file(GLOB SRCS src/*.c)
file(GLOB HDRS include/*.h)

set(LIBTARGET "lcms2")
add_library(${LIBTARGET} STATIC ${SRCS} ${HDRS})
if(MSVC)
  set_target_properties(${LIBTARGET} PROPERTIES PREFIX "lib")
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  target_compile_options(${LIBTARGET} PRIVATE -Wstringop-truncation -Wno-stringop-truncation)
endif()
set_target_properties(${LIBTARGET}  PROPERTIES  OUTPUT_NAME "${LIBTARGET}")
set_property(TARGET ${LIBTARGET} PROPERTY POSITION_INDEPENDENT_CODE ON)
# set emcc options
if (CMAKE_SYSTEM_NAME STREQUAL Emscripten)
  target_link_options(${exe} PUBLIC -sASSERTIONS=1 -sPROXY_TO_PTHREAD -sTOTAL_MEMORY=1536MB -sEXIT_RUNTIME=1 -sUSE_PTHREADS=1)
  target_compile_options(${exe} PUBLIC -matomics)
endif()

