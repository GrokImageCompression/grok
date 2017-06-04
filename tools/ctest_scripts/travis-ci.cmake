# -----------------------------------------------------------------------------
# Travis-ci ctest script for Grok project
# This will compile/run tests/upload to cdash Grok
# Results will be available at: http://my.cdash.org/index.php?project=GROK
# -----------------------------------------------------------------------------

cmake_minimum_required(VERSION 2.8)

set( ENV{LANG} en_US.UTF-8)
if($ENV{GROK_BINARY_DIR})
	set( CTEST_DASHBOARD_ROOT  "$ENV{GROK_BINARY_DIR}" )
else()
	set( CTEST_DASHBOARD_ROOT  "$ENV{PWD}/build" )
endif()

if("$ENV{TRAVIS_OS_NAME}" STREQUAL "windows")
	set( CTEST_CMAKE_GENERATOR "NMake Makefiles")
	set( CTEST_BUILD_COMMAND   "nmake" )
	set( JPYLYZER_EXT          "exe"  )
else()
	set( CTEST_CMAKE_GENERATOR "Unix Makefiles")   # Always makefile in travis-ci environment
	set( CCFLAGS_WARNING "-Wall -Wextra -Wconversion -Wno-unused-parameter -Wdeclaration-after-statement -Werror=declaration-after-statement")
	set( JPYLYZER_EXT          "py"  )
endif()

if ("$ENV{GROK_BUILD_CONFIGURATION}" STREQUAL "")
  set( CTEST_BUILD_CONFIGURATION "Release")
else()
	set( CTEST_BUILD_CONFIGURATION "$ENV{GROK_BUILD_CONFIGURATION}")
endif()

if ("$ENV{GROK_SITE}" STREQUAL "")
  set( CTEST_SITE "Unknown")
else()
	set( CTEST_SITE "$ENV{GROK_SITE}")
endif()

if ("$ENV{GROK_BUILDNAME}" STREQUAL "")
  set( CTEST_BUILD_NAME "Unknown-${CTEST_BUILD_CONFIGURATION}")
else()
	set( CTEST_BUILD_NAME "$ENV{GROK_BUILDNAME}")
endif()

if (NOT "$ENV{GROK_CI_ARCH}" STREQUAL "")
	if (APPLE)
	  set(CCFLAGS_ARCH "-arch $ENV{GROK_CI_ARCH}")
	else()
		if ("$ENV{GROK_CI_ARCH}" MATCHES "^i[3-6]86$")
			set(CCFLAGS_ARCH "-m32 -march=$ENV{GROK_CI_ARCH}")
		elseif ("$ENV{GROK_CI_ARCH}" STREQUAL "x86_64")
			set(CCFLAGS_ARCH "-m64")
		endif()
	endif()
endif()

if ("$ENV{GROK_CI_ASAN}" STREQUAL "1")
	set(GROK_HAS_MEMCHECK TRUE)
	set(CTEST_MEMORYCHECK_TYPE "AddressSanitizer")
	set(CCFLAGS_ARCH "${CCFLAGS_ARCH} -O1 -g -fsanitize=address -fno-omit-frame-pointer")
endif()

if("$ENV{CC}" MATCHES ".*mingw.*")
	# We are trying to use mingw
	if ("$ENV{GROK_CI_ARCH}" MATCHES "^i[3-6]86$")
		set(CTEST_CONFIGURE_OPTIONS "-DCMAKE_TOOLCHAIN_FILE=${CTEST_SCRIPT_DIRECTORY}/toolchain-mingw32.cmake")
	else()
		set(CTEST_CONFIGURE_OPTIONS "-DCMAKE_TOOLCHAIN_FILE=${CTEST_SCRIPT_DIRECTORY}/toolchain-mingw64.cmake")
	endif()
endif()

if(NOT "$ENV{GROK_CI_SKIP_TESTS}" STREQUAL "1")
	# To execute part of the encoding test suite, kakadu binaries are needed to decode encoded image and compare
	# it to the baseline. Kakadu binaries are freely available for non-commercial purposes
	# at http://www.kakadusoftware.com.
	# Here's the copyright notice from kakadu:
	# Copyright is owned by NewSouth Innovations Pty Limited, commercial arm of the UNSW Australia in Sydney.
	# You are free to trial these executables and even to re-distribute them,
	# so long as such use or re-distribution is accompanied with this copyright notice and is not for commercial gain.
	# Note: Binaries can only be used for non-commercial purposes.
	if ("$ENV{GROK_NONCOMMERCIAL}" STREQUAL "1" )
		set(KDUPATH $ENV{PWD}/kdu)
		if("$ENV{TRAVIS_OS_NAME}" STREQUAL "windows")
			set(ENV{PATH} "$ENV{PATH};${KDUPATH}")
		else()
			set(ENV{LD_LIBRARY_PATH} ${KDUPATH})
			set(ENV{PATH} $ENV{PATH}:${KDUPATH})
		endif()
	endif()
	set(BUILD_TESTING "TRUE")
else()
	set(BUILD_TESTING "FALSE")
endif(NOT "$ENV{GROK_CI_SKIP_TESTS}" STREQUAL "1")


if("$ENV{GROK_CI_CHECK_STYLE}" STREQUAL "1")
	set(BUILD_ASTYLE "TRUE")
else()
	set(BUILD_ASTYLE "FALSE")
endif("$ENV{GROK_CI_CHECK_STYLE}" STREQUAL "1")

# Options
set( CACHE_CONTENTS "

# Build kind
CMAKE_BUILD_TYPE:STRING=${CTEST_BUILD_CONFIGURATION}

# Warning level
CMAKE_C_FLAGS:STRING= ${CCFLAGS_ARCH} ${CCFLAGS_WARNING}

# For astyle
CMAKE_CXX_FLAGS:STRING= ${CCFLAGS_ARCH}

# Use to activate the test suite
BUILD_TESTING:BOOL=${BUILD_TESTING}

# Build Thirdparty, useful but not required for test suite
BUILD_THIRDPARTY:BOOL=TRUE

# JPEG2000 test files are available with git clone https://github.com/GrokImageCompression/grok-test-data
GROK_DATA_ROOT:PATH=$ENV{PWD}/data

# jpylyzer is available with on GitHub: https://github.com/openpreserve/jpylyzer
JPYLYZER_EXECUTABLE=$ENV{PWD}/jpylyzer/jpylyzer.${JPYLYZER_EXT}

# Enable astyle
WITH_ASTYLE:BOOL=${BUILD_ASTYLE}
" )

#---------------------
#1. Grok specific:
set( CTEST_PROJECT_NAME	"GROK" )
if(NOT EXISTS $ENV{GROK_SOURCE_DIR})
	message(FATAL_ERROR "GROK_SOURCE_DIR not defined or does not exist:$ENV{GROK_SOURCE_DIR}")
endif()
set( CTEST_SOURCE_DIRECTORY	"$ENV{GROK_SOURCE_DIR}")
set( CTEST_BINARY_DIRECTORY	"${CTEST_DASHBOARD_ROOT}")

#---------------------
# Files to submit to the dashboard
set (CTEST_NOTES_FILES
${CTEST_SCRIPT_DIRECTORY}/${CTEST_SCRIPT_NAME}
${CTEST_BINARY_DIRECTORY}/CMakeCache.txt )

ctest_empty_binary_directory( "${CTEST_BINARY_DIRECTORY}" )
file(WRITE "${CTEST_BINARY_DIRECTORY}/CMakeCache.txt" "${CACHE_CONTENTS}")

# Perform a Experimental build
ctest_start(Experimental)
#ctest_update(SOURCE "${CTEST_SOURCE_DIRECTORY}")
ctest_configure(BUILD "${CTEST_BINARY_DIRECTORY}" OPTIONS "${CTEST_CONFIGURE_OPTIONS}")
ctest_read_custom_files(${CTEST_BINARY_DIRECTORY})
ctest_build(BUILD "${CTEST_BINARY_DIRECTORY}")
if(NOT "$ENV{GROK_CI_SKIP_TESTS}" STREQUAL "1")
	ctest_test(BUILD "${CTEST_BINARY_DIRECTORY}" PARALLEL_LEVEL 2)
	if(GROK_HAS_MEMCHECK)
		ctest_memcheck(BUILD "${CTEST_BINARY_DIRECTORY}" PARALLEL_LEVEL 2)
	endif()
endif()
if ("$ENV{GROK_DO_SUBMIT}" STREQUAL "1")
	ctest_submit()
endif()
# Do not clean, we'll parse the log for known failure
#ctest_empty_binary_directory( "${CTEST_BINARY_DIRECTORY}" )
