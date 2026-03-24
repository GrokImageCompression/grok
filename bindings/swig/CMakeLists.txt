project(GrokSwigBindings)

if(POLICY CMP0122)
    cmake_policy(SET CMP0122 NEW)
endif()

set(GROK_SWIG_CORE_NAME grok_core)
set(GROK_SWIG_CODEC_NAME grok_codec)

# Set the include paths
set(GROK_CORE_INCLUDE_PATH "${CMAKE_SOURCE_DIR}/src/lib/core")
set(GROK_CORE_BUILD_INCLUDE_PATH "${CMAKE_BINARY_DIR}/src/lib/core")

# --- Python bindings ---
if(GRK_BUILD_PYTHON_SWIG)
    find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
    include_directories(${Python3_INCLUDE_DIRS})

    configure_file(
      ${CMAKE_CURRENT_SOURCE_DIR}/${GROK_SWIG_CORE_NAME}.i.in
      ${CMAKE_CURRENT_BINARY_DIR}/${GROK_SWIG_CORE_NAME}.i
      @ONLY
    )

    set(SWIG_INTERFACE_FILE ${CMAKE_CURRENT_BINARY_DIR}/${GROK_SWIG_CORE_NAME}.i)

    swig_add_library(${GROK_SWIG_CORE_NAME}
      TYPE MODULE
      LANGUAGE python
      SOURCES ${SWIG_INTERFACE_FILE}
    )

    target_include_directories(${GROK_SWIG_CORE_NAME} PRIVATE
        ${GROK_CORE_BUILD_INCLUDE_PATH}
    )

    target_link_libraries(${GROK_SWIG_CORE_NAME} PRIVATE ${Python3_LIBRARIES} ${GROK_CORE_NAME})

    set(GROK_PYTHON_DEST_PATH "${CMAKE_BINARY_DIR}/bin")
    add_custom_command(
        TARGET ${GROK_SWIG_CORE_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_BINARY_DIR}/${GROK_SWIG_CORE_NAME}.py
            ${GROK_PYTHON_DEST_PATH}/${GROK_SWIG_CORE_NAME}.py
        COMMENT "Copying grok_core.py to bin directory"
    )
endif()

# --- C# bindings ---
if(GRK_BUILD_CSHARP_SWIG)
    set(GROK_SWIG_CSHARP_NAME grok_core_csharp)
    set(GROK_CSHARP_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/csharp")

    configure_file(
      ${CMAKE_CURRENT_SOURCE_DIR}/${GROK_SWIG_CSHARP_NAME}.i.in
      ${CMAKE_CURRENT_BINARY_DIR}/${GROK_SWIG_CSHARP_NAME}.i
      @ONLY
    )

    file(MAKE_DIRECTORY ${GROK_CSHARP_OUTPUT_DIR})

    set(CMAKE_SWIG_OUTDIR ${GROK_CSHARP_OUTPUT_DIR})
    set(SWIG_OUTFILE_DIR ${CMAKE_CURRENT_BINARY_DIR})

    swig_add_library(${GROK_SWIG_CSHARP_NAME}
      TYPE MODULE
      LANGUAGE csharp
      SOURCES ${CMAKE_CURRENT_BINARY_DIR}/${GROK_SWIG_CSHARP_NAME}.i
    )

    target_include_directories(${GROK_SWIG_CSHARP_NAME} PRIVATE
        ${GROK_CORE_BUILD_INCLUDE_PATH}
    )

    target_link_libraries(${GROK_SWIG_CSHARP_NAME} PRIVATE ${GROK_CORE_NAME})

    # Copy native library to csharp output dir
    add_custom_command(
        TARGET ${GROK_SWIG_CSHARP_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:${GROK_SWIG_CSHARP_NAME}>
            ${GROK_CSHARP_OUTPUT_DIR}/
        COMMENT "Copying C# native library to csharp output directory"
    )

    # Build the C# assembly if dotnet is available
    find_program(DOTNET_EXECUTABLE dotnet)
    if(DOTNET_EXECUTABLE)
        set(GROK_CSHARP_PROJECT_DIR "${CMAKE_SOURCE_DIR}/bindings/swig/csharp")
        if(EXISTS "${GROK_CSHARP_PROJECT_DIR}/GrokCore.csproj")
            add_custom_command(
                TARGET ${GROK_SWIG_CSHARP_NAME}
                POST_BUILD
                COMMAND ${DOTNET_EXECUTABLE} build
                    "${GROK_CSHARP_PROJECT_DIR}/GrokCore.csproj"
                    -o "${GROK_CSHARP_OUTPUT_DIR}"
                COMMENT "Building C# assembly"
            )
        endif()
    endif()

    # Reset SWIG output dirs so they don't affect other targets
    unset(CMAKE_SWIG_OUTDIR)
    unset(SWIG_OUTFILE_DIR)
endif()
