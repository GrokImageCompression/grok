find_package(Threads REQUIRED)

IF(UNIX)
find_package(Valgrind)
if (Valgrind_FOUND)
	message(STATUS "Found Valgrind")
    add_definitions(-DGRK_HAVE_VALGRIND)
    include_directories(${Valgrind_INCLUDE_DIR})
endif(Valgrind_FOUND)
IF(BUILD_SHARED_LIBS)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         string(APPEND CMAKE_CXX_FLAGS " -fvisibility=hidden")
    ENDIF()
ENDIF()
ENDIF(UNIX)

install( FILES  ${CMAKE_CURRENT_BINARY_DIR}/grk_config.h
 DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${GROK_INSTALL_SUBDIR} COMPONENT Headers)

include_directories(
  ${CMAKE_CURRENT_BINARY_DIR}/../../bin # grk_apps_config.h
  ${CMAKE_CURRENT_BINARY_DIR} # grk_config.h and grk_config_private.h
  ${GROK_SOURCE_DIR}/src/lib/codec/common
  ${GROK_SOURCE_DIR}/src/lib/codec
  ${GROK_SOURCE_DIR}/src/include
  ${GROK_SOURCE_DIR}/src/include/taskflow # taskflow submodule (nests headers under taskflow/)
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin
  ${CMAKE_CURRENT_SOURCE_DIR}/wavelet
  ${CMAKE_CURRENT_SOURCE_DIR}/t1
  ${CMAKE_CURRENT_SOURCE_DIR}/t1_t2
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/codeblock
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/block_coder
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/block_coder/mqcoder
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/common
  ${CMAKE_CURRENT_SOURCE_DIR}/util
  ${CMAKE_CURRENT_SOURCE_DIR}/canvas
  ${CMAKE_CURRENT_SOURCE_DIR}/canvas/precinct
  ${CMAKE_CURRENT_SOURCE_DIR}/canvas/resolution
  ${CMAKE_CURRENT_SOURCE_DIR}/canvas/subband
  ${CMAKE_CURRENT_SOURCE_DIR}/canvas/tile
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream
  ${CMAKE_CURRENT_SOURCE_DIR}/fileformat
  ${CMAKE_CURRENT_SOURCE_DIR}/fileformat/compress
  ${CMAKE_CURRENT_SOURCE_DIR}/fileformat/decompress
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/decompress
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/compress
  ${CMAKE_CURRENT_SOURCE_DIR}/point_transform
  ${CMAKE_CURRENT_SOURCE_DIR}/t2
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/quantizer
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/quantizer/part15
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/quantizer/part1
  ${CMAKE_CURRENT_SOURCE_DIR}/tile_processor
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/standard
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/standard/flow
  ${CMAKE_CURRENT_SOURCE_DIR}/stream
  ${CMAKE_CURRENT_SOURCE_DIR}/stream/fetchers
  ${CMAKE_CURRENT_SOURCE_DIR}/stream/fetchers/s3
  ${CMAKE_CURRENT_SOURCE_DIR}/filters
  ${CMAKE_CURRENT_SOURCE_DIR}/cache
  ${CMAKE_CURRENT_SOURCE_DIR}/debug
  ${LCMS_INCLUDE_DIRNAME}
)

# When the GPU plugin submodule is present and the loader is enabled,
# add the submodule's include path for plugin_api.h / plugin_types.h
if (GRK_BUILD_PLUGIN_LOADER AND EXISTS "${GROK_SOURCE_DIR}/extern/grok-gpu-plugin/include")
  include_directories(${GROK_SOURCE_DIR}/extern/grok-gpu-plugin/include)
endif()

# Defines the source code for executables
set(GROK_EXECUTABLES_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/util/bench_dwt.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/t1_generate_luts.cpp
)

set(GROK_LIBRARY_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/util/Logger.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/SparseBuffer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/GrkImage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/GrkImageSIMD.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/XYZTransform.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/GrkMatrix.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/stream/MemStream.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stream/MappedFile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stream/StreamIO.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stream/StreamGenerator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stream/fetchers/CurlFetcher.cpp
  
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/minpf_dynamic_library.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/minpf_plugin_manager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/plugin_bridge.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/TileWindow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/Profile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/CodeStream.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/CodingParams.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/fileformat/FileFormatJP2Family.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/fileformat/FileFormatMJ2.cpp
    
  ${CMAKE_CURRENT_SOURCE_DIR}/fileformat/compress/FileFormatJP2Compress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fileformat/compress/FileFormatMJ2Compress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fileformat/decompress/FileFormatJP2Decompress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fileformat/decompress/FileFormatMJ2Decompress.cpp
  
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/compress/CodeStreamCompress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/decompress/CodeStreamDecompress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/decompress/CodeStreamDecompress_ReadMarkers.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/decompress/CodeStreamDecompress_Dump.cpp
  
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/SIZMarker.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/PPMMarker.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/SOTMarker.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/TLMMarker.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/PLMarker.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/MarkerParser.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/MarkerCache.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/debug/debug_context.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/point_transform/mct.cpp
  
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/PacketManager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/T2Compress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/T2Decompress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/RateControl.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/RateInfo.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/PacketIter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/PacketParser.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/PacketCache.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/SelectiveFetchRanges.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/quantizer/QuantizerFactory.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/quantizer/Qfactor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/quantizer/part15/QuantizerOJPH.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/quantizer/part1//Quantizer.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/grok.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/standard/flow/ImageComponentFlow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/standard/SchedulerStandard.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/standard/DecompressScheduler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/standard/CompressScheduler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/CodecScheduler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/CoderPool.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/wavelet/WaveletFwd.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/wavelet/WaveletPoolData.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/wavelet/WaveletReverse.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/wavelet/WaveletReverse97.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/wavelet/WaveletReverse97_16.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/wavelet/WaveletReversePartial.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/canvas/precinct/Precinct.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/canvas/precinct/PrecinctImpl.cpp  
  ${CMAKE_CURRENT_SOURCE_DIR}/canvas/resolution/Resolution.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/canvas/subband/Subband.cpp
   
  ${CMAKE_CURRENT_SOURCE_DIR}/tile_processor/TileProcessor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/tile_processor/TileProcessorCompress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/tile_processor/TileProcessor_RateControl.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/t1/CoderFactory.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/CoderOJPH.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_common.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_decoder32.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_decoder64.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_encoder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/others/ojph_arch.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/others/ojph_mem.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/others/ojph_message.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/block_coder/BlockCoderDec.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/block_coder/BlockCoderEnc.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/Coder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/block_coder/mqcoder/mqc.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/block_coder/mqcoder/mqc_base.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/block_coder/mqcoder/mqc_backup.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/block_coder/mqcoder/mqc_state.cpp
)

# OpenJPH SIMD-accelerated block coding files
if(NOT CMAKE_SYSTEM_NAME STREQUAL Emscripten)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|amd64|i[3-6]86)")
    # SSSE3-accelerated decoder
    list(APPEND GROK_LIBRARY_SRCS
      ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_decoder_ssse3.cpp)
    # AVX2-accelerated decoder and encoder
    list(APPEND GROK_LIBRARY_SRCS
      ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_decoder_avx2.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_encoder_avx2.cpp)
    # AVX512-accelerated encoder
    list(APPEND GROK_LIBRARY_SRCS
      ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_encoder_avx512.cpp)

    if(MSVC)
      set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_decoder_avx2.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_encoder_avx2.cpp
        PROPERTIES COMPILE_FLAGS "/arch:AVX2")
      set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_encoder_avx512.cpp
        PROPERTIES COMPILE_FLAGS "/arch:AVX512")
    else()
      set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_decoder_ssse3.cpp
        PROPERTIES COMPILE_FLAGS "-mssse3 -w")
      set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_decoder_avx2.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_encoder_avx2.cpp
        PROPERTIES COMPILE_FLAGS "-mavx2 -w")
      set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_encoder_avx512.cpp
        PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512cd -w")
    endif()
  endif()
endif()

# suppress warnings from third-party OpenJPH code
set_source_files_properties(
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_common.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_decoder32.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_decoder64.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_encoder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/others/ojph_arch.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/others/ojph_mem.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/others/ojph_message.cpp
  PROPERTIES COMPILE_OPTIONS "-w")

# the reversible 5/3 inverse wavelet lifting is exact modular arithmetic:
# valid coefficients never overflow, but a malformed codestream can push an
# intermediate past int range. define that as 2's-complement wraparound (what
# the hardware already does) so it is defined behavior, not a ubsan trap.
if(NOT MSVC)
  set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/wavelet/WaveletReverse.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/wavelet/WaveletReverse97_16.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/wavelet/WaveletReversePartial.cpp
    PROPERTIES COMPILE_OPTIONS "-fwrapv")
endif()

if (GRK_BUILD_PLUGIN_LOADER)
    add_definitions(-DGRK_BUILD_PLUGIN_LOADER)
endif()

set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "Enable HWY examples")
set(HWY_ENABLE_CONTRIB OFF CACHE BOOL "Enable HWY contrib")
set(HWY_ENABLE_INSTALL OFF CACHE BOOL "Enable HWY install")
set(HWY_FORCE_STATIC_LIBS ON CACHE BOOL "Enable HWY force static libs")
set(INSTALL_GTEST OFF CACHE BOOL "Install GTest")
set(HWY_ENABLE_TESTS OFF CACHE BOOL "Disable tests")
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/highway EXCLUDE_FROM_ALL)

add_library(${GROK_CORE_NAME} ${GROK_LIBRARY_SRCS})
set_target_properties(${GROK_CORE_NAME} PROPERTIES ${GROK_LIBRARY_PROPERTIES})
target_compile_options(${GROK_CORE_NAME} PRIVATE ${GROK_COMPILE_OPTIONS} PRIVATE ${HWY_FLAGS})
# set emcc options
if (CMAKE_SYSTEM_NAME STREQUAL Emscripten)
  target_link_options(${exe} PUBLIC -sASSERTIONS=1 -sPROXY_TO_PTHREAD -sTOTAL_MEMORY=1536MB -sEXIT_RUNTIME=1 -sUSE_PTHREADS=1)
  target_compile_options(${exe} PUBLIC -matomics)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  target_link_options(${GROK_CORE_NAME} PRIVATE "LINKER:-z,now")
endif()

# link to libatomic for these architectures
if (GRK_ARCH MATCHES "armv7l|armv8l|m68k|mips|sh4|ppc|riscv64")
  target_link_libraries(${GROK_CORE_NAME} PUBLIC atomic)
endif (GRK_ARCH MATCHES "armv7l|armv8l|m68k|mips|sh4|ppc|riscv64")

# mips needs explicit linker flag to disable executable stack
if (GRK_ARCH MATCHES "mips")
  target_link_options(${GROK_CORE_NAME} PRIVATE "LINKER:-z,noexecstack")
endif (GRK_ARCH MATCHES "mips")

if(UNIX)
  target_link_libraries(${GROK_CORE_NAME} PUBLIC ${CMAKE_THREAD_LIBS_INIT})
endif(UNIX)
target_link_libraries(${GROK_CORE_NAME} PRIVATE hwy ${LCMS_LIBNAME})

# mercury streaming fast path — compile mercury's DWT kernels against grok's
# Highway and link the extern-kernels mercury staticlib (built in-tree by
# cargo). requires cargo: when it is absent (e.g. the Alpine musl guard job)
# the fast path is dropped and mercury_fastpath.cpp compiles to a no-op
# fallback. run cargo --version rather than trusting find_program: the
# oss-fuzz base-builder image puts a cargo wrapper on PATH without a rust
# toolchain behind it.
set(MERCURY_DIR ${CMAKE_SOURCE_DIR}/mercury)
find_program(CARGO_EXECUTABLE cargo)
set(GRK_ENABLE_MERCURY OFF)
if(CARGO_EXECUTABLE AND EXISTS ${MERCURY_DIR}/Cargo.toml)
  execute_process(COMMAND ${CARGO_EXECUTABLE} --version
    RESULT_VARIABLE CARGO_VERSION_RESULT OUTPUT_QUIET ERROR_QUIET)
  if(CARGO_VERSION_RESULT EQUAL 0)
    set(GRK_ENABLE_MERCURY ON)
  endif()
endif()
set(GRK_ENABLE_MERCURY ${GRK_ENABLE_MERCURY} PARENT_SCOPE)

target_sources(${GROK_CORE_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mercury_fastpath.cpp)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/mercury_fastpath.cpp PROPERTIES
  INCLUDE_DIRECTORIES "${MERCURY_DIR}/include")

if(GRK_ENABLE_MERCURY)
  # an explicit cargo target (CARGO_BUILD_TARGET) keeps RUSTFLAGS off build
  # scripts and proc macros, and moves the artifact under a triple subdir
  set(MERCURY_TARGET_ROOT ${MERCURY_DIR}/target-grok)
  if(DEFINED ENV{CARGO_BUILD_TARGET})
    set(MERCURY_TARGET_ROOT ${MERCURY_TARGET_ROOT}/$ENV{CARGO_BUILD_TARGET})
  endif()
  if(MSVC)
    set(MERCURY_LIB ${MERCURY_TARGET_ROOT}/release/mercury.lib)
  else()
    set(MERCURY_LIB ${MERCURY_TARGET_ROOT}/release/libmercury.a)
  endif()
  file(GLOB_RECURSE MERCURY_RUST_SRCS CONFIGURE_DEPENDS ${MERCURY_DIR}/src/*.rs)
  add_custom_command(
    OUTPUT ${MERCURY_LIB}
    COMMAND ${CARGO_EXECUTABLE} build --release --lib
            --no-default-features --features extern-kernels
            --target-dir ${MERCURY_DIR}/target-grok
    WORKING_DIRECTORY ${MERCURY_DIR}
    DEPENDS ${MERCURY_RUST_SRCS}
            ${MERCURY_DIR}/Cargo.toml ${MERCURY_DIR}/Cargo.lock ${MERCURY_DIR}/build.rs
    COMMENT "Building mercury staticlib (cargo)"
    VERBATIM)
  add_custom_target(mercury_lib DEPENDS ${MERCURY_LIB})
  add_dependencies(${GROK_CORE_NAME} mercury_lib)

  # the kernels live in their own archive, not inside grokj2k: the rust lib
  # pulls mercury_hwy_* from them, so static consumer links must resolve
  # left to right as grokj2k -> mercury -> kernels -> hwy.
  add_library(mercury_kernels STATIC
    ${MERCURY_DIR}/native/transform/hwy_dwt.cpp
    ${MERCURY_DIR}/native/transform/mercury_dwt_bridge.cpp)
  set_target_properties(mercury_kernels PROPERTIES POSITION_INDEPENDENT_CODE ON)

  # -msse2 is the x86 SSE baseline for the Highway kernels; on non-x86
  # targets (e.g. arm64 macOS) it is invalid, so omit it there. msvc gets
  # sse2 by default on x64 and uses per-config optimization flags.
  if(MSVC)
    set(MERCURY_KERNEL_OPTS "/w")
  else()
    set(MERCURY_KERNEL_OPTS "-O2;-std=gnu++23;-w")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|amd64|i[3-6]86)")
      list(APPEND MERCURY_KERNEL_OPTS "-msse2")
    endif()
  endif()
  target_compile_options(mercury_kernels PRIVATE ${MERCURY_KERNEL_OPTS})
  target_compile_definitions(mercury_kernels PRIVATE
    HWY_STATIC_DEFINE TOOLCHAIN_MISS_ASM_HWCAP_H NDEBUG)
  target_include_directories(mercury_kernels PRIVATE
    ${MERCURY_DIR}/native ${CMAKE_CURRENT_SOURCE_DIR}/highway)
  target_link_libraries(mercury_kernels PRIVATE hwy)

  target_sources(${GROK_CORE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/mercury_t1_shim.cpp)

  set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/mercury_fastpath.cpp PROPERTIES
    COMPILE_DEFINITIONS GRK_MERCURY_BUILD)
  if(WIN32)
    # system libs rust's std needs (rustc --print native-static-libs)
    target_link_libraries(${GROK_CORE_NAME} PRIVATE ${MERCURY_LIB} mercury_kernels
      advapi32 bcrypt ntdll userenv ws2_32 synchronization)
  else()
    target_link_libraries(${GROK_CORE_NAME} PRIVATE ${MERCURY_LIB} mercury_kernels dl)
  endif()
  if(NOT BUILD_SHARED_LIBS)
    # static grokj2k keeps mercury_kernels in its link interface, so the
    # export set must carry it
    install(TARGETS mercury_kernels EXPORT GrokTargets
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Libraries)
  endif()
endif()
if(GRK_ENABLE_LIBCURL AND GRK_HAVE_LIBCURL)
  target_link_libraries(${GROK_CORE_NAME} PRIVATE ${GRK_CURL_TARGET})
  target_include_directories(${GROK_CORE_NAME} PRIVATE
      ${CURL_INCLUDE_DIRS}
      ${curl_SOURCE_DIR}/include
  )
endif(GRK_ENABLE_LIBCURL AND GRK_HAVE_LIBCURL)


# Public include directory for consumers of the library
target_include_directories(${GROK_CORE_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${GROK_INSTALL_SUBDIR}>
)

# bundle all static libraries into a single library
if (GRK_BUNDLE_STATIC_CORE AND NOT BUILD_SHARED_LIBS AND NOT APPLE)
    include(BundleStatic)
	bundle_static_library(${GROK_CORE_NAME} ${GROK_CORE_NAME}_bundled)
endif(GRK_BUNDLE_STATIC_CORE AND NOT BUILD_SHARED_LIBS AND NOT APPLE)

install(TARGETS ${GROK_CORE_NAME}
  EXPORT GrokTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Applications
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Libraries
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Libraries)
  
install(FILES grok.h 
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${GROK_INSTALL_SUBDIR} COMPONENT Headers
)

if(BUILD_LUTS_GENERATOR)
# internal utility to generate t1_luts.h (part of the jp2 lib)
# no need to install:
add_executable(t1_generate_luts ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_part1/t1_generate_luts.cpp)
  if(UNIX)
    target_link_libraries(t1_generate_luts)
  endif()
endif()
