project(libpng C)

# suppress "explicit source extensions" warning
if(POLICY CMP0115)
  cmake_policy(SET CMP0115 OLD)
endif()

include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}"
  ${GROK_SOURCE_DIR}/thirdparty/libz
  ${CMAKE_BINARY_DIR}/thirdparty/libz
)

option(PNG_HARDWARE_OPTIMIZATIONS "Enable hardware optimizations" ON)

if(PNG_HARDWARE_OPTIMIZATIONS)

# set definitions and sources for arm
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm" OR
  CMAKE_SYSTEM_PROCESSOR MATCHES "^aarch64")
  set(PNG_ARM_NEON_POSSIBLE_VALUES on off)
  set(PNG_ARM_NEON "off" CACHE STRING "Enable ARM NEON optimizations:
     off: (default) disable the optimizations;
     on: turn on unconditionally.")
  set_property(CACHE PNG_ARM_NEON PROPERTY STRINGS
     ${PNG_ARM_NEON_POSSIBLE_VALUES})
  list(FIND PNG_ARM_NEON_POSSIBLE_VALUES ${PNG_ARM_NEON} index)
  if(index EQUAL -1)
    message(FATAL_ERROR
            "PNG_ARM_NEON must be one of [${PNG_ARM_NEON_POSSIBLE_VALUES}]")
  elseif(NOT ${PNG_ARM_NEON} STREQUAL "off")
    set(libpng_arm_sources
      arm/arm_init.c
      arm/filter_neon.S
      arm/filter_neon_intrinsics.c
      arm/palette_neon_intrinsics.c)

    if(${PNG_ARM_NEON} STREQUAL "on")
      add_definitions(-DPNG_ARM_NEON_OPT=2)
    endif()
  else()
    add_definitions(-DPNG_ARM_NEON_OPT=0)
  endif()
endif()

# set definitions and sources for intel
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^i?86" OR
   CMAKE_SYSTEM_PROCESSOR MATCHES "^x86_64*")
  set(PNG_INTEL_SSE_POSSIBLE_VALUES on off)
  set(PNG_INTEL_SSE "on" CACHE STRING "Enable INTEL_SSE optimizations:
     off: disable the optimizations")
  set_property(CACHE PNG_INTEL_SSE PROPERTY STRINGS
     ${PNG_INTEL_SSE_POSSIBLE_VALUES})
  list(FIND PNG_INTEL_SSE_POSSIBLE_VALUES ${PNG_INTEL_SSE} index)
  if(index EQUAL -1)
    message(FATAL_ERROR
            "PNG_INTEL_SSE must be one of [${PNG_INTEL_SSE_POSSIBLE_VALUES}]")
  elseif(NOT ${PNG_INTEL_SSE} STREQUAL "off")
    set(libpng_intel_sources
      intel/intel_init.c
      intel/filter_sse2_intrinsics.c)
    if(${PNG_INTEL_SSE} STREQUAL "on")
      add_definitions(-DPNG_INTEL_SSE_OPT=1)
    endif()
  else()
    add_definitions(-DPNG_INTEL_SSE_OPT=0)
  endif()
endif()

else(PNG_HARDWARE_OPTIMIZATIONS)

# set definitions and sources for arm
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm" OR
   CMAKE_SYSTEM_PROCESSOR MATCHES "^aarch64")
  add_definitions(-DPNG_ARM_NEON_OPT=0)
endif()

# set definitions and sources for intel
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^i?86" OR
   CMAKE_SYSTEM_PROCESSOR MATCHES "^x86_64*")
  add_definitions(-DPNG_INTEL_SSE_OPT=0)
endif()


endif(PNG_HARDWARE_OPTIMIZATIONS)


file(GLOB BASE_SRCS *.c)
set(libpng_sources
    ${BASE_SRCS}
    ${libpng_arm_sources}
    ${libpng_intel_sources}
 )
    
file(GLOB libpng_hdrs *.h)
set(EXT_HDRS
 ${GROK_SOURCE_DIR}/thirdparty/libz/zlib.h
 ${CMAKE_BINARY_DIR}/thirdparty/libz/zconf.h
)

#
set(LIBTARGET "png")
add_library(${LIBTARGET} STATIC ${libpng_sources} ${libpng_hdrs} ${EXT_HDRS})
set_property(TARGET ${LIBTARGET} PROPERTY POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  set_target_properties(${LIBTARGET} PROPERTIES PREFIX "lib")
endif(MSVC)
target_link_libraries(${LIBTARGET} zlib)
set_target_properties(${LIBTARGET}
	PROPERTIES
    OUTPUT_NAME "${LIBTARGET}")
