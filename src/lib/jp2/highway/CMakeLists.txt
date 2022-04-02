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

project(hwy VERSION 0.16.0)  # Keep in sync with highway.h version

# Directly define the ABI version from the cmake project() version values:
set(LIBRARY_VERSION "${hwy_VERSION}")
set(LIBRARY_SOVERSION ${hwy_VERSION_MAJOR})

set(CMAKE_CXX_EXTENSIONS OFF)

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

set(HWY_CMAKE_ARM7 OFF CACHE BOOL "Set copts for ARMv7 with NEON (requires vfpv4)?")

# Unconditionally adding -Werror risks breaking the build when new warnings
# arise due to compiler/platform changes. Enable this in CI/tests.
set(HWY_WARNINGS_ARE_ERRORS OFF CACHE BOOL "Add -Werror flag?")

set(HWY_ENABLE_EXAMPLES ON CACHE BOOL "Build examples")
set(HWY_ENABLE_INSTALL ON CACHE BOOL "Install library")

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
    hwy/contrib/dot/dot-inl.h
    hwy/contrib/image/image.cc
    hwy/contrib/image/image.h
    hwy/contrib/math/math-inl.h
    hwy/contrib/sort/disabled_targets.h
    hwy/contrib/sort/shared-inl.h
    hwy/contrib/sort/sorting_networks-inl.h
    hwy/contrib/sort/traits-inl.h
    hwy/contrib/sort/traits128-inl.h
    hwy/contrib/sort/vqsort-inl.h
    hwy/contrib/sort/vqsort.cc
    hwy/contrib/sort/vqsort.h
    hwy/contrib/sort/vqsort_128a.cc
    hwy/contrib/sort/vqsort_128d.cc
    hwy/contrib/sort/vqsort_f32a.cc
    hwy/contrib/sort/vqsort_f32d.cc
    hwy/contrib/sort/vqsort_f64a.cc
    hwy/contrib/sort/vqsort_f64d.cc
    hwy/contrib/sort/vqsort_i16a.cc
    hwy/contrib/sort/vqsort_i16d.cc
    hwy/contrib/sort/vqsort_i32a.cc
    hwy/contrib/sort/vqsort_i32d.cc
    hwy/contrib/sort/vqsort_i64a.cc
    hwy/contrib/sort/vqsort_i64d.cc
    hwy/contrib/sort/vqsort_u16a.cc
    hwy/contrib/sort/vqsort_u16d.cc
    hwy/contrib/sort/vqsort_u32a.cc
    hwy/contrib/sort/vqsort_u32d.cc
    hwy/contrib/sort/vqsort_u64a.cc
    hwy/contrib/sort/vqsort_u64d.cc
)

set(HWY_SOURCES
    hwy/aligned_allocator.cc
    hwy/aligned_allocator.h
    hwy/base.h
    hwy/cache_control.h
    hwy/detect_compiler_arch.h  # private
    hwy/detect_targets.h  # private
    hwy/foreach_target.h
    hwy/highway.h
    hwy/highway_export.h
    hwy/nanobenchmark.cc
    hwy/nanobenchmark.h
    hwy/ops/arm_neon-inl.h
    hwy/ops/arm_sve-inl.h
    hwy/ops/generic_ops-inl.h
    hwy/ops/scalar-inl.h
    hwy/ops/set_macros-inl.h
    hwy/ops/shared-inl.h
    hwy/ops/wasm_128-inl.h
    hwy/ops/x86_128-inl.h
    hwy/ops/x86_256-inl.h
    hwy/ops/x86_512-inl.h
    hwy/print-inl.h
    hwy/print.cc
    hwy/print.h
    hwy/targets.cc
    hwy/targets.h
)

set(HWY_TEST_SOURCES
    hwy/tests/hwy_gtest.h
    hwy/tests/test_util-inl.h
    hwy/tests/test_util.cc
    hwy/tests/test_util.h
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
    # These are not included in Wall nor Wextra:
    -Wconversion
    -Wsign-conversion
    -Wvla
    -Wnon-virtual-dtor
  )

  if(${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    list(APPEND HWY_FLAGS
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
    if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 6.0)
      list(APPEND HWY_FLAGS -Wc++2a-extensions)
    endif()
  endif()

  if (WIN32)
    if(${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
      list(APPEND HWY_FLAGS
        -Wno-global-constructors
        -Wno-language-extension-token
        -Wno-used-but-marked-unused
        -Wno-shadow-field-in-constructor
        -Wno-unused-member-function
        -Wno-unused-template
        -Wno-c++98-compat-pedantic
        -Wno-used-but-marked-unused
        -Wno-zero-as-null-pointer-constant
      )
    endif()

    list(APPEND HWY_FLAGS
      -Wno-cast-align
      -Wno-double-promotion
      -Wno-float-equal
      -Wno-format-nonliteral
      -Wno-shadow
      -Wno-sign-conversion
    )
  else()
    list(APPEND HWY_FLAGS
      -fmath-errno
      -fno-exceptions
    )
  endif()  # WIN32

  if (HWY_CMAKE_ARM7)
    list(APPEND HWY_FLAGS
      -march=armv7-a
      -mfpu=neon-vfpv4
      -mfloat-abi=hard  # must match the toolchain specified as CXX=
      -mfp16-format=ieee  # required for vcvt_f32_f16
    )
  endif()  # HWY_CMAKE_ARM7

  if (HWY_WARNINGS_ARE_ERRORS)
    list(APPEND HWY_FLAGS -Werror)
  endif()

endif()  # !MSVC

add_library(hwy STATIC ${HWY_SOURCES})
target_compile_options(hwy PRIVATE ${HWY_FLAGS})
set_property(TARGET hwy PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(hwy PUBLIC ${CMAKE_CURRENT_LIST_DIR})
