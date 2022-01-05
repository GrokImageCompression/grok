include_regular_expression("^.*$")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

IF(MSVC)
    string(APPEND CMAKE_CXX_FLAGS " /EHsc")
ENDIF(MSVC)

if(BUILD_UNIT_TESTS)
    SET(BUILD_STATIC_LIBS ON)
ENDIF()

IF(UNIX)
find_package(Valgrind)
if (Valgrind_FOUND)
	message(STATUS "Found Valgrind")
    add_definitions(-DGRK_HAVE_VALGRIND)
    include_directories(${Valgrind_INCLUDE_DIR})
endif(Valgrind_FOUND)
IF(BUILD_SHARED_LIBS AND NOT BUILD_STATIC_LIBS)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         string(APPEND CMAKE_CXX_FLAGS " -fvisibility=hidden")
    ENDIF()
ENDIF()
ENDIF(UNIX)


install( FILES  ${CMAKE_CURRENT_BINARY_DIR}/grk_config.h
 DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${GROK_INSTALL_SUBDIR} COMPONENT Headers)

include_directories(
  ${${GROK_NAMESPACE}_BINARY_DIR}/src/lib/jp2 # grk_config.h and grk_config_private.h
  ${GROK_SOURCE_DIR}/src/bin/common
  ${GROK_SOURCE_DIR}/src/bin/jp2
  ${GROK_SOURCE_DIR}/src/include
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin
  ${CMAKE_CURRENT_SOURCE_DIR}/transform
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
set(GROK_LIBRARY_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/util/BufferedStream.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/BufferedStream.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/IBufferedStream.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/logger.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/logger.h  
  ${CMAKE_CURRENT_SOURCE_DIR}/util/GrkMappedFile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/GrkMappedFile.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/MemStream.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/MemStream.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/grk_intmath.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/grk_intmath.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/util.h
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
  ${CMAKE_CURRENT_SOURCE_DIR}/cache/PacketLengthMarkers.h
  ${CMAKE_CURRENT_SOURCE_DIR}/cache/PacketLengthMarkers.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/cache/PacketLengthCache.h
  ${CMAKE_CURRENT_SOURCE_DIR}/cache/PacketLengthCache.cpp

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
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/TagTree.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/BitIO.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/BitIO.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/IBitIO.h

  ${CMAKE_CURRENT_SOURCE_DIR}/grok.h
  ${CMAKE_CURRENT_SOURCE_DIR}/grok.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/grok.h
  ${CMAKE_CURRENT_SOURCE_DIR}/grk_includes.h  
    
  ${CMAKE_CURRENT_SOURCE_DIR}/tile/TileComponentWindowBuffer.h
  ${CMAKE_CURRENT_SOURCE_DIR}/tile/TileComponent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/tile/TileComponent.h
  ${CMAKE_CURRENT_SOURCE_DIR}/tile/TileProcessor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/tile/TileProcessor.h
  ${CMAKE_CURRENT_SOURCE_DIR}/tile/SparseCanvas.h

  ${CMAKE_CURRENT_SOURCE_DIR}/transform/WaveletFwd.h
  ${CMAKE_CURRENT_SOURCE_DIR}/transform/WaveletFwd.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/transform/WaveletReverse.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/transform/WaveletReverse.h

  ${CMAKE_CURRENT_SOURCE_DIR}/t1/BlockExec.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/T1DecompressScheduler.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/T1DecompressScheduler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/T1CompressScheduler.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/T1CompressScheduler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/T1Factory.cpp  
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/T1Factory.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/T1Interface.h

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

  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OJPH/T1OJPH.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OJPH/T1OJPH.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OJPH/QuantizerOJPH.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/OJPH/QuantizerOJPH.cpp
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

# Build the library
if (BUILD_PLUGIN_LOADER)
    add_definitions(-DGRK_BUILD_PLUGIN_LOADER)
endif()
if(WIN32)
  if(BUILD_SHARED_LIBS)
    add_definitions(-DGRK_EXPORTS)
  else()
    add_definitions(-DGRK_STATIC)
  endif()
  add_library(${GROK_LIBRARY_NAME} ${GROK_LIBRARY_SRCS})
  set(INSTALL_LIBS ${GROK_LIBRARY_NAME})
else()
  if(BUILD_STATIC_LIBS)
      # Builds and installs both static and dynamic libraries
	  add_library(${GROK_LIBRARY_NAME} SHARED ${GROK_LIBRARY_SRCS})
	  add_library(${GROK_LIBRARY_NAME}_static STATIC ${GROK_LIBRARY_SRCS})
	  set_target_properties(${GROK_LIBRARY_NAME}_static PROPERTIES OUTPUT_NAME ${GROK_LIBRARY_NAME})
	  set(INSTALL_LIBS ${GROK_LIBRARY_NAME} ${GROK_LIBRARY_NAME}_static)
  else()
	  add_library(${GROK_LIBRARY_NAME} ${GROK_LIBRARY_SRCS})
	  set(INSTALL_LIBS ${GROK_LIBRARY_NAME})
  endif()
endif()
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	target_link_options(${GROK_LIBRARY_NAME} PUBLIC "LINKER:-z,now")
endif() 
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/highway EXCLUDE_FROM_ALL)
if(UNIX)
  target_link_libraries(${GROK_LIBRARY_NAME} PUBLIC ${CMAKE_THREAD_LIBS_INIT} PRIVATE hwy lcms2)
else()
  target_link_libraries(${GROK_LIBRARY_NAME} PRIVATE hwy lcms2)
endif()
set_target_properties(${GROK_LIBRARY_NAME} PROPERTIES ${GROK_LIBRARY_PROPERTIES})
target_compile_options(${GROK_LIBRARY_NAME} PRIVATE ${GROK_COMPILE_OPTIONS} PRIVATE ${HWY_FLAGS})

# Install library
install(TARGETS ${INSTALL_LIBS}
  EXPORT GrokTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Applications
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Libraries
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Libraries
)

# Install includes files
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

if(BUILD_UNIT_TESTS)
    add_executable(bench_dwt 
				    util/bench_dwt.cpp
				    ${GROK_SOURCE_DIR}/src/bin/common/spdlog/spdlog.cpp
				    ${GROK_SOURCE_DIR}/src/bin/common/spdlog/color_sinks.cpp
				    ${GROK_SOURCE_DIR}/src/bin/common/spdlog/stdout_sinks.cpp
				    ${GROK_SOURCE_DIR}/src/bin/common/spdlog/fmt.cpp
				    ${GROK_SOURCE_DIR}/src/bin/common/spdlog/async.cpp     
				    ${GROK_SOURCE_DIR}/src/bin/common/spdlog/file_sinks.cpp )
    if(UNIX)
        target_link_libraries(bench_dwt m ${GROK_LIBRARY_NAME})
    endif()
endif(BUILD_UNIT_TESTS)
