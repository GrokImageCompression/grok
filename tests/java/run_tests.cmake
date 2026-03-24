# CMake script to compile and run Java tests
# Called via: cmake -P run_tests.cmake with vars set by parent

# Detect java runtime version for cross-compilation
execute_process(
    COMMAND ${JAVA_EXECUTABLE} -version
    ERROR_VARIABLE JAVA_VERSION_OUTPUT
    OUTPUT_VARIABLE JAVA_VERSION_OUTPUT2
)
# Combine stdout and stderr (java -version outputs to stderr)
string(CONCAT JAVA_VERSION_OUTPUT "${JAVA_VERSION_OUTPUT}" "${JAVA_VERSION_OUTPUT2}")
string(REGEX MATCH "\"([0-9]+)" JAVA_VERSION_MATCH "${JAVA_VERSION_OUTPUT}")
set(JAVA_MAJOR_VERSION "${CMAKE_MATCH_1}")

set(JAVAC_RELEASE_FLAG "")
if(JAVA_MAJOR_VERSION)
    set(JAVAC_RELEASE_FLAG --release ${JAVA_MAJOR_VERSION})
endif()

# Compile the test
execute_process(
    COMMAND ${JAVAC_EXECUTABLE}
        ${JAVAC_RELEASE_FLAG}
        -cp "${GRK_JAVA_OUTPUT_DIR}"
        -d "${GRK_JAVA_OUTPUT_DIR}"
        "${GRK_JAVA_TEST_DIR}/org/grok/core/GrokCoreTests.java"
    RESULT_VARIABLE COMPILE_RESULT
    OUTPUT_VARIABLE COMPILE_OUT
    ERROR_VARIABLE COMPILE_ERR
)
if(NOT COMPILE_RESULT EQUAL 0)
    message(FATAL_ERROR "Java test compilation failed:\n${COMPILE_OUT}\n${COMPILE_ERR}")
endif()

# Run the test
execute_process(
    COMMAND ${JAVA_EXECUTABLE}
        -Djava.library.path=${GRK_JAVA_OUTPUT_DIR}
        -cp "${GRK_JAVA_OUTPUT_DIR}"
        org.grok.core.GrokCoreTests
    RESULT_VARIABLE RUN_RESULT
    OUTPUT_VARIABLE RUN_OUT
    ERROR_VARIABLE RUN_ERR
)
message("${RUN_OUT}")
if(NOT "${RUN_ERR}" STREQUAL "")
    message("${RUN_ERR}")
endif()
if(NOT RUN_RESULT EQUAL 0)
    message(FATAL_ERROR "Java tests failed with exit code ${RUN_RESULT}")
endif()
