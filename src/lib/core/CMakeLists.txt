find_package(Threads REQUIRED)

if (NOT APPLE)
include(BundleStatic)
endif(NOT APPLE)

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
  ${CMAKE_BINARY_DIR}/src/lib/core # grk_config.h and grk_config_private.h
  ${GROK_SOURCE_DIR}/src/lib/codec/common
  ${GROK_SOURCE_DIR}/src/lib/codec
  ${GROK_SOURCE_DIR}/src/include
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin
  ${CMAKE_CURRENT_SOURCE_DIR}/wavelet
  ${CMAKE_CURRENT_SOURCE_DIR}/t1
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/impl
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OpenHTJ2K
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OpenHTJ2K/coding
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OpenHTJ2K/common
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OJPH/common
  ${CMAKE_CURRENT_SOURCE_DIR}/util
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers
  ${CMAKE_CURRENT_SOURCE_DIR}/point_transform
  ${CMAKE_CURRENT_SOURCE_DIR}/t2
  ${CMAKE_CURRENT_SOURCE_DIR}/tile
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling
  ${CMAKE_CURRENT_SOURCE_DIR}/filters
  ${CMAKE_CURRENT_SOURCE_DIR}/highway
  ${CMAKE_CURRENT_SOURCE_DIR}/cache
  ${GROK_SOURCE_DIR}/thirdparty/liblcms2/include
)

# Defines the source code for executables
set(GROK_EXECUTABLES_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/util/bench_dwt.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/t1_generate_luts.cpp
)

# Defines the source code for the library
set(GROK_LIBRARY_OPENHTJ2K_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OpenHTJ2K/coding/coding_local.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OpenHTJ2K/coding/coding_units.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OpenHTJ2K/coding/ht_block_decoding.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OpenHTJ2K/coding/ht_block_encoding.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OpenHTJ2K/coding/coding_units.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OpenHTJ2K/coding/dec_CxtVLC_tables.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OpenHTJ2K/coding/enc_CxtVLC_tables.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OpenHTJ2K/coding/ht_block_decoding.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OpenHTJ2K/coding/ht_block_encoding.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OpenHTJ2K/T1OpenHTJ2K.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OpenHTJ2K/T1OpenHTJ2K.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OpenHTJ2K/QuantizerOpenHTJ2K.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OpenHTJ2K/QuantizerOpenHTJ2K.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OpenHTJ2K/PostT1DecompressFiltersOpenHTJ2K.h
)
set(GROK_LIBRARY_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/util/BufferedStream.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/BufferedStream.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/logger.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/logger.h  
  ${CMAKE_CURRENT_SOURCE_DIR}/util/GrkMappedFile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/GrkMappedFile.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/MemStream.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/MemStream.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/grk_intmath.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/grk_intmath.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/geometry.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/SparseBuffer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/SparseBuffer.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/grk_exceptions.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/testing.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/GrkImage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/GrkImage_Conversion.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/GrkImage.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/GrkObjectWrapper.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/GrkObjectWrapper.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/GrkMatrix.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/GrkMatrix.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/simd.h
  
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/minpf_dynamic_library.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/minpf_dynamic_library.h
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/minpf_plugin_manager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/minpf_plugin_manager.h
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/minpf_common.h
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/minpf_plugin.h
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/plugin_interface.h
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/plugin_bridge.h
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/plugin_bridge.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/TileSet.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/TileSet.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/Profile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/Profile.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/CodeStream.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/CodeStream.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/CodeStreamCompress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/CodeStreamCompress.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/CodeStreamDecompress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/CodeStreamDecompress_Markers.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/CodeStreamDecompress.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/FileFormat.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/FileFormat.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/FileFormatCompress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/FileFormatCompress.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/FileFormatDecompress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/FileFormatDecompress.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/CodingParams.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/CodingParams.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/SIZMarker.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/SIZMarker.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/PPMMarker.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/PPMMarker.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/SOTMarker.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/SOTMarker.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/cache/StripCache.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/cache/StripCache.h
  ${CMAKE_CURRENT_SOURCE_DIR}/cache/TileCache.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/cache/TileCache.h
  ${CMAKE_CURRENT_SOURCE_DIR}/cache/MemManager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/cache/MemManager.h
  ${CMAKE_CURRENT_SOURCE_DIR}/cache/LengthCache.h
  ${CMAKE_CURRENT_SOURCE_DIR}/cache/LengthCache.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/cache/PLMarkerMgr.h
  ${CMAKE_CURRENT_SOURCE_DIR}/cache/PLMarkerMgr.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/cache/PLCache.h
  ${CMAKE_CURRENT_SOURCE_DIR}/cache/PLCache.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/point_transform/mct.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/point_transform/mct.h
  
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/PacketManager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/PacketManager.h  
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/T2Compress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/T2Compress.h  
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/T2Decompress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/T2Decompress.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/RateControl.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/RateControl.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/RateInfo.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/RateInfo.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/PacketIter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/PacketIter.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/PacketParser.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/PacketParser.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/TagTree.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/BitIO.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/BitIO.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/IBitIO.h

  ${CMAKE_CURRENT_SOURCE_DIR}/grok.h
  ${CMAKE_CURRENT_SOURCE_DIR}/grok.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/grk_includes.h  
    
  ${CMAKE_CURRENT_SOURCE_DIR}/tile/TileComponentWindow.h
  ${CMAKE_CURRENT_SOURCE_DIR}/tile/TileComponent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/tile/TileComponent.h
  ${CMAKE_CURRENT_SOURCE_DIR}/tile/TileProcessor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/tile/TileProcessor.h
  ${CMAKE_CURRENT_SOURCE_DIR}/tile/SparseCanvas.h
  
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/ImageComponentFlow.h
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/ImageComponentFlow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/Scheduler.h
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/Scheduler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/DecompressScheduler.h
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/DecompressScheduler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/CompressScheduler.h
  ${CMAKE_CURRENT_SOURCE_DIR}/scheduling/CompressScheduler.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/wavelet/WaveletFwd.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wavelet/WaveletFwd.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/wavelet/WaveletReverse.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/wavelet/WaveletReverse.h

  ${CMAKE_CURRENT_SOURCE_DIR}/t1/BlockExec.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/T1Factory.cpp  
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/T1Factory.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/T1Interface.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/Resolution.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/Precinct.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/Subband.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OJPH/T1OJPH.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OJPH/T1OJPH.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OJPH/QuantizerOJPH.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OJPH/QuantizerOJPH.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OJPH/coding/ojph_block_common.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OJPH/coding/ojph_block_decoder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OJPH/coding/ojph_block_decoder.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OJPH/coding/ojph_block_encoder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OJPH/coding/ojph_block_encoder.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OJPH/coding/table0.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OJPH/coding/table1.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OJPH/common/ojph_arch.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OJPH/common/ojph_defs.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OJPH/common/ojph_mem.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OJPH/others/ojph_mem.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/impl/T1.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/impl/T1.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/impl/mqc_enc.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/impl/mqc_dec.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/T1Part1.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1/T1Part1.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1//Quantizer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/part1//Quantizer.h
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
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/highway EXCLUDE_FROM_ALL)

add_library(${GROK_CORE_NAME} ${GROK_LIBRARY_SRCS})
set_target_properties(${GROK_CORE_NAME} PROPERTIES ${GROK_LIBRARY_PROPERTIES})
target_compile_options(${GROK_CORE_NAME} PRIVATE ${GROK_COMPILE_OPTIONS} PRIVATE ${HWY_FLAGS})
if (CMAKE_SYSTEM_NAME STREQUAL Emscripten)
  target_compile_options(${GROK_CORE_NAME} PUBLIC -matomics)
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

# bundle all static libraries into a single library
if (NOT BUILD_SHARED_LIBS AND NOT APPLE)
	bundle_static_library(${GROK_CORE_NAME} ${GROK_CORE_NAME}_bundled)
endif(NOT BUILD_SHARED_LIBS AND NOT APPLE)

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
