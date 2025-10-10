# Copyright (C) 2016-2025 Grok Image Compression Inc.
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


# NON-REGRESSION TESTS ON THIS DATASET LOCATED AT
# ${GRK_DATA_ROOT}/input/nonregression
cmake_minimum_required(VERSION 3.16.0)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Temporary)
set(TEMP ${CMAKE_CURRENT_BINARY_DIR}/Temporary)
set(BASELINE_NR ${GRK_DATA_ROOT}/baseline/nonregression)
set(INPUT_NR ${GRK_DATA_ROOT}/input/nonregression)
set(INPUT_NR_PATH ${INPUT_NR})
set(TEMP_PATH ${TEMP})
set(INPUT_CONF_PATH ${GRK_DATA_ROOT}/input/conformance)
set(BASELINE_CONF_PATH ${GRK_DATA_ROOT}/baseline/conformance)

# need kdu_expand if available
find_package(KAKADU)

#########################################################################
# FUNCTION TO ADD DUMP TESTS
function(add_dump_test INPUT_FILENAME TEMP BASELINE_NR BLACKLIST_JPEG2000)
  # Extract filename and filename without extension (shortest extension)
  get_filename_component(INPUT_FILENAME_NAME ${INPUT_FILENAME} NAME)
  string(FIND ${INPUT_FILENAME_NAME} "." SHORTEST_EXT_POS REVERSE)
  string(SUBSTRING ${INPUT_FILENAME_NAME} 0 ${SHORTEST_EXT_POS} INPUT_FILENAME_NAME_WE)

  # Check if the file is in the blacklist
  list(FIND BLACKLIST_JPEG2000 ${INPUT_FILENAME_NAME} BLACKLIST_INDEX)
  set(IS_BLACKLISTED FALSE)
  if(BLACKLIST_INDEX GREATER -1)
    set(IS_BLACKLISTED TRUE)
  endif()

  # Add the dump test
  add_test(NAME NR-${INPUT_FILENAME_NAME}-dump
           COMMAND grk_dump
           -i ${INPUT_FILENAME}
           -o ${TEMP}/${INPUT_FILENAME_NAME}.txt)

  if(IS_BLACKLISTED)
    set_tests_properties(NR-${INPUT_FILENAME_NAME}-dump PROPERTIES WILL_FAIL TRUE)
  else()
    # Add the compare test if the file is not blacklisted
    add_test(NAME NR-${INPUT_FILENAME_NAME}-compare_dump2base
             COMMAND compare_dump_files
             -b ${BASELINE_NR}/${INPUT_FILENAME_NAME_WE}.txt
             -t ${TEMP}/${INPUT_FILENAME_NAME}.txt)
    set_tests_properties(NR-${INPUT_FILENAME_NAME}-compare_dump2base
                         PROPERTIES DEPENDS NR-${INPUT_FILENAME_NAME}-dump)
  endif()
endfunction()

#########################################################################
# GENERATION OF THE TEST SUITE (DUMP)
# Load and clean blacklist files
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/blacklist_jpeg2000_tmp.txt" BLACKLIST_JPEG2000_TMP_RAW)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/blacklist_jpeg2000.txt" BLACKLIST_JPEG2000_ADDITIONAL_RAW)

# Process raw lists to remove comments, whitespace, and empty lines
set(BLACKLIST_JPEG2000_TMP "")
foreach(line ${BLACKLIST_JPEG2000_TMP_RAW})
  # Remove comments (anything after #) and trim whitespace
  string(REGEX REPLACE "#.*$" "" line "${line}")
  string(STRIP "${line}" line)
  if(line)
    list(APPEND BLACKLIST_JPEG2000_TMP "${line}")
  endif()
endforeach()

set(BLACKLIST_JPEG2000_ADDITIONAL "")
foreach(line ${BLACKLIST_JPEG2000_ADDITIONAL_RAW})
  string(REGEX REPLACE "#.*$" "" line "${line}")
  string(STRIP "${line}" line)
  if(line)
    list(APPEND BLACKLIST_JPEG2000_ADDITIONAL "${line}")
  endif()
endforeach()

set(BLACKLIST_JPEG2000 ${BLACKLIST_JPEG2000_TMP} ${BLACKLIST_JPEG2000_ADDITIONAL})

file(GLOB_RECURSE DATA_NR_LIST
     "${INPUT_NR}/*.j2k"
     "${INPUT_NR}/*.j2c"
     "${INPUT_NR}/*.jp2"
     "${INPUT_NR}/*.jpc"
     "${INPUT_NR}/*.jph"
     "${INPUT_NR}/*.jhc"
)

foreach(INPUT_FILENAME ${DATA_NR_LIST})
  add_dump_test("${INPUT_FILENAME}" "${TEMP}" "${BASELINE_NR}" "${BLACKLIST_JPEG2000}")
endforeach()

#########################################################################
# GENERATION OF THE TEST SUITE (DECODE AND ENCODE)

# Function to add encoder tests
function(add_encoder_test EXE_NAME ARGS INPUT_FILENAME OUTPUT_FILENAME TEST_NUM IS_FAILING)
  get_filename_component(INPUT_FILENAME_NAME ${INPUT_FILENAME} NAME)
  get_filename_component(OUTPUT_FILENAME_NAME_WE ${OUTPUT_FILENAME} NAME_WE)

  # Determine if it's a no_raw or lossless variant
  string(REGEX MATCH "^grk_compress_no_raw$|^grk_compress_no_raw_lossless$" NO_RAW ${EXE_NAME})
  string(REGEX MATCH "grk_compress_no_raw_lossless" LOSSLESS ${EXE_NAME})

  # Add the encoding test
  add_test(NAME NR-ENC-${INPUT_FILENAME_NAME}-${TEST_NUM}-encode
           COMMAND grk_compress ${ARGS})

  if(IS_FAILING)
    set_tests_properties(NR-ENC-${INPUT_FILENAME_NAME}-${TEST_NUM}-encode PROPERTIES WILL_FAIL TRUE)
  else()
    # Dump the encoded file
    add_test(NAME NR-ENC-${INPUT_FILENAME_NAME}-${TEST_NUM}-dump
             COMMAND grk_dump -i ${OUTPUT_FILENAME} -o ${OUTPUT_FILENAME}-ENC-${TEST_NUM}.txt)
    set_tests_properties(NR-ENC-${INPUT_FILENAME_NAME}-${TEST_NUM}-dump
                         PROPERTIES DEPENDS NR-ENC-${INPUT_FILENAME_NAME}-${TEST_NUM}-encode)

    # Compare the dump file with the baseline
    add_test(NAME NR-ENC-${INPUT_FILENAME_NAME}-${TEST_NUM}-compare_dump2base
             COMMAND compare_dump_files
                     -b ${BASELINE_NR}/${OUTPUT_FILENAME_NAME_WE}-ENC-${TEST_NUM}.txt
                     -t ${OUTPUT_FILENAME}-ENC-${TEST_NUM}.txt)
    set_tests_properties(NR-ENC-${INPUT_FILENAME_NAME}-${TEST_NUM}-compare_dump2base
                         PROPERTIES DEPENDS NR-ENC-${INPUT_FILENAME_NAME}-${TEST_NUM}-dump)

    # Decode with Kakadu if available
    if(KDU_EXPAND_EXECUTABLE)
      add_test(NAME NR-ENC-${INPUT_FILENAME_NAME}-${TEST_NUM}-decode-ref
               COMMAND ${KDU_EXPAND_EXECUTABLE}
                       -i ${OUTPUT_FILENAME}
                       -o ${OUTPUT_FILENAME}.raw)
      set_tests_properties(NR-ENC-${INPUT_FILENAME_NAME}-${TEST_NUM}-decode-ref
                           PROPERTIES DEPENDS NR-ENC-${INPUT_FILENAME_NAME}-${TEST_NUM}-encode)

      if(NOT NO_RAW)
        add_test(NAME NR-ENC-${INPUT_FILENAME_NAME}-${TEST_NUM}-compare_dec-ref-out2base
                 COMMAND compare_raw_files
                         -b ${BASELINE_NR}/${OUTPUT_FILENAME_NAME_WE}-ENC-${TEST_NUM}.raw
                         -t ${OUTPUT_FILENAME}.raw)
        set_tests_properties(NR-ENC-${INPUT_FILENAME_NAME}-${TEST_NUM}-compare_dec-ref-out2base
                             PROPERTIES DEPENDS NR-ENC-${INPUT_FILENAME_NAME}-${TEST_NUM}-decode-ref)
      endif()
    endif()

    # Lossless compression: decompress & compare
    list(LENGTH ARGS ARG_COUNT)
    if((ARG_COUNT EQUAL 4) OR LOSSLESS)
      if(${INPUT_FILENAME_NAME} MATCHES "\\.tif$")
        add_test(NAME NR-ENC-${INPUT_FILENAME_NAME}-${TEST_NUM}-lossless-decode
                 COMMAND grk_decompress -i ${OUTPUT_FILENAME} -o ${OUTPUT_FILENAME}.lossless.tif)
        set_tests_properties(NR-ENC-${INPUT_FILENAME_NAME}-${TEST_NUM}-lossless-decode
                             PROPERTIES DEPENDS NR-ENC-${INPUT_FILENAME_NAME}-${TEST_NUM}-encode)

        add_test(NAME NR-ENC-${INPUT_FILENAME_NAME}-${TEST_NUM}-lossless-compare
                 COMMAND compare_images -b ${INPUT_FILENAME} -t ${OUTPUT_FILENAME}.lossless.tif -n 1 -d)
        set_tests_properties(NR-ENC-${INPUT_FILENAME_NAME}-${TEST_NUM}-lossless-compare
                             PROPERTIES DEPENDS NR-ENC-${INPUT_FILENAME_NAME}-${TEST_NUM}-lossless-decode)
      endif()
    endif()
  endif()
endfunction()

# Function to add decoder tests
function(add_decoder_test EXE_NAME ARGS INPUT_FILENAME OUTPUT_FILENAME TEST_NUM IS_FAILING)
  get_filename_component(INPUT_FILENAME_NAME ${INPUT_FILENAME} NAME)

  # Determine if it's core_decompress
  string(REGEX MATCH "^core_decompress$" IS_CORE_DEC ${EXE_NAME})

  # Add the decoding test
  if(IS_CORE_DEC)
    add_test(NAME NR-DEC-${INPUT_FILENAME_NAME}-${TEST_NUM}-core-decode
             COMMAND core_decompress ${ARGS})
  else()
    add_test(NAME NR-DEC-${INPUT_FILENAME_NAME}-${TEST_NUM}-decode
             COMMAND grk_decompress ${ARGS})
  endif()

  if(IS_FAILING)
    if(IS_CORE_DEC)
      set_tests_properties(NR-DEC-${INPUT_FILENAME_NAME}-${TEST_NUM}-core-decode PROPERTIES WILL_FAIL TRUE)
    else()
      set_tests_properties(NR-DEC-${INPUT_FILENAME_NAME}-${TEST_NUM}-decode PROPERTIES WILL_FAIL TRUE)
    endif()
  else()
    if(NOT IS_CORE_DEC)
      # MD5 Check: Verify the decoded output against registered md5 checksums
      add_test(NAME NR-DEC-${INPUT_FILENAME_NAME}-${TEST_NUM}-decode-md5
               COMMAND ${CMAKE_COMMAND}
                       -DREFFILE:STRING=${CMAKE_CURRENT_SOURCE_DIR}/md5refs.txt
                       -DOUTFILENAME:STRING=${OUTPUT_FILENAME}
                       -P ${CMAKE_CURRENT_SOURCE_DIR}/checkmd5refs.cmake)
      set_tests_properties(NR-DEC-${INPUT_FILENAME_NAME}-${TEST_NUM}-decode-md5
                           PROPERTIES DEPENDS NR-DEC-${INPUT_FILENAME_NAME}-${TEST_NUM}-decode)
    endif()
  endif()
endfunction()

# Configure the primary test suite file
configure_file("test_suite.ctest.in"
               "${CMAKE_CURRENT_BINARY_DIR}/test_suite.ctest"
               @ONLY)

# Read the primary test suite file into a list
file(STRINGS "${CMAKE_CURRENT_BINARY_DIR}/test_suite.ctest" TEST_CMD_LINE_LIST)

# Find any additional test suite files (excluding the primary one)
file(GLOB TEST_SUITE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.ctest.in")

# Loop through and configure any additional test suite files
foreach(TEST_SUITE_FILE ${TEST_SUITE_FILES})
  get_filename_component(TEST_SUITE_FILENAME ${TEST_SUITE_FILE} NAME)
  if(NOT TEST_SUITE_FILENAME STREQUAL "test_suite.ctest.in")
    get_filename_component(TEST_SUITE_FILE_SUB ${TEST_SUITE_FILE} NAME_WE)
    configure_file("${TEST_SUITE_FILE}"
                   "${CMAKE_CURRENT_BINARY_DIR}/${TEST_SUITE_FILE_SUB}.ctest"
                   @ONLY)
    file(STRINGS "${CMAKE_CURRENT_BINARY_DIR}/${TEST_SUITE_FILE_SUB}.ctest" TEST_CMD_LINE_LIST_TEMP)
    list(APPEND TEST_CMD_LINE_LIST ${TEST_CMD_LINE_LIST_TEMP})
  endif()
endforeach()

if(NOT TEST_CMD_LINE_LIST)
  message(FATAL_ERROR "No valid test suite files found. Ensure at least one test suite is available.")
endif()

set(nonregression_filenames_used)
set(IT_TEST_ENC 0)
set(IT_TEST_DEC 0)
foreach(TEST_CMD_LINE ${TEST_CMD_LINE_LIST})
  set(IGNORE_LINE_FOUND 0)
  string(REPLACE " " ";" CMD_ARG_LIST ${TEST_CMD_LINE})
  list(GET CMD_ARG_LIST 0 EXE_NAME)
  if(EXE_NAME)
    string(REGEX MATCH "^#" IGNORE_LINE_FOUND ${EXE_NAME})
  endif()
  if(IGNORE_LINE_FOUND OR NOT EXE_NAME)
    #message(STATUS "Current line is ignored: ${TEST_CMD_LINE}")
  else()
    set(FAILED_TEST_FOUND 0)  # Initialize to 0
    string(REGEX MATCH "^!" IS_FAILING ${EXE_NAME})
    if(IS_FAILING)
      set(FAILED_TEST_FOUND 1)
      string(SUBSTRING ${EXE_NAME} 1 -1 EXE_NAME)  # Remove '!' from !grk_compress or !core_decompress
      list(REMOVE_AT CMD_ARG_LIST 0)
    endif()

    string(REGEX MATCH "^grk_compress$|^grk_compress_no_raw$|^grk_compress_no_raw_lossless$|^grk_decompress$|^core_decompress$" EXE_NAME_FOUND ${EXE_NAME})
    if(NOT EXE_NAME_FOUND)
      message(FATAL_ERROR "${EXE_NAME} is not the right executable name (try grk_compress, grk_decompress, or core_decompress)")
    endif()
    list(REMOVE_AT CMD_ARG_LIST 0)

    set(CMD_ARG_LIST_2 "")
    set(ARG_POS 0)
    set(INPUT_ARG_POS 0)
    set(OUTPUT_ARG_POS 0)
    foreach(CMD_ARG_ELT ${CMD_ARG_LIST})
      math(EXPR ARG_POS "${ARG_POS}+1")
      string(COMPARE EQUAL ${CMD_ARG_ELT} "-i" INPUT_ARG_FOUND)
      if(INPUT_ARG_FOUND)
        set(INPUT_ARG_POS ${ARG_POS})
        set(INPUT_ARG_FOUND 0)
      endif()
      string(COMPARE EQUAL ${CMD_ARG_ELT} "-o" OUTPUT_ARG_FOUND)
      if(OUTPUT_ARG_FOUND)
        set(OUTPUT_ARG_POS ${ARG_POS})
        set(OUTPUT_ARG_FOUND 0)
      endif()
      list(APPEND CMD_ARG_LIST_2 ${CMD_ARG_ELT})
    endforeach()

    list(GET CMD_ARG_LIST_2 ${INPUT_ARG_POS} INPUT_FILENAME)
    get_filename_component(INPUT_FILENAME_NAME ${INPUT_FILENAME} NAME)
    get_filename_component(INPUT_FILENAME_NAME_WE ${INPUT_FILENAME_NAME} NAME_WE)
    if(OUTPUT_ARG_POS GREATER 0)
      list(GET CMD_ARG_LIST_2 ${OUTPUT_ARG_POS} OUTPUT_FILENAME)
      get_filename_component(OUTPUT_FILENAME_NAME_WE ${OUTPUT_FILENAME} NAME_WE)
    else()
      set(OUTPUT_FILENAME "")
    endif()

    if(NOT INPUT_FILENAME)
      message(FATAL_ERROR "Missing input file (-i) in command: ${TEST_CMD_LINE}")
    endif()
    if(EXE_NAME MATCHES "^grk_decompress$" AND NOT OUTPUT_FILENAME)
      message(FATAL_ERROR "Missing output file (-o) required for ${EXE_NAME} in command: ${TEST_CMD_LINE}")
    endif()
    if(EXE_NAME MATCHES "^grk_compress.*$" AND NOT OUTPUT_FILENAME)
      message(FATAL_ERROR "Missing output file (-o) required for ${EXE_NAME} in command: ${TEST_CMD_LINE}")
    endif()

    #-----
    # Add the test suite corresponding to a line command in the file
    #-----

    if(EXE_NAME MATCHES "^grk_compress.*$")
      math(EXPR IT_TEST_ENC "${IT_TEST_ENC}+1")
      add_encoder_test("${EXE_NAME}" "${CMD_ARG_LIST_2}" "${INPUT_FILENAME}" "${OUTPUT_FILENAME}" ${IT_TEST_ENC} ${FAILED_TEST_FOUND})
    else()
      math(EXPR IT_TEST_DEC "${IT_TEST_DEC}+1")
      add_decoder_test("${EXE_NAME}" "${CMD_ARG_LIST_2}" "${INPUT_FILENAME}" "${OUTPUT_FILENAME}" ${IT_TEST_DEC} ${FAILED_TEST_FOUND})

      string(FIND ${INPUT_FILENAME} "nonregression" nr_pos)
      if(${nr_pos} GREATER 0)
        list(APPEND nonregression_filenames_used ${INPUT_FILENAME_NAME})
      endif()
    endif()
  endif()
endforeach()

set(existing_filenames "")
foreach(f ${DATA_NR_LIST})
  get_filename_component(filename ${f} NAME)
  if(NOT filename IN_LIST nonregression_filenames_used)
    list(APPEND existing_filenames ${filename})
  endif()
endforeach()

foreach(found_but_no_test ${existing_filenames})
  add_test(NAME Found-But-No-Test-${found_but_no_test}
           COMMAND ${CMAKE_COMMAND} -E echo "${found_but_no_test}")
  set_tests_properties(Found-But-No-Test-${found_but_no_test} PROPERTIES WILL_FAIL TRUE)
endforeach()