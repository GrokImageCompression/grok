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
# GENERATION OF THE TEST SUITE (DUMP)
# Dump all files with the selected extension inside the input directory
# technically grk_dump should simply parse these ones, since syntax is ok.
set(BLACKLIST_JPEG2000_TMP
    2539.pdf.SIGFPE.706.1712.jp2
    0290cb77c5df21828fa74cf2ab2c84d8.SIGFPE.d25.31.jp2
    26ccf3651020967f7778238ef5af08af.SIGFPE.d25.527.jp2
    4035.pdf.SIGSEGV.d8b.3375.jp2
    451.pdf.SIGSEGV.5b5.3723.jp2
    451.pdf.SIGSEGV.f4c.3723.jp2
    4149.pdf.SIGSEGV.cf7.3501.jp2
    1336.pdf.asan.47.376.jp2
    openjpeg_1125.jp2
    issue395.jp2
    issue397.jp2
    issue733.jp2
    3635.pdf.asan.77.2930.jp2
    issue165.jp2
    edf_c2_1178956.jp2
    edf_c2_1000290.jp2
    #edf_c2_1000691.jp2 # ok
    edf_c2_20.jp2
    edf_c2_1377017.jp2
    edf_c2_1002767.jp2
    edf_c2_10025.jp2
    edf_c2_1000234.jp2
    edf_c2_225881.jp2
    edf_c2_1000671.jp2
    edf_c2_1013627.jp2
    edf_c2_1015644.jp2
    edf_c2_101463.jp2
    edf_c2_1674177.jp2
    edf_c2_1673169.jp2
	issue418.jp2
    issue429.jp2
    issue427-null-image-size.jp2
    issue427-illegal-tile-offset.jp2
	issue495.jp2
    issue820.jp2
	issue774.jp2
	issue823.jp2
	issue826.jp2)

# Define a list of file which should be gracefully rejected:
set(BLACKLIST_JPEG2000
    ${BLACKLIST_JPEG2000_TMP}
    broken1.jp2
    broken2.jp2
    broken3.jp2
    broken4.jp2
    gdal_fuzzer_assert_in_j2k_read_SQcd_SQcc.patch.jp2	
    gdal_fuzzer_check_comp_dx_dy.jp2
    gdal_fuzzer_check_number_of_tiles.jp2
    gdal_fuzzer_unchecked_numresolutions.jp2
    mem-b2ace68c-1381.jp2
    1851.pdf.SIGSEGV.ce9.948.jp2
    1888.pdf.asan.35.988.jp2
    2977.pdf.asan.67.2198.jp2
    issue362-2863.jp2 #kdu_jp2info ok
    issue362-2866.jp2
    issue362-2894.jp2
    issue400.jp2 #kdu_jp2info ok
    issue364-38.jp2
    issue364-903.jp2 #kdu_jp2info ok
    issue393.jp2 #kdu_jp2info ok
    issue408.jp2 #kdu_jp2info ok
    issue420.jp2 #kdu_jp2info ok
    27ac957758a35d00d6765a0c86350d9c.SIGFPE.d25.537.jpc #kdu_jp2info crash
    3672da2f1f67bbecad27d7181b4e9d7c.SIGFPE.d25.805.jpc #kdu_jp2info crash
    issue476.jp2 #kdu_jp2info ok
    issue475.jp2 #kdu_jp2info ok
    issue413.jp2 #kdu_jp2info ok
    issue725.jp2)

file(GLOB_RECURSE DATA_NR_LIST
     "${INPUT_NR}/*.j2k"
     "${INPUT_NR}/*.j2c"
     "${INPUT_NR}/*.jp2"
     "${INPUT_NR}/*.jpc")

foreach(INPUT_FILENAME ${DATA_NR_LIST})
  get_filename_component(INPUT_FILENAME_NAME ${INPUT_FILENAME} NAME)
  #get_filename_component(INPUT_FILENAME_NAME_WE ${INPUT_FILENAME_NAME} NAME_WE)
  # cannot use longest extension function, since some names contain multiple
  # dots. Instead write our own shortest extension function:
  string(FIND ${INPUT_FILENAME_NAME} "." SHORTEST_EXT_POS REVERSE)
  string(SUBSTRING ${INPUT_FILENAME_NAME} 0 ${SHORTEST_EXT_POS} INPUT_FILENAME_NAME_WE)
  string(REGEX MATCH ${INPUT_FILENAME_NAME} bad_jpeg2000 ${BLACKLIST_JPEG2000})

  # Dump the input image
  add_test(NAME NR-${INPUT_FILENAME_NAME}-dump
           COMMAND grk_dump
  	       -i ${INPUT_FILENAME}
  	       -o ${TEMP}/${INPUT_FILENAME_NAME}.txt
  	       -v)
  	
  if(bad_jpeg2000)
    set_tests_properties(NR-${INPUT_FILENAME_NAME}-dump
                         PROPERTIES WILL_FAIL TRUE)
  else()
    # Compare the dump output with the baseline
    add_test(NAME NR-${INPUT_FILENAME_NAME}-compare_dump2base
  	        COMMAND compare_dump_files
            -b ${BASELINE_NR}/${INPUT_FILENAME_NAME_WE}.txt
            -t ${TEMP}/${INPUT_FILENAME_NAME}.txt)
    set_tests_properties(NR-${INPUT_FILENAME_NAME}-compare_dump2base
                         PROPERTIES DEPENDS
                         NR-${INPUT_FILENAME_NAME}-dump)
  endif()
endforeach()

#########################################################################
# GENERATION OF THE TEST SUITE (DECODE AND ENCODE)
# Read one and more input file(s) (located in ${GRK_DATA_ROOT}/input/nonregression)
# to know which files processed and with which options.

# Configure the test suite file:
configure_file("test_suite.ctest.in"
               "${CMAKE_CURRENT_BINARY_DIR}/test_suite.ctest"
               @ONLY)

# Read the file into a list
file(STRINGS ${CMAKE_CURRENT_BINARY_DIR}/test_suite.ctest TEST_CMD_LINE_LIST)

# Try to find and configure and read another test suite file
file(GLOB TEST_SUITE_FILES *.ctest.in)
if (TEST_SUITE_FILES)
  foreach(TEST_SUITE_FILE ${TEST_SUITE_FILES})
    # Avoid processing the official test suite
    set(FILE_ALREADY_READ 0)
    get_filename_component(TEST_SUITE_FILENAME ${TEST_SUITE_FILE} NAME)
    string(REGEX MATCH "^test_suite.ctest.in$" FILE_ALREADY_READ ${TEST_SUITE_FILENAME})

    if(NOT FILE_ALREADY_READ)
      # Configure the additional test suite file:
      get_filename_component(TEST_SUITE_FILE_SUB ${TEST_SUITE_FILE} NAME_WE)
      configure_file("${TEST_SUITE_FILE}"
                     "${CMAKE_CURRENT_BINARY_DIR}/${TEST_SUITE_FILE_SUB}.ctest"
                     @ONLY)
      # Read the additional file into a list
      file(STRINGS ${CMAKE_CURRENT_BINARY_DIR}/${TEST_SUITE_FILE_SUB}.ctest TEST_CMD_LINE_LIST_TEMP)
      # Append the list of command
      set(TEST_CMD_LINE_LIST ${TEST_CMD_LINE_LIST} ${TEST_CMD_LINE_LIST_TEMP})
    endif()
  endforeach()
else()
  message(FATAL_ERROR "One test suite should be available (test_suite.ctest.in)")
endif()

set(nonregression_filenames_used)
# Parse the command line found in the file(s)
set(IT_TEST_ENC 0)
set(IT_TEST_DEC 0)
foreach(TEST_CMD_LINE ${TEST_CMD_LINE_LIST})
  set(IGNORE_LINE_FOUND 0)
  # Replace space by ; to generate a list
  string(REPLACE " " ";" CMD_ARG_LIST ${TEST_CMD_LINE})
  # Check if the first argument begin by the comment sign
  list(GET CMD_ARG_LIST 0 EXE_NAME)
  if(EXE_NAME)
    string(REGEX MATCH "^#" IGNORE_LINE_FOUND ${EXE_NAME})
  endif()
  if(IGNORE_LINE_FOUND OR NOT EXE_NAME)
    #message( STATUS "Current line is ignored: ${TEST_CMD_LINE}")
  else()
    # Check if the first argument begins with the fail symbol
    set(FAILED_TEST_FOUND 0)
    string(REGEX MATCH "^!" FAILED_TEST_FOUND ${EXE_NAME})
    if (FAILED_TEST_FOUND)
      list(REMOVE_AT CMD_ARG_LIST 0)
      string(REGEX MATCH "^grk_compress$|^!grk_compress$" ENC_TEST_FOUND ${EXE_NAME})
    else ()
      string(REGEX MATCH "^grk_compress$|^grk_compress_no_raw$|^grk_compress_no_raw_lossless$|^grk_decompress$" EXE_NAME_FOUND ${EXE_NAME})
      if(EXE_NAME_FOUND)
        string(REGEX MATCH "^grk_compress$|^grk_compress$|^grk_compress_no_raw$|^grk_compress_no_raw_lossless$" ENC_TEST_FOUND ${EXE_NAME})
        string(REGEX MATCH "^grk_compress_no_raw$|^grk_compress_no_raw_lossless$" NO_RAW ${EXE_NAME})
        string(REGEX MATCH "grk_compress_no_raw_lossless" LOSSLESS ${EXE_NAME})
      else()
        message( FATAL_ERROR "${EXE_NAME} is not the right executable name to encode file (try to use grk_compress)")
      endif()
      list(REMOVE_AT CMD_ARG_LIST 0)
    endif ()
    # Parse the argument list to find the input filename and output filename
    set(CMD_ARG_LIST_2 "")
    set(ARG_POS 0)
    set(INPUT_ARG_POS 0)
    set(OUTPUT_ARG_POS 0)
    foreach(CMD_ARG_ELT ${CMD_ARG_LIST})
      math(EXPR ARG_POS "${ARG_POS}+1" )
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
      list (APPEND CMD_ARG_LIST_2 ${CMD_ARG_ELT})
    endforeach()

    list(GET CMD_ARG_LIST_2 ${INPUT_ARG_POS} INPUT_FILENAME)
    get_filename_component(INPUT_FILENAME_NAME ${INPUT_FILENAME} NAME)
    get_filename_component(INPUT_FILENAME_NAME_WE ${INPUT_FILENAME_NAME} NAME_WE)
    list(GET CMD_ARG_LIST_2 ${OUTPUT_ARG_POS} OUTPUT_FILENAME)
    get_filename_component(OUTPUT_FILENAME_NAME_WE ${OUTPUT_FILENAME} NAME_WE)
    #-----
    # Now we can add the test suite corresponding to a line command in the file
    #-----
    # ENCODER TEST SUITE
    if(ENC_TEST_FOUND)
        math(EXPR IT_TEST_ENC "${IT_TEST_ENC}+1" )
        # message( STATUS "Encode test found: ${TEST_CMD_LINE}")
        # Encode an image into the jpeg2000 format
        add_test(NAME NR-ENC-${INPUT_FILENAME_NAME}-${IT_TEST_ENC}-encode
          COMMAND grk_compress
          ${CMD_ARG_LIST_2})
        if(FAILED_TEST_FOUND)
            set_tests_properties(NR-ENC-${INPUT_FILENAME_NAME}-${IT_TEST_ENC}-encode PROPERTIES WILL_FAIL TRUE)
        else()
          # Dump the encoding file
          add_test(NAME NR-ENC-${INPUT_FILENAME_NAME}-${IT_TEST_ENC}-dump
            COMMAND grk_dump
            -i ${OUTPUT_FILENAME}
            -o ${OUTPUT_FILENAME}-ENC-${IT_TEST_ENC}.txt
          )
          set_tests_properties(NR-ENC-${INPUT_FILENAME_NAME}-${IT_TEST_ENC}-dump
                               PROPERTIES DEPENDS
                               NR-ENC-${INPUT_FILENAME_NAME}-${IT_TEST_ENC}-encode)

          # Compare the dump file with the baseline
          add_test(NAME NR-ENC-${INPUT_FILENAME_NAME}-${IT_TEST_ENC}-compare_dump2base
            COMMAND compare_dump_files
            -b ${BASELINE_NR}/${OUTPUT_FILENAME_NAME_WE}-ENC-${IT_TEST_ENC}.txt
            -t ${OUTPUT_FILENAME}-ENC-${IT_TEST_ENC}.txt)

          set_tests_properties(NR-ENC-${INPUT_FILENAME_NAME}-${IT_TEST_ENC}-compare_dump2base
                               PROPERTIES DEPENDS
                               NR-ENC-${INPUT_FILENAME_NAME}-${IT_TEST_ENC}-dump)

          # Decode the encoding file with kakadu expand command
          if (KDU_EXPAND_EXECUTABLE)
            add_test(NAME NR-ENC-${INPUT_FILENAME_NAME}-${IT_TEST_ENC}-decode-ref
              COMMAND ${KDU_EXPAND_EXECUTABLE}
              -i ${OUTPUT_FILENAME}
              -o ${OUTPUT_FILENAME}.raw)

            set_tests_properties(NR-ENC-${INPUT_FILENAME_NAME}-${IT_TEST_ENC}-decode-ref
                                 PROPERTIES DEPENDS
                                 NR-ENC-${INPUT_FILENAME_NAME}-${IT_TEST_ENC}-encode)

            # Compare the decoding file with baseline generated from the kdu_expand and baseline.j2k
            if(NOT NO_RAW)
                # Compare the decoding file with baseline generated from the kdu_expand and baseline.j2k
                add_test(NAME NR-ENC-${INPUT_FILENAME_NAME}-${IT_TEST_ENC}-compare_dec-ref-out2base
                COMMAND compare_raw_files
                -b ${BASELINE_NR}/${OUTPUT_FILENAME_NAME_WE}-ENC-${IT_TEST_ENC}.raw
                -t ${OUTPUT_FILENAME}.raw)

                set_tests_properties(NR-ENC-${INPUT_FILENAME_NAME}-${IT_TEST_ENC}-compare_dec-ref-out2base
                                    PROPERTIES DEPENDS
                                    NR-ENC-${INPUT_FILENAME_NAME}-${IT_TEST_ENC}-decode-ref)
            endif()
          endif()

          # If lossless compression (simple test is 4 arguments), decompress & compare
          list(LENGTH CMD_ARG_LIST_2 ARG_COUNT)
          if ((ARG_COUNT EQUAL 4) OR LOSSLESS)
            # can we compare with the input image ?
            if (${INPUT_FILENAME_NAME} MATCHES "\\.tif$")
              add_test(NAME NR-ENC-${INPUT_FILENAME_NAME}-${IT_TEST_ENC}-lossless-decode
                COMMAND grk_decompress -i ${OUTPUT_FILENAME} -o ${OUTPUT_FILENAME}.lossless.tif)
              set_tests_properties(NR-ENC-${INPUT_FILENAME_NAME}-${IT_TEST_ENC}-lossless-decode PROPERTIES
                DEPENDS NR-ENC-${INPUT_FILENAME_NAME}-${IT_TEST_ENC}-encode)
              add_test(NAME NR-ENC-${INPUT_FILENAME_NAME}-${IT_TEST_ENC}-lossless-compare
                COMMAND compare_images -b ${INPUT_FILENAME} -t ${OUTPUT_FILENAME}.lossless.tif -n 1 -d)
              set_tests_properties(NR-ENC-${INPUT_FILENAME_NAME}-${IT_TEST_ENC}-lossless-compare PROPERTIES
                DEPENDS NR-ENC-${INPUT_FILENAME_NAME}-${IT_TEST_ENC}-lossless-decode)	
            endif()
          endif()
      endif()
    # DECODER TEST SUITE
    else()
      #message( STATUS "Decode test found: ${TEST_CMD_LINE}")
      string(FIND ${INPUT_FILENAME} "nonregression" nr_pos)
      if(${nr_pos} GREATER 0)
        list(APPEND nonregression_filenames_used ${INPUT_FILENAME_NAME})
      endif()
      math(EXPR IT_TEST_DEC "${IT_TEST_DEC}+1" )

      # Decode the input image
      add_test(NAME NR-DEC-${INPUT_FILENAME_NAME}-${IT_TEST_DEC}-decode
        COMMAND grk_decompress
        ${CMD_ARG_LIST_2})
      if(FAILED_TEST_FOUND)
        # message( STATUS "Failed Decode test found: ${TEST_CMD_LINE}")
        set_tests_properties(NR-DEC-${INPUT_FILENAME_NAME}-${IT_TEST_DEC}-decode PROPERTIES WILL_FAIL TRUE)
      else()
        # if not failed, check against registered md5:
        add_test(NAME NR-DEC-${INPUT_FILENAME_NAME}-${IT_TEST_DEC}-decode-md5 COMMAND ${CMAKE_COMMAND}
          -DREFFILE:STRING=${CMAKE_CURRENT_SOURCE_DIR}/md5refs.txt
          -DOUTFILENAME:STRING=${OUTPUT_FILENAME}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/checkmd5refs.cmake)
        set_tests_properties(NR-DEC-${INPUT_FILENAME_NAME}-${IT_TEST_DEC}-decode-md5
          PROPERTIES DEPENDS NR-DEC-${INPUT_FILENAME_NAME}-${IT_TEST_DEC}-decode)

# FIXME: add a compare2base function base on raw which
# can output png diff files if necessary
#  add_test(NR-${filename}-compare2base
#      ${EXECUTABLE_OUTPUT_PATH}/compare_images
#     -b ${BASELINE_NR}/${filenameRef}
#     -t ${TEMP}/${filename}.pgx
#     -n ${nbComponents}
#     -d
#     -s b_t_
#     )
#  set_tests_properties(NR-${filename}-compare2base
#                       PROPERTIES DEPENDS
#                       NR-${filename}-decode)
      endif()
    endif()
  endif()
endforeach()

set(existing_filenames)
foreach(f ${DATA_NR_LIST})
  get_filename_component(ff ${f} NAME)
  list(APPEND existing_filenames ${ff})
endforeach()
if(existing_filenames)
  list(REMOVE_ITEM existing_filenames ${nonregression_filenames_used})
endif()

# keep track of new addition:
# if we reach here, then a J2K files was added but no test is present in
# test_suite.ctest.in:
foreach(found_but_no_test ${existing_filenames})
  add_test(NAME Found-But-No-Test-${found_but_no_test} 
  			COMMAND ${CMAKE_COMMAND} -E echo "${found_but_no_test}")
  set_tests_properties(Found-But-No-Test-${found_but_no_test} PROPERTIES WILL_FAIL TRUE)
endforeach()
