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


cmake_minimum_required(VERSION 3.20)

project(GROK)

#######################################
# GROK version
set(GROK_VERSION_MAJOR 20)
set(GROK_VERSION_MINOR 0)
set(GROK_VERSION_PATCH 5)
set(GROK_VERSION
  "${GROK_VERSION_MAJOR}.${GROK_VERSION_MINOR}.${GROK_VERSION_PATCH}")
set(PACKAGE_VERSION
  "${GROK_VERSION_MAJOR}.${GROK_VERSION_MINOR}.${GROK_VERSION_PATCH}")

# As autotools does not support X.Y notation for SOVERSION, we have to use
# two different versions, one for Grok itself and one for its .so
if(NOT GROK_SOVERSION)
  set(GROK_SOVERSION 1)
endif(NOT GROK_SOVERSION)
set(GROK_LIBRARY_PROPERTIES
  VERSION   "${GROK_VERSION_MAJOR}.${GROK_VERSION_MINOR}.${GROK_VERSION_PATCH}"
  SOVERSION "${GROK_SOVERSION}"
)
#######################################

set(GROK_CORE_NAME grokj2k)
set(GROK_CODEC_NAME grokj2kcodec)
set(GROK_PLUGIN_NAME grokj2k_plugin)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (CMAKE_SYSTEM_PROCESSOR MATCHES "unknown")
  # uname -p is broken on this system.  Try uname -m
  EXECUTE_PROCESS( COMMAND uname -m
		   OUTPUT_STRIP_TRAILING_WHITESPACE
		   ERROR_QUIET
		   OUTPUT_VARIABLE GRK_ARCH)
else (CMAKE_SYSTEM_PROCESSOR MATCHES "unknown")
  set(GRK_ARCH ${CMAKE_SYSTEM_PROCESSOR})
endif (CMAKE_SYSTEM_PROCESSOR MATCHES "unknown")
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

set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -fno-omit-frame-pointer")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -fno-omit-frame-pointer")
set(CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO} -Wl,--build-id")


# Path to additional CMake modules
set(CMAKE_MODULE_PATH
    ${GROK_SOURCE_DIR}/cmake
    ${CMAKE_MODULE_PATH})

include (ExternalProject)
# Install directories
include(GNUInstallDirs)
string(TOLOWER ${PROJECT_NAME} projectname)
set(GROK_INSTALL_SUBDIR "grok-${GROK_VERSION_MAJOR}.${GROK_VERSION_MINOR}")
set(GROK_INSTALL_PACKAGE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/${GROK_INSTALL_SUBDIR}")
if (APPLE)
	list(APPEND GROK_LIBRARY_PROPERTIES INSTALL_NAME_DIR "${CMAKE_INSTALL_FULL_LIBDIR}")
endif()

# Big endian test:
include (${CMAKE_ROOT}/Modules/TestBigEndian.cmake)
if (NOT CMAKE_SYSTEM_NAME STREQUAL Emscripten)
TEST_BIG_ENDIAN(GROK_BIG_ENDIAN)
endif()

# Grok build configuration options.
option(BUILD_SHARED_LIBS "Build Grok shared library and link executables against it." ON)
set (EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/bin CACHE PATH "Single output directory for building all executables.")
set (LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/bin CACHE PATH "Single output directory for building all libraries.")
mark_as_advanced(LIBRARY_OUTPUT_PATH EXECUTABLE_OUTPUT_PATH)

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
	add_definitions(-DGRK_STATIC)
    ucm_set_runtime(STATIC)
endif()

if(WIN32)
  add_definitions(-DNOMINMAX)
  if(BUILD_SHARED_LIBS)
    add_definitions(-DGRK_EXPORTS)
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

# examples use files from data folder
if (GRK_BUILD_CORE_EXAMPLES OR GRK_BUILD_CODEC_EXAMPLES)
	add_subdirectory(examples)
	find_path(GRK_DATA_ROOT README-GROK-TEST-DATA
	  PATHS $ENV{GRK_DATA_ROOT} ${CMAKE_SOURCE_DIR}/../grok-test-data
	  NO_CMAKE_FIND_ROOT_PATH)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(WIN32)
        # Ensure no debug symbols & remove Debug Directory
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG:NONE /RELEASE")
        set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG:NONE /RELEASE")

        if(MSVC)
            # Ensure MSVC does not generate .pdb files
            set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "None")
        endif()
    elseif(APPLE)
        # Strip all local symbols for macOS
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-x")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-x")
    else()
        # Strip all symbols for shared libraries on other non-Windows platforms (e.g., Linux)
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--strip-all")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--strip-all")
    endif()
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

add_subdirectory(src/lib/core)
# Option to build SWIG bindings for grok_core (default OFF)
option(GRK_BUILD_CORE_SWIG_BINDINGS "Build SWIG Python bindings for grok core library" OFF)

# Conditionally build SWIG bindings for grok_core
if(GRK_BUILD_CORE_SWIG_BINDINGS)
    # Enable SWIG and Python dependencies
    find_package(SWIG REQUIRED)
    find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
    include(UseSWIG)

    # Add the bindings subdirectory where the SWIG interface for grok_core is located
    add_subdirectory(bindings/swig)
endif()



if(GRK_BUILD_CODEC)
  add_subdirectory(src/bin)
  add_subdirectory(src/lib/codec)
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
    find_path(GRK_DATA_ROOT README-GROK-TEST-DATA
      PATHS $ENV{GRK_DATA_ROOT} ${CMAKE_SOURCE_DIR}/../grok-test-data
      NO_CMAKE_FIND_ROOT_PATH)
    add_subdirectory(tests)
    set(GRK_BUILD_CORE_EXAMPLES ON CACHE BOOL "Build core examples" FORCE)
endif()

# install all targets referenced as GrokTargets
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

  # Install the GrokConfig.cmake file
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/GrokConfig.cmake
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
