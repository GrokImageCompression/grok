project(GrokSwigBindings)

if(POLICY CMP0122)
    cmake_policy(SET CMP0122 NEW)
endif()

set(GROK_SWIG_CORE_NAME grok_core)
set(GROK_SWIG_CODEC_NAME grok_codec)

# Set the include paths
set(GROK_CORE_INCLUDE_PATH "${CMAKE_SOURCE_DIR}/src/lib/core")
set(GROK_CORE_BUILD_INCLUDE_PATH "${CMAKE_BINARY_DIR}/src/lib/core")
set(GROK_CODEC_INCLUDE_PATH "${CMAKE_SOURCE_DIR}/src/lib/codec")

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
    # --- Python codec bindings ---
    if(GRK_BUILD_CODEC)
        configure_file(
          ${CMAKE_CURRENT_SOURCE_DIR}/${GROK_SWIG_CODEC_NAME}.i.in
          ${CMAKE_CURRENT_BINARY_DIR}/${GROK_SWIG_CODEC_NAME}.i
          @ONLY
        )

        swig_add_library(${GROK_SWIG_CODEC_NAME}
          TYPE MODULE
          LANGUAGE python
          SOURCES ${CMAKE_CURRENT_BINARY_DIR}/${GROK_SWIG_CODEC_NAME}.i
        )

        target_include_directories(${GROK_SWIG_CODEC_NAME} PRIVATE
            ${GROK_CORE_INCLUDE_PATH}
            ${GROK_CORE_BUILD_INCLUDE_PATH}
            ${GROK_CODEC_INCLUDE_PATH}
        )

        target_link_libraries(${GROK_SWIG_CODEC_NAME} PRIVATE
            ${Python3_LIBRARIES} ${GROK_CODEC_NAME} ${GROK_CORE_NAME})

        add_custom_command(
            TARGET ${GROK_SWIG_CODEC_NAME}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_BINARY_DIR}/${GROK_SWIG_CODEC_NAME}.py
                ${GROK_PYTHON_DEST_PATH}/${GROK_SWIG_CODEC_NAME}.py
            COMMENT "Copying grok_codec.py to bin directory"
        )
    endif()
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

    # --- C# codec bindings ---
    if(GRK_BUILD_CODEC)
        set(GROK_SWIG_CODEC_CSHARP_NAME grok_codec_csharp)

        configure_file(
          ${CMAKE_CURRENT_SOURCE_DIR}/${GROK_SWIG_CODEC_CSHARP_NAME}.i.in
          ${CMAKE_CURRENT_BINARY_DIR}/${GROK_SWIG_CODEC_CSHARP_NAME}.i
          @ONLY
        )

        set(CMAKE_SWIG_OUTDIR ${GROK_CSHARP_OUTPUT_DIR})
        set(SWIG_OUTFILE_DIR ${CMAKE_CURRENT_BINARY_DIR})

        swig_add_library(${GROK_SWIG_CODEC_CSHARP_NAME}
          TYPE MODULE
          LANGUAGE csharp
          SOURCES ${CMAKE_CURRENT_BINARY_DIR}/${GROK_SWIG_CODEC_CSHARP_NAME}.i
        )

        target_include_directories(${GROK_SWIG_CODEC_CSHARP_NAME} PRIVATE
            ${GROK_CORE_INCLUDE_PATH}
            ${GROK_CORE_BUILD_INCLUDE_PATH}
            ${GROK_CODEC_INCLUDE_PATH}
        )

        target_link_libraries(${GROK_SWIG_CODEC_CSHARP_NAME} PRIVATE
            ${GROK_CODEC_NAME} ${GROK_CORE_NAME})

        add_custom_command(
            TARGET ${GROK_SWIG_CODEC_CSHARP_NAME}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                $<TARGET_FILE:${GROK_SWIG_CODEC_CSHARP_NAME}>
                ${GROK_CSHARP_OUTPUT_DIR}/
            COMMENT "Copying C# codec native library to csharp output directory"
        )

        unset(CMAKE_SWIG_OUTDIR)
        unset(SWIG_OUTFILE_DIR)
    endif()
endif()

# --- Java bindings ---
if(GRK_BUILD_JAVA_SWIG)
    set(GROK_SWIG_JAVA_NAME grok_core_java)
    set(GROK_JAVA_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/java")
    set(GROK_JAVA_PACKAGE "org.grok.core")
    string(REPLACE "." "/" GROK_JAVA_PACKAGE_DIR "${GROK_JAVA_PACKAGE}")
    set(GROK_JAVA_SRC_DIR "${GROK_JAVA_OUTPUT_DIR}/${GROK_JAVA_PACKAGE_DIR}")

    configure_file(
      ${CMAKE_CURRENT_SOURCE_DIR}/${GROK_SWIG_JAVA_NAME}.i.in
      ${CMAKE_CURRENT_BINARY_DIR}/${GROK_SWIG_JAVA_NAME}.i
      @ONLY
    )

    file(MAKE_DIRECTORY ${GROK_JAVA_SRC_DIR})

    set(CMAKE_SWIG_OUTDIR ${GROK_JAVA_SRC_DIR})
    set(SWIG_OUTFILE_DIR ${CMAKE_CURRENT_BINARY_DIR})
    set(CMAKE_SWIG_FLAGS -package ${GROK_JAVA_PACKAGE})

    swig_add_library(${GROK_SWIG_JAVA_NAME}
      TYPE MODULE
      LANGUAGE java
      SOURCES ${CMAKE_CURRENT_BINARY_DIR}/${GROK_SWIG_JAVA_NAME}.i
    )

    target_include_directories(${GROK_SWIG_JAVA_NAME} PRIVATE
        ${GROK_CORE_BUILD_INCLUDE_PATH}
        ${JNI_INCLUDE_DIRS}
    )

    target_link_libraries(${GROK_SWIG_JAVA_NAME} PRIVATE ${GROK_CORE_NAME})

    # Copy native library to java output dir
    add_custom_command(
        TARGET ${GROK_SWIG_JAVA_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:${GROK_SWIG_JAVA_NAME}>
            ${GROK_JAVA_OUTPUT_DIR}/
        COMMENT "Copying Java native library to java output directory"
    )

    # Compile Java sources if javac is available
    find_program(JAVAC_EXECUTABLE javac)
    find_program(JAVA_EXECUTABLE java)
    if(JAVAC_EXECUTABLE)
        # Detect runtime version so javac targets the correct class file format
        set(JAVAC_RELEASE_FLAG "")
        if(JAVA_EXECUTABLE)
            execute_process(
                COMMAND ${JAVA_EXECUTABLE} -version
                ERROR_VARIABLE _JAVA_VER_OUT
                OUTPUT_VARIABLE _JAVA_VER_OUT2
            )
            string(CONCAT _JAVA_VER_OUT "${_JAVA_VER_OUT}" "${_JAVA_VER_OUT2}")
            string(REGEX MATCH "\"([0-9]+)" _JAVA_VER_MATCH "${_JAVA_VER_OUT}")
            if(CMAKE_MATCH_1)
                set(JAVAC_RELEASE_FLAG --release ${CMAKE_MATCH_1})
            endif()
        endif()

        add_custom_command(
            TARGET ${GROK_SWIG_JAVA_NAME}
            POST_BUILD
            COMMAND ${JAVAC_EXECUTABLE}
                ${JAVAC_RELEASE_FLAG}
                -d "${GROK_JAVA_OUTPUT_DIR}"
                ${GROK_JAVA_SRC_DIR}/*.java
            COMMENT "Compiling Java bindings"
        )
    endif()

    unset(CMAKE_SWIG_OUTDIR)
    unset(SWIG_OUTFILE_DIR)
    unset(CMAKE_SWIG_FLAGS)

    # --- Java codec bindings ---
    if(GRK_BUILD_CODEC)
        set(GROK_SWIG_CODEC_JAVA_NAME grok_codec_java)
        set(GROK_JAVA_CODEC_PACKAGE "org.grok.codec")
        string(REPLACE "." "/" GROK_JAVA_CODEC_PACKAGE_DIR "${GROK_JAVA_CODEC_PACKAGE}")
        set(GROK_JAVA_CODEC_SRC_DIR "${GROK_JAVA_OUTPUT_DIR}/${GROK_JAVA_CODEC_PACKAGE_DIR}")

        configure_file(
          ${CMAKE_CURRENT_SOURCE_DIR}/${GROK_SWIG_CODEC_JAVA_NAME}.i.in
          ${CMAKE_CURRENT_BINARY_DIR}/${GROK_SWIG_CODEC_JAVA_NAME}.i
          @ONLY
        )

        file(MAKE_DIRECTORY ${GROK_JAVA_CODEC_SRC_DIR})

        set(CMAKE_SWIG_OUTDIR ${GROK_JAVA_CODEC_SRC_DIR})
        set(SWIG_OUTFILE_DIR ${CMAKE_CURRENT_BINARY_DIR})
        set(CMAKE_SWIG_FLAGS -package ${GROK_JAVA_CODEC_PACKAGE})

        swig_add_library(${GROK_SWIG_CODEC_JAVA_NAME}
          TYPE MODULE
          LANGUAGE java
          SOURCES ${CMAKE_CURRENT_BINARY_DIR}/${GROK_SWIG_CODEC_JAVA_NAME}.i
        )

        target_include_directories(${GROK_SWIG_CODEC_JAVA_NAME} PRIVATE
            ${GROK_CORE_INCLUDE_PATH}
            ${GROK_CORE_BUILD_INCLUDE_PATH}
            ${GROK_CODEC_INCLUDE_PATH}
            ${JNI_INCLUDE_DIRS}
        )

        target_link_libraries(${GROK_SWIG_CODEC_JAVA_NAME} PRIVATE
            ${GROK_CODEC_NAME} ${GROK_CORE_NAME})

        add_custom_command(
            TARGET ${GROK_SWIG_CODEC_JAVA_NAME}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                $<TARGET_FILE:${GROK_SWIG_CODEC_JAVA_NAME}>
                ${GROK_JAVA_OUTPUT_DIR}/
            COMMENT "Copying Java codec native library to java output directory"
        )

        if(JAVAC_EXECUTABLE)
            add_custom_command(
                TARGET ${GROK_SWIG_CODEC_JAVA_NAME}
                POST_BUILD
                COMMAND ${JAVAC_EXECUTABLE}
                    ${JAVAC_RELEASE_FLAG}
                    -d "${GROK_JAVA_OUTPUT_DIR}"
                    ${GROK_JAVA_CODEC_SRC_DIR}/*.java
                COMMENT "Compiling Java codec bindings"
            )
        endif()

        unset(CMAKE_SWIG_OUTDIR)
        unset(SWIG_OUTFILE_DIR)
        unset(CMAKE_SWIG_FLAGS)
    endif()
endif()
