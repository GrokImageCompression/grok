#
#    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

# This script expect two inputs
# REFFILE: Path to the md5sum.txt file
# OUTFILENAME: The name of the generated file we want to check The script will
# check whether a PGX or a PNG file was generated in the test suite (computed
# from OUTFILENAME)
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

# Read the reference file (REFFILE) content to variable
file(READ ${REFFILE} ref_content)

# Loop through all globbed files and compare MD5 hash
foreach(file_path ${globfiles})
  # Get MD5 hash and filename
  file(MD5 ${file_path} file_md5)
  get_filename_component(file_name ${file_path} NAME)

  # Construct the expected output format: "md5_hash  filename"
  set(expected_entry "${file_md5}  ${file_name}")

  # Check if the expected entry exists in the reference content
  if(ref_content MATCHES "${expected_entry}")
    message(STATUS "Match: [${expected_entry}]")
  else()
    message(SEND_ERROR "Mismatch: Expected [${expected_entry}] was not found in reference file")
  endif()
endforeach()
