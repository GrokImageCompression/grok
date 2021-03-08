# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindCMath
--------

Find the native CMath includes and library.

IMPORTED Targets
^^^^^^^^^^^^^^^^

This module defines :prop_tgt:`IMPORTED` target ``CMath::CMath``, if
CMath has been found.

Result Variables
^^^^^^^^^^^^^^^^

This module defines the following variables:

::

  CMath_INCLUDE_DIRS   - where to find CMath.h, etc.
  CMath_LIBRARIES      - List of libraries when using CMath.
  CMath_FOUND          - True if CMath found.

#]=======================================================================]


include(CheckSymbolExists)
include(CheckLibraryExists)

check_symbol_exists(pow "math.h" CMath_HAVE_LIBC_POW)
if(NOT CMath_HAVE_LIBC_POW)
    find_library(CMath_LIBRARY NAMES m)

    set(CMAKE_REQUIRED_INCLUDES_SAVE ${CMAKE_REQUIRED_INCLUDES})
    set(CMAKE_REQUIRED_INCLUDES ${CMAKE_REQUIRED_INCLUDES} ${ZSTD_INCLUDE_DIRS})
    set(CMAKE_REQUIRED_LIBRARIES_SAVE ${CMAKE_REQUIRED_LIBRARIES})
    set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES} ${CMath_LIBRARY})
    check_symbol_exists(pow "math.h" CMath_HAVE_LIBM_POW)
    set(CMAKE_REQUIRED_INCLUDES ${CMAKE_REQUIRED_INCLUDES_SAVE})
    set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES_SAVE})
endif()

set(CMath_pow FALSE)
if(CMath_HAVE_LIBC_POW OR CMath_HAVE_LIBM_POW)
    set(CMath_pow TRUE)
endif()

set(CMath_INCLUDE_DIRS)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(CMath REQUIRED_VARS CMath_pow)

if(CMath_FOUND)
    if(NOT CMath_LIBRARIES)
        if (CMath_LIBRARY)
            set(CMath_LIBRARIES ${CMath_LIBRARY})
        endif()
    endif()

    if(NOT TARGET CMath::CMath)
        if(CMath_LIBRARIES)
            add_library(CMath::CMath UNKNOWN IMPORTED)
            set_target_properties(CMath::CMath PROPERTIES
                  IMPORTED_LOCATION "${CMath_LIBRARY}")
        else()
            add_library(CMath::CMath INTERFACE IMPORTED)
        endif()
    endif()
endif()
