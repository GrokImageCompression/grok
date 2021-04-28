# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.10)

# Set PIE flags for POSITION_INDEPENDENT_CODE targets, added in 3.14.
if(POLICY CMP0083)
  cmake_policy(SET CMP0083 NEW)
endif()

project(hwy VERSION 0.12.0)  # Keep in sync with highway.h version

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

# Enabled PIE binaries by default if supported.
include(CheckPIESupported OPTIONAL RESULT_VARIABLE CHECK_PIE_SUPPORTED)
if(CHECK_PIE_SUPPORTED)
  check_pie_supported(LANGUAGES CXX)
  if(CMAKE_CXX_LINK_PIE_SUPPORTED)
    set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
  endif()
endif()

include(GNUInstallDirs)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(CheckCXXSourceCompiles)
check_cxx_source_compiles(
   "int main() {
      #if !defined(__EMSCRIPTEN__)
      static_assert(false, \"__EMSCRIPTEN__ is not defined\");
      #endif
      return 0;
    }"
  HWY_EMSCRIPTEN
)

set(HWY_CONTRIB_SOURCES
    hwy/contrib/image/image.cc
    hwy/contrib/image/image.h
    hwy/contrib/math/math-inl.h
)

set(HWY_SOURCES
    hwy/aligned_allocator.cc
    hwy/aligned_allocator.h
    hwy/base.h
    hwy/cache_control.h
    hwy/foreach_target.h
    hwy/highway.h
    hwy/nanobenchmark.cc
    hwy/nanobenchmark.h
    hwy/ops/arm_neon-inl.h
    hwy/ops/scalar-inl.h
    hwy/ops/set_macros-inl.h
    hwy/ops/shared-inl.h
    hwy/ops/wasm_128-inl.h
    hwy/ops/x86_128-inl.h
    hwy/ops/x86_256-inl.h
    hwy/ops/x86_512-inl.h
    hwy/targets.cc
    hwy/targets.h
    hwy/tests/test_util-inl.h
)

if (MSVC)
  # TODO(janwas): add flags
else()
  set(HWY_FLAGS
    # Avoid changing binaries based on the current time and date.
    -Wno-builtin-macro-redefined
    -D__DATE__="redacted"
    -D__TIMESTAMP__="redacted"
    -D__TIME__="redacted"

    # Optimizations
    -fmerge-all-constants

    # Warnings
    -Wall
    -Wextra
    -Wformat-security
    -Wno-unused-function
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wvla
  )

  if(${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    list(APPEND HWY_FLAGS
      -Wc++2a-extensions
      -Wfloat-overflow-conversion
      -Wfloat-zero-conversion
      -Wfor-loop-analysis
      -Wgnu-redeclared-enum
      -Winfinite-recursion
      -Wself-assign
      -Wstring-conversion
      -Wtautological-overlap-compare
      -Wthread-safety-analysis
      -Wundefined-func-template

      -fno-cxx-exceptions
      -fno-slp-vectorize
      -fno-vectorize

      # Use color in messages
      -fdiagnostics-show-option -fcolor-diagnostics
    )
  endif()

  if (WIN32)
    list(APPEND HWY_FLAGS
      -Wno-c++98-compat-pedantic
      -Wno-cast-align
      -Wno-double-promotion
      -Wno-float-equal
      -Wno-format-nonliteral
      -Wno-global-constructors
      -Wno-language-extension-token
      -Wno-missing-prototypes
      -Wno-shadow
      -Wno-shadow-field-in-constructor
      -Wno-sign-conversion
      -Wno-unused-member-function
      -Wno-unused-template
      -Wno-used-but-marked-unused
      -Wno-zero-as-null-pointer-constant
    )
  else()
    list(APPEND HWY_FLAGS
      -fmath-errno
      -fno-exceptions
    )
  endif()
endif()

add_library(hwy STATIC ${HWY_SOURCES})
target_compile_options(hwy PRIVATE ${HWY_FLAGS})
set_property(TARGET hwy PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(hwy PUBLIC ${CMAKE_CURRENT_LIST_DIR})

add_library(hwy_contrib STATIC ${HWY_CONTRIB_SOURCES})
target_compile_options(hwy_contrib PRIVATE ${HWY_FLAGS})
set_property(TARGET hwy_contrib PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(hwy_contrib PUBLIC ${CMAKE_CURRENT_LIST_DIR})

# -------------------------------------------------------- install library
install(TARGETS hwy
  DESTINATION "${CMAKE_INSTALL_LIBDIR}")
# Install all the headers keeping the relative path to the current directory
# when installing them.
foreach (source ${HWY_SOURCES})
  if ("${source}" MATCHES "\.h$")
    get_filename_component(dirname "${source}" DIRECTORY)
    install(FILES "${source}"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${dirname}")
  endif()
endforeach()

install(TARGETS hwy_contrib
  DESTINATION "${CMAKE_INSTALL_LIBDIR}")
# Install all the headers keeping the relative path to the current directory
# when installing them.
foreach (source ${HWY_CONTRIB_SOURCES})
  if ("${source}" MATCHES "\.h$")
    get_filename_component(dirname "${source}" DIRECTORY)
    install(FILES "${source}"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${dirname}")
  endif()
endforeach()

# Add a pkg-config file for libhwy and the contrib/test libraries.
set(HWY_LIBRARY_VERSION "${CMAKE_PROJECT_VERSION}")
foreach (pc libhwy.pc libhwy-contrib.pc libhwy-test.pc)
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${pc}.in" "${pc}" @ONLY)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${pc}"
      DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endforeach()
