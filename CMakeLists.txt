cmake_minimum_required(VERSION 3.20)

project(GROK)

#######################################
# GROK version
set(GROK_VERSION_MAJOR 12)
set(GROK_VERSION_MINOR 0)
set(GROK_VERSION_BUILD 2)
set(GROK_VERSION
  "${GROK_VERSION_MAJOR}.${GROK_VERSION_MINOR}.${GROK_VERSION_BUILD}")
set(PACKAGE_VERSION
  "${GROK_VERSION_MAJOR}.${GROK_VERSION_MINOR}.${GROK_VERSION_BUILD}")

# As autotools does not support X.Y notation for SOVERSION, we have to use
# two different versions, one for Grok itself and one for its .so
if(NOT GROK_SOVERSION)
  set(GROK_SOVERSION 1)
endif(NOT GROK_SOVERSION)
set(GROK_LIBRARY_PROPERTIES
  VERSION   "${GROK_VERSION_MAJOR}.${GROK_VERSION_MINOR}.${GROK_VERSION_BUILD}"
  SOVERSION "${GROK_SOVERSION}"
)
#######################################

set(GROK_CORE_NAME grokj2k)
set(GROK_CODEC_NAME grokj2kcodec)
set(GROK_PLUGIN_NAME grokj2k_plugin)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ( (CMAKE_CXX_COMPILER_ID MATCHES "GNU") AND (CMAKE_CXX_COMPILER_VERSION LESS 11.0) )
   message(FATAL_ERROR "GNU compiler version must be at least 11.0")
endif()


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

IF(MSVC)
    string(APPEND CMAKE_CXX_FLAGS " /EHsc")
ENDIF(MSVC)

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
  set(GROK_COMPILE_OPTIONS ${GROK_COMPILE_OPTIONS} -Wall -Wextra -Wconversion -Wsign-conversion -Wunused-parameter)
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


option(GRK_BUILD_LIBPNG "Build libpng library" ON)
option(GRK_BUILD_LIBTIFF "Build libtiff library" ON)
option(GRK_BUILD_LCMS2 "Build lcms2 library" ON)
option(GRK_BUILD_JPEG "Build jpeg library" ON)
add_subdirectory(thirdparty)

# Build Library
add_subdirectory(src/lib)
option(BUILD_LUTS_GENERATOR "Build utility to generate t1_luts.h" OFF)

# Build examples
option(GRK_BUILD_CORE_EXAMPLES "Build core examples" OFF)
option(GRK_BUILD_CODEC_EXAMPLES "Build codec examples" OFF)

# examples use files from data folder
if (GRK_BUILD_CORE_EXAMPLES OR GRK_BUILD_CODEC_EXAMPLES)
	add_subdirectory(examples)
	find_path(GRK_DATA_ROOT README-GROK-TEST-DATA
	  PATHS $ENV{GRK_DATA_ROOT} ${CMAKE_SOURCE_DIR}/../grok-test-data
	  NO_CMAKE_FIND_ROOT_PATH)
endif()


# Build Applications
option(GRK_BUILD_CODEC "Build the CODEC executables" ON)
option(GRK_BUILD_PLUGIN_LOADER "Enable loading of T1 plugin" OFF)
mark_as_advanced(GRK_BUILD_PLUGIN_LOADER)

# URING support
option(URING OFF "Enable support for io_uring (requires liburing and Linux kernel >= 5.8)")
mark_as_advanced(URING)
if (URING)
  if(NOT CMAKE_SYSTEM_NAME MATCHES Linux)
    set(URING OFF CACHE BOOL "Disabled because liburing is only available on Linux" FORCE)
    message(STATUS "liburing was disabled : only available on Linux")
    set(GROK_HAVE_URING undef)
  else()
    message(STATUS "Looking for liburing")
    find_package(liburing)
    if(NOT LIBURING_FOUND)
      if(fail-on-missing)
        message(FATAL_ERROR "liburing not found and uring option required")
      else()
        message(STATUS "liburing not found. Switching off uring option")
        set(uring OFF CACHE BOOL "Disabled because liburing was not found (${uring_description})" FORCE)
      endif()
    else()
    	message(STATUS "Found liburing") 
    	set(GROK_HAVE_URING define)     
    endif()
  endif()
endif()

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
endif(PERLLIBS_FOUND)

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
if(BUILD_TESTING AND GRK_BUILD_CODEC)
	enable_testing()
	include(CTest)
	find_path(GRK_DATA_ROOT README-GROK-TEST-DATA
	  PATHS $ENV{GRK_DATA_ROOT} ${CMAKE_SOURCE_DIR}/../grok-test-data
	  NO_CMAKE_FIND_ROOT_PATH)
	add_subdirectory(tests)
endif()

# install all targets referenced as GrokTargets
if (BUILD_SHARED_LIBS AND NOT CMAKE_SYSTEM_NAME STREQUAL Emscripten)
install(EXPORT GrokTargets DESTINATION ${GROK_INSTALL_PACKAGE_DIR})
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
