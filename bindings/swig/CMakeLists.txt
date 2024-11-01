project(GrokSwigBindings)

set(GROK_SWIG_CORE_NAME grok_core)
set(GROK_SWIG_CODEC_NAME grok_codec)

find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
include_directories(${Python3_INCLUDE_DIRS})

# Set the include paths
set(GROK_CORE_INCLUDE_PATH "${CMAKE_SOURCE_DIR}/src/lib/core")
set(GROK_CORE_BUILD_INCLUDE_PATH "${CMAKE_BINARY_DIR}/src/lib/core")

# Configure grok_core.i.in with the actual source path
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/${GROK_SWIG_CORE_NAME}.i.in
  ${CMAKE_CURRENT_BINARY_DIR}/${GROK_SWIG_CORE_NAME}.i
  @ONLY
)

# Set SWIG interface file path
set(SWIG_INTERFACE_FILE ${CMAKE_CURRENT_BINARY_DIR}/${GROK_SWIG_CORE_NAME}.i)

# Add SWIG library with the configured interface
swig_add_library(${GROK_SWIG_CORE_NAME}
  TYPE MODULE
  LANGUAGE python
  SOURCES ${SWIG_INTERFACE_FILE}
)

# Include directories for build and source paths
target_include_directories(${GROK_SWIG_CORE_NAME} PRIVATE
    ${GROK_CORE_BUILD_INCLUDE_PATH}
)

# Link to Python libraries and the core library target
target_link_libraries(${GROK_SWIG_CORE_NAME} PRIVATE ${Python3_LIBRARIES} ${GROK_CORE_NAME})

# Copy grok_core.py to the bin directory after build
set(GROK_PYTHON_DEST_PATH "${CMAKE_BINARY_DIR}/bin")
add_custom_command(
    TARGET ${GROK_SWIG_CORE_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_BINARY_DIR}/${GROK_SWIG_CORE_NAME}.py
        ${GROK_PYTHON_DEST_PATH}/${GROK_SWIG_CORE_NAME}.py
    COMMENT "Copying grok_core.py to bin directory"
)
