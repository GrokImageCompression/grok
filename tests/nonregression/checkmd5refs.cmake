#
#    Copyright (C) 2016-2026 Grok Image Compression Inc.
#
#    This source code is free software: you can redistribute it and/or  modify
#    it under the terms of the GNU Affero General Public License, version 3,
#    as published by the Free Software Foundation.
#
#    This source code is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU Affero General Public License for more details.
#
#    You should have received a copy of the GNU Affero General Public License
#    along with this program.  If not, see <http://www.gnu.org/licenses/>.
#


# check md5 refs
#
# This script will be used to make sure we never introduce a regression on any
# of the nonregression file.
#
# The approach is relatively simple, we compute a md5sum for each of the decode
# file. Anytime the md5sum is different from the reference one, we assume
# something went wrong and simply fails.  of course if could happen during the
# course of Grok development that the internals are changed that impact the
# decoding process that the output would be bitwise different (while PSNR would
# be kept identical).

# Another more conventional approach is to store the generated output from
# Grok however storing the full generated output is generally useless since
# we do not really care about the exact pixel value, we simply need to known
# when a code change impact output generation. 

# This script expects:
# REFFILE:      Path to the canonical md5refs.txt file
# OUTFILENAME:  The name of the generated file to check
# PLATFORM_KEY: (optional) Platform key string (e.g. "ubuntu-dynamic",
#               "macos-static") used to select a platform-specific
#               md5refs-<PLATFORM_KEY>.txt that overrides REFFILE when it exists.
#               This lets each OS ⨉ linkage combination carry its own checksums
#               for floating-point-sensitive codecs (e.g. 9/7 wavelet) without
#               requiring separate branches.

# Resolve the active reference file: prefer platform-specific override.
# Platform-specific files only contain entries that *differ* from canonical;
# we fall back to the canonical file for entries not found in the override.
set(ACTIVE_REFFILE "${REFFILE}")
set(FALLBACK_REFFILE "")
set(_blacklist "")
if(PLATFORM_KEY)
    get_filename_component(_refdir  "${REFFILE}" DIRECTORY)
    get_filename_component(_refname "${REFFILE}" NAME_WE)
    get_filename_component(_refext  "${REFFILE}" EXT)
    set(_platform_reffile "${_refdir}/${_refname}-${PLATFORM_KEY}${_refext}")
    if(EXISTS "${_platform_reffile}")
        set(ACTIVE_REFFILE "${_platform_reffile}")
        set(FALLBACK_REFFILE "${REFFILE}")
        message(STATUS "Using platform-specific MD5 refs: ${_platform_reffile}")
    endif()
    # Load platform-specific blacklist (files to skip MD5 checking entirely).
    # Useful for outputs that are non-deterministic across builds on a given
    # platform (e.g. floating-point non-determinism on macOS ARM).
    set(_blacklist_file "${_refdir}/md5blacklist-${PLATFORM_KEY}${_refext}")
    if(EXISTS "${_blacklist_file}")
        file(STRINGS "${_blacklist_file}" _blacklist_raw)
        foreach(_bl_line ${_blacklist_raw})
            string(REGEX REPLACE "#.*$" "" _bl_line "${_bl_line}")
            string(STRIP "${_bl_line}" _bl_line)
            if(_bl_line)
                list(APPEND _blacklist "${_bl_line}")
            endif()
        endforeach()
        if(_blacklist)
            message(STATUS "Using platform MD5 blacklist: ${_blacklist_file} (${_blacklist})")
        endif()
    endif()
endif()

# Extract filename without extension
get_filename_component(OUTFILENAME_NAME_WE ${OUTFILENAME} NAME_WE)

# Search for the files with different extensions
file(GLOB globfiles
    "Temporary/${OUTFILENAME_NAME_WE}*.pgx"
    "Temporary/${OUTFILENAME_NAME_WE}*.pgm"
    "Temporary/${OUTFILENAME_NAME_WE}*.png"
    "Temporary/${OUTFILENAME_NAME_WE}*.bmp"
    "Temporary/${OUTFILENAME_NAME_WE}*.tif")

# Check if no files are found
if(NOT globfiles)
    message(SEND_ERROR "Could not find output PGX/PGM/PNG/BMP/TIF files: ${OUTFILENAME_NAME_WE}")
endif()

# Read the active reference file content
file(READ ${ACTIVE_REFFILE} ref_content)

# Read the fallback (canonical) reference file if we have a platform override
set(fallback_content "")
if(FALLBACK_REFFILE)
    file(READ ${FALLBACK_REFFILE} fallback_content)
endif()

# Loop through all globbed files and compare MD5 hash
foreach(file_path ${globfiles})
  # Get MD5 hash and filename
  file(MD5 ${file_path} file_md5)
  get_filename_component(file_name ${file_path} NAME)

  # Skip files that are blacklisted for this platform.
  # A blacklist entry of "*" means skip ALL md5 checks for this platform.
  set(_skip_blacklisted FALSE)
  if(_blacklist)
      list(FIND _blacklist "*" _wildcard_idx)
      if(NOT _wildcard_idx EQUAL -1)
          set(_skip_blacklisted TRUE)
      elseif(file_name IN_LIST _blacklist)
          set(_skip_blacklisted TRUE)
      endif()
  endif()
  if(_skip_blacklisted)
    message(STATUS "Blacklisted (skipped): [${file_md5}  ${file_name}]")
    continue()
  endif()

  # Construct the expected output format: "md5_hash  filename"
  set(expected_entry "${file_md5}  ${file_name}")

  # Check if the expected entry exists in the reference content,
  # falling back to canonical if not found in platform-specific file
  if(ref_content MATCHES "${expected_entry}")
    message(STATUS "Match: [${expected_entry}]")
  elseif(fallback_content AND fallback_content MATCHES "${expected_entry}")
    message(STATUS "Match (canonical fallback): [${expected_entry}]")
  else()
    message(SEND_ERROR "Mismatch: Expected [${expected_entry}] was not found in reference file")
  endif()
endforeach()
