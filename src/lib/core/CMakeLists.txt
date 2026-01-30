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
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin
  ${CMAKE_CURRENT_SOURCE_DIR}/wavelet
  ${CMAKE_CURRENT_SOURCE_DIR}/t1
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/block_coder
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/block_coder/mqcoder
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/common
  ${CMAKE_CURRENT_SOURCE_DIR}/util
  ${CMAKE_CURRENT_SOURCE_DIR}/canvas
  ${CMAKE_CURRENT_SOURCE_DIR}/canvas/codeblock
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
  ${CMAKE_CURRENT_SOURCE_DIR}/tile_processor
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/whole_tile
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/whole_tile/flow
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/window
  ${CMAKE_CURRENT_SOURCE_DIR}/stream
  ${CMAKE_CURRENT_SOURCE_DIR}/stream/fetchers
  ${CMAKE_CURRENT_SOURCE_DIR}/stream/fetchers/s3
  ${CMAKE_CURRENT_SOURCE_DIR}/filters
  ${CMAKE_CURRENT_SOURCE_DIR}/cache
  ${CMAKE_CURRENT_SOURCE_DIR}/debug
  ${LCMS_INCLUDE_DIRNAME}
)

# Defines the source code for executables
set(GROK_EXECUTABLES_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/util/bench_dwt.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/t1_generate_luts.cpp
)

set(GROK_LIBRARY_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/util/Logger.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/SparseBuffer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/GrkImage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/GrkMatrix.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/stream/MemStream.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stream/MappedFile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stream/StreamIO.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stream/StreamGenerator.cpp
  
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
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/BitIO.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/BitIO.h

  ${CMAKE_CURRENT_SOURCE_DIR}/grok.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/whole_tile/flow/ImageComponentFlow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/whole_tile/WholeTileScheduler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/whole_tile/DecompressScheduler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/whole_tile/CompressScheduler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/window/DecompressWindowScheduler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/window/CompressWindowScheduler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/window/WindowScheduler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/CodecScheduler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/CoderPool.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/wavelet/WaveletFwd.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/wavelet/WaveletReverse.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/wavelet/WaveletReverse97.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/wavelet/WaveletReversePartial.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/t1/CoderFactory.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/canvas/precinct/Precinct.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/canvas/precinct/PrecinctImpl.cpp  
  ${CMAKE_CURRENT_SOURCE_DIR}/canvas/resolution/Resolution.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/canvas/subband/Subband.cpp
   
  ${CMAKE_CURRENT_SOURCE_DIR}/tile_processor/TileProcessor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/tile_processor/TileProcessorCompress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/tile_processor/TileProcessor_RateControl.cpp
 
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/CoderOJPH.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/QuantizerOJPH.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_common.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_decoder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/coding/ojph_block_encoder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part15/others/ojph_mem.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/block_coder/BlockCoderDec.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/block_coder/BlockCoderEnc.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/Coder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1//Quantizer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/block_coder/mqcoder/mqc.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/block_coder/mqcoder/mqc_base.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/block_coder/mqcoder/mqc_backup.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/block_coder/mqcoder/mqc_state.cpp
)

add_definitions(-DSPDLOG_COMPILED_LIB)
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
target_link_libraries(${GROK_CORE_NAME} PRIVATE hwy ${LCMS_LIBNAME} )
if(GRK_ENABLE_LIBCURL AND GRK_HAVE_LIBCURL)
  target_link_libraries(${GROK_CORE_NAME} PRIVATE CURL::libcurl)
  target_include_directories(${GROK_CORE_NAME} PRIVATE
      ${CURL_INCLUDE_DIRS}
      ${curl_SOURCE_DIR}/include
  )
endif(GRK_ENABLE_LIBCURL AND GRK_HAVE_LIBCURL)


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
