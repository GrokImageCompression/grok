# Copyright (C) 2016-2026 Grok Image Compression Inc.
#
# This source code is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License, version 3,
# as published by the Free Software Foundation.
#
# This source code is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.


cmake_minimum_required(VERSION 3.21)

project(GROK
  VERSION 20.2.4
  LANGUAGES C CXX)

#######################################
# GROK version — derived from project()
set(GROK_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(GROK_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(GROK_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(GROK_VERSION "${PROJECT_VERSION}")
set(PACKAGE_VERSION "${PROJECT_VERSION}")

# As autotools does not support X.Y notation for SOVERSION, we have to use
# two different versions, one for Grok itself and one for its .so
if(NOT GROK_SOVERSION)
  set(GROK_SOVERSION 1)
endif()
set(GROK_LIBRARY_PROPERTIES
  VERSION   "${GROK_VERSION_MAJOR}.${GROK_VERSION_MINOR}.${GROK_VERSION_PATCH}"
  SOVERSION "${GROK_SOVERSION}"
)
#######################################

set(GROK_CORE_NAME grokj2k)
set(GROK_CODEC_NAME grokj2kcodec)
set(GROK_PLUGIN_NAME grokj2k_plugin)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (CMAKE_SYSTEM_PROCESSOR MATCHES "unknown")
  # uname -p is broken on this system.  Try uname -m
  execute_process(COMMAND uname -m
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    OUTPUT_VARIABLE GRK_ARCH)
else()
  set(GRK_ARCH ${CMAKE_SYSTEM_PROCESSOR})
endif()
message(STATUS "Architecture: " ${GRK_ARCH})

if(MSVC)
    add_compile_options(/EHsc /utf-8)
endif()

if ( (CMAKE_CXX_COMPILER_ID MATCHES "GNU") AND (CMAKE_CXX_COMPILER_VERSION LESS 10.0) )
   message(FATAL_ERROR "GNU compiler version must be at least 10.0")
endif()

# Set a default build type if none was specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
    "MinSizeRel" "RelWithDebInfo")
endif()

include(CheckPIESupported OPTIONAL RESULT_VARIABLE CHECK_PIE_SUPPORTED)
check_pie_supported(LANGUAGES CXX)
if(CMAKE_CXX_LINK_PIE_SUPPORTED)
  set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
endif()

add_compile_options(
  $<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<CONFIG:RelWithDebInfo>>:-fno-omit-frame-pointer>)
add_link_options(
  $<$<CONFIG:RelWithDebInfo>:LINKER:--build-id>)


# Path to additional CMake modules
set(CMAKE_MODULE_PATH
    ${GROK_SOURCE_DIR}/cmake
    ${CMAKE_MODULE_PATH})

include(ExternalProject)
# Install directories
include(GNUInstallDirs)
string(TOLOWER ${PROJECT_NAME} projectname)
set(GROK_INSTALL_SUBDIR "grok-${GROK_VERSION_MAJOR}.${GROK_VERSION_MINOR}")
set(GROK_INSTALL_PACKAGE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/${GROK_INSTALL_SUBDIR}")
if (APPLE)
	list(APPEND GROK_LIBRARY_PROPERTIES INSTALL_NAME_DIR "${CMAKE_INSTALL_FULL_LIBDIR}")
endif()

# Big endian test:
include(TestBigEndian)
if(NOT CMAKE_SYSTEM_NAME STREQUAL Emscripten)
  test_big_endian(GROK_BIG_ENDIAN)
endif()

# Grok build configuration options.
option(BUILD_SHARED_LIBS "Build Grok shared library and link executables against it." ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin CACHE PATH "Single output directory for building all executables.")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin CACHE PATH "Single output directory for building all libraries.")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin CACHE PATH "Single output directory for building all archives.")
# Keep legacy alias for test scripts that reference EXECUTABLE_OUTPUT_PATH
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
mark_as_advanced(CMAKE_RUNTIME_OUTPUT_DIRECTORY CMAKE_LIBRARY_OUTPUT_DIRECTORY CMAKE_ARCHIVE_OUTPUT_DIRECTORY)

# Compiler specific flags:
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  list(APPEND GROK_COMPILE_OPTIONS -Wall -Wextra -Wconversion -Wsign-conversion -Wunused-parameter)
elseif(MSVC)
  list(APPEND GROK_COMPILE_OPTIONS /MP)
endif()

# grk_config.h generation
# Option choose whether to use static runtime
include(ucm)
if(BUILD_SHARED_LIBS)
    ucm_set_runtime(DYNAMIC)
else()
    add_compile_definitions(GRK_STATIC)
    ucm_set_runtime(STATIC)
endif()

if(WIN32)
  add_compile_definitions(NOMINMAX)
  if(BUILD_SHARED_LIBS)
    add_compile_definitions(GRK_EXPORTS)
  endif()
endif()

include(CheckSymbolExists)

# Special Builds
option(GRK_BUILD_DCI "Build DCI" OFF)
mark_as_advanced(GRK_BUILD_DCI)
option(GRK_BUNDLE_STATIC_CORE "Bundle static core library" OFF)
mark_as_advanced(GRK_BUNDLE_STATIC_CORE)
option(GRK_BUILD_LIBPNG "Build libpng library" ON)
option(GRK_BUILD_LIBTIFF "Build libtiff library" ON)
option(GRK_BUILD_LCMS2 "Build lcms2 library" ON)
option(GRK_BUILD_JPEG "Build jpeg library" ON)
add_subdirectory(thirdparty)

# Build Library
option(BUILD_LUTS_GENERATOR "Build utility to generate t1_luts.h" OFF)

# Build examples
option(GRK_BUILD_CORE_EXAMPLES "Build core examples" ON)
option(GRK_BUILD_CODEC_EXAMPLES "Build codec examples" OFF)

# Locate test data (shared by examples and tests)
find_path(GRK_DATA_ROOT README-GROK-TEST-DATA
  PATHS $ENV{GRK_DATA_ROOT} ${CMAKE_SOURCE_DIR}/../grok-test-data
  NO_CMAKE_FIND_ROOT_PATH)

# examples use files from data folder
if(GRK_BUILD_CORE_EXAMPLES OR GRK_BUILD_CODEC_EXAMPLES)
  add_subdirectory(examples)
endif()

if(WIN32)
  add_link_options(
    $<$<CONFIG:Release>:/DEBUG:NONE>
    $<$<CONFIG:Release>:/RELEASE>)
  if(MSVC)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
      $<$<CONFIG:Release>:> $<$<NOT:$<CONFIG:Release>>:ProgramDatabase>)
  endif()
elseif(APPLE)
  add_link_options($<$<CONFIG:Release>:LINKER:-x>)
else()
  # Strip all symbols in Release builds on Linux/other
  add_link_options($<$<CONFIG:Release>:LINKER:--strip-all>)
endif()

# Build Applications
option(GRK_BUILD_CODEC "Build the CODEC executables" ON)
option(GRK_BUILD_PLUGIN_LOADER "Enable loading of T1 plugin" OFF)
mark_as_advanced(GRK_BUILD_PLUGIN_LOADER)

# Add an option to enable or disable the ExifTool check
option(GRK_ENABLE_EXIFTOOL "Enable detection of ExifTool Perl module" ON)

# Check for Perl libraries and ExifTool only if the option is enabled
if (GRK_ENABLE_EXIFTOOL)
    find_package(PerlLibs)
    if (PERLLIBS_FOUND)
        message(STATUS "Perl libraries found")
        execute_process(COMMAND ${PERL_EXECUTABLE} -MImage::ExifTool -e ""
            ERROR_QUIET RESULT_VARIABLE status)
        if (NOT status)
            message(STATUS "ExifTool Perl module found")
            set(GROK_HAVE_EXIFTOOL define)
        else()
            message(STATUS "ExifTool Perl module not found")
        endif()
    else()
        message(STATUS "Perl libraries not found")
    endif()
else()
    message(STATUS "ExifTool detection is disabled")
endif()

set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(src/include/spdlog)
set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED})

add_subdirectory(src/lib/core)

# Build codec library before SWIG bindings (codec bindings link against it)
if(GRK_BUILD_CODEC)
  add_subdirectory(src/lib/codec)
endif()

# SWIG bindings for grok_core and grok_codec
option(GRK_BUILD_CORE_SWIG_BINDINGS "Build SWIG Python bindings for grok core library" ON)
option(GRK_BUILD_CSHARP_SWIG_BINDINGS "Build SWIG C# bindings for grok core library" OFF)
option(GRK_BUILD_JAVA_SWIG_BINDINGS "Build SWIG Java bindings for grok core library" OFF)

set(GRK_SWIG_BINDINGS_AVAILABLE FALSE)
set(GRK_BUILD_PYTHON_SWIG FALSE)
set(GRK_BUILD_CSHARP_SWIG FALSE)
set(GRK_BUILD_JAVA_SWIG FALSE)

if(GRK_BUILD_CORE_SWIG_BINDINGS OR GRK_BUILD_CSHARP_SWIG_BINDINGS OR GRK_BUILD_JAVA_SWIG_BINDINGS)
    find_package(SWIG QUIET)
    if(NOT SWIG_FOUND)
        message(STATUS "SWIG not found - all SWIG bindings disabled")
    else()
        include(UseSWIG)

        # Python bindings
        if(GRK_BUILD_CORE_SWIG_BINDINGS)
            find_package(Python3 COMPONENTS Interpreter Development QUIET)
            if(Python3_FOUND)
                set(GRK_BUILD_PYTHON_SWIG TRUE)
                set(GRK_SWIG_BINDINGS_AVAILABLE TRUE)
            else()
                message(STATUS "Python3 development files not found - Python bindings disabled")
            endif()
        endif()

        # C# bindings
        if(GRK_BUILD_CSHARP_SWIG_BINDINGS)
            set(GRK_BUILD_CSHARP_SWIG TRUE)
            message(STATUS "C# SWIG bindings enabled")
        endif()

        # Java bindings
        if(GRK_BUILD_JAVA_SWIG_BINDINGS)
            find_package(JNI QUIET)
            if(JNI_FOUND)
                set(GRK_BUILD_JAVA_SWIG TRUE)
                message(STATUS "Java SWIG bindings enabled")
            else()
                message(STATUS "JNI not found - Java bindings disabled (install a JDK)")
            endif()
        endif()

        if(GRK_BUILD_PYTHON_SWIG OR GRK_BUILD_CSHARP_SWIG OR GRK_BUILD_JAVA_SWIG)
            add_subdirectory(bindings/swig)
        endif()
    endif()
endif()

# Build CLI tools (depends on codec library)
if(GRK_BUILD_CODEC)
  add_subdirectory(src/bin)
endif()

# grk_config.h generation
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/lib/core/grk_config.h.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/src/lib/core/grk_config.h
  @ONLY)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/lib/core/grk_config_private.h.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/src/lib/core/grk_config_private.h
  @ONLY)

# Build DOCUMENTATION
option(GRK_BUILD_DOC "Build HTML documentation (with doxygen if available)." OFF)
if(GRK_BUILD_DOC)
  add_subdirectory(doc)
endif()

# Build Testing
option(BUILD_TESTING "Build tests." OFF)
if(BUILD_TESTING AND GRK_BUILD_CODEC AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    include(CTest)
    add_subdirectory(tests)
    set(GRK_BUILD_CORE_EXAMPLES ON CACHE BOOL "Build core examples" FORCE)
endif()

# Python tests: auto-enabled when BUILD_TESTING and SWIG bindings are available
option(GRK_BUILD_PYTHON_TESTS "Build and register pytest-based Python tests" OFF)
if(NOT DEFINED GRK_BUILD_PYTHON_TESTS_SET_BY_USER)
    # Auto-enable when BUILD_TESTING is ON and SWIG bindings built
    if(BUILD_TESTING AND GRK_SWIG_BINDINGS_AVAILABLE)
        set(GRK_BUILD_PYTHON_TESTS ON)
    endif()
endif()
if(GRK_BUILD_PYTHON_TESTS)
    if(NOT GRK_SWIG_BINDINGS_AVAILABLE)
        message(WARNING "GRK_BUILD_PYTHON_TESTS requires SWIG bindings - skipping Python tests")
    else()
        execute_process(
            COMMAND ${Python3_EXECUTABLE} -m pytest --version
            RESULT_VARIABLE PYTEST_RESULT
            OUTPUT_QUIET ERROR_QUIET)
        if(PYTEST_RESULT EQUAL 0)
            enable_testing()
            add_subdirectory(tests/python)
            message(STATUS "Python tests enabled")
        else()
            message(STATUS "pytest not found - Python tests disabled (install with: pip install pytest)")
        endif()
    endif()
endif()

# C# tests: auto-enabled when BUILD_TESTING and C# SWIG bindings are available
option(GRK_BUILD_CSHARP_TESTS "Build and register C# xUnit tests" OFF)
if(NOT DEFINED GRK_BUILD_CSHARP_TESTS_SET_BY_USER)
    if(BUILD_TESTING AND GRK_BUILD_CSHARP_SWIG)
        set(GRK_BUILD_CSHARP_TESTS ON)
    endif()
endif()
if(GRK_BUILD_CSHARP_TESTS)
    find_program(DOTNET_EXECUTABLE dotnet)
    if(DOTNET_EXECUTABLE)
        set(GRK_CSHARP_GENERATED_DIR "${CMAKE_BINARY_DIR}/bin/csharp")
        set(GRK_CSHARP_TEST_DIR "${CMAKE_SOURCE_DIR}/tests/csharp")
        enable_testing()
        add_test(
            NAME csharp_tests
            COMMAND ${DOTNET_EXECUTABLE} test
                "${GRK_CSHARP_TEST_DIR}/GrokCoreTests.csproj"
                --no-restore
                -p:GRK_CSHARP_GENERATED_DIR=${GRK_CSHARP_GENERATED_DIR}
                --verbosity normal
            WORKING_DIRECTORY ${GRK_CSHARP_GENERATED_DIR}
        )
        set_tests_properties(csharp_tests PROPERTIES
            ENVIRONMENT "LD_LIBRARY_PATH=${GRK_CSHARP_GENERATED_DIR}:${CMAKE_BINARY_DIR}/bin:$ENV{LD_LIBRARY_PATH}"
        )
        message(STATUS "C# tests enabled")
    else()
        message(STATUS "dotnet not found - C# tests disabled")
    endif()
endif()

# Java tests: auto-enabled when BUILD_TESTING and Java SWIG bindings are available
option(GRK_BUILD_JAVA_TESTS "Build and register Java JUnit tests" OFF)
if(NOT DEFINED GRK_BUILD_JAVA_TESTS_SET_BY_USER)
    if(BUILD_TESTING AND GRK_BUILD_JAVA_SWIG)
        set(GRK_BUILD_JAVA_TESTS ON)
    endif()
endif()
if(GRK_BUILD_JAVA_TESTS)
    find_program(JAVA_EXECUTABLE java)
    find_program(JAVAC_EXECUTABLE javac)
    if(JAVA_EXECUTABLE AND JAVAC_EXECUTABLE)
        set(GRK_JAVA_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/java")
        set(GRK_JAVA_TEST_DIR "${CMAKE_SOURCE_DIR}/tests/java")
        enable_testing()
        add_test(
            NAME java_tests
            COMMAND ${CMAKE_COMMAND}
                -DJAVA_EXECUTABLE=${JAVA_EXECUTABLE}
                -DJAVAC_EXECUTABLE=${JAVAC_EXECUTABLE}
                -DGRK_JAVA_OUTPUT_DIR=${GRK_JAVA_OUTPUT_DIR}
                -DGRK_JAVA_TEST_DIR=${GRK_JAVA_TEST_DIR}
                -P "${GRK_JAVA_TEST_DIR}/run_tests.cmake"
            WORKING_DIRECTORY ${GRK_JAVA_OUTPUT_DIR}
        )
        set_tests_properties(java_tests PROPERTIES
            ENVIRONMENT "LD_LIBRARY_PATH=${GRK_JAVA_OUTPUT_DIR}:${CMAKE_BINARY_DIR}/bin:$ENV{LD_LIBRARY_PATH}"
        )
        message(STATUS "Java tests enabled")
    else()
        message(STATUS "java/javac not found - Java tests disabled")
    endif()
endif()

# ===================================================================
# PACKAGE CONFIGURATION - FIXED VERSION HANDLING
# ===================================================================
if (BUILD_SHARED_LIBS AND NOT CMAKE_SYSTEM_NAME STREQUAL Emscripten)
  # Install the GrokTargets file
  install(EXPORT GrokTargets
          DESTINATION ${GROK_INSTALL_PACKAGE_DIR}
          NAMESPACE GROK::)

  # Generate the GrokConfig.cmake file
  include(CMakePackageConfigHelpers)
  configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GrokConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/GrokConfig.cmake
    INSTALL_DESTINATION ${GROK_INSTALL_PACKAGE_DIR}
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR)

  # Generate the version file
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/GrokConfigVersion.cmake"
    VERSION "${GROK_VERSION}"
    COMPATIBILITY SameMinorVersion
  )

  # Install both config and version files
  install(FILES
            ${CMAKE_CURRENT_BINARY_DIR}/GrokConfig.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/GrokConfigVersion.cmake
          DESTINATION ${GROK_INSTALL_PACKAGE_DIR})
endif()

if(GRK_BUILD_DOC)
   install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

include (cmake/GrokCPack.cmake)

# pkgconfig support
# enabled by default on Unix, disabled by default on other platforms
if(UNIX)
  option(GRK_BUILD_PKGCONFIG_FILES "Build and install pkg-config files" ON)
else()
  option(GRK_BUILD_PKGCONFIG_FILES "Build and install pkg-config files" OFF)
endif()
if(GRK_BUILD_PKGCONFIG_FILES)
   configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/lib/core/libgrokj2k.pc.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/libgrokj2k.pc @ONLY)
  install( FILES  ${CMAKE_CURRENT_BINARY_DIR}/libgrokj2k.pc DESTINATION
    ${CMAKE_INSTALL_LIBDIR}/pkgconfig )
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/lib/codec/libgrokj2kcodec.pc.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/libgrokj2kcodec.pc @ONLY)
  install( FILES  ${CMAKE_CURRENT_BINARY_DIR}/libgrokj2kcodec.pc DESTINATION
    ${CMAKE_INSTALL_LIBDIR}/pkgconfig )
endif()