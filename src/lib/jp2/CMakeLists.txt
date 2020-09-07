include_regular_expression("^.*$")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

IF(MSVC)
    SET(CMAKE_CXX_FLAGS "/EHsc")
ENDIF(MSVC)

if(BUILD_UNIT_TESTS)
    SET(BUILD_STATIC_LIBS ON)
ENDIF()

IF(UNIX)
IF(BUILD_SHARED_LIBS AND NOT BUILD_STATIC_LIBS)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
    ENDIF()
ENDIF()
IF(AVX2_FOUND)
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2 -mbmi2")
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
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_part1
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht/coding
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht/common
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht/others
  ${CMAKE_CURRENT_SOURCE_DIR}/util
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers
  ${CMAKE_CURRENT_SOURCE_DIR}/point_transform
  ${CMAKE_CURRENT_SOURCE_DIR}/t2
)

# Defines the source code for executables
set(GROK_EXECUTABLES_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/util/test_sparse_array.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/bench_dwt.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_part1/t1_generate_luts.cpp
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
  ${CMAKE_CURRENT_SOURCE_DIR}/util/mem_stream.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/mem_stream.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/grk_intmath.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/MemManager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/MemManager.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/util.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/util.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/CPUArch.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/CPUArch.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/ChunkBuffer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/ChunkBuffer.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/grk_exceptions.h
  ${CMAKE_CURRENT_SOURCE_DIR}/util/testing.h
  
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/minpf_dynamic_library.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/minpf_dynamic_library.h
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/minpf_plugin_manager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/minpf_plugin_manager.h
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/minpf_common.h
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/minpf_plugin.h
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/plugin_interface.h
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/plugin_bridge.h
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin/plugin_bridge.cpp
      
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/BitIO.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/BitIO.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/IBitIO.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/Profile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/Profile.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/Dump.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/Dump.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/CodeStream.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/CodeStream.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/FileFormat.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/FileFormat.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/CodingParams.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/CodingParams.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/PacketIter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/PacketIter.h  
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/TagTree.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/TagTree.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/Quantizer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/Quantizer.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/HTParams.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/HTParams.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/LengthMarkers.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/LengthMarkers.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/SIZMarker.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/SIZMarker.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/PPMMarker.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/PPMMarker.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/SOTMarker.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/SOTMarker.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/markers.h
  ${CMAKE_CURRENT_SOURCE_DIR}/codestream/markers/markers.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/point_transform/mct.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/point_transform/mct.h

  ${CMAKE_CURRENT_SOURCE_DIR}/t2/T2.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/T2.h  
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/T2Encode.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/T2Encode.h  
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/T2Decode.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/T2Decode.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/RateControl.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/RateControl.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/RateInfo.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t2/RateInfo.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/grok.h
  ${CMAKE_CURRENT_SOURCE_DIR}/grok.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/grok.h
  ${CMAKE_CURRENT_SOURCE_DIR}/TileComponentBuffer.h
  ${CMAKE_CURRENT_SOURCE_DIR}/TileComponent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/TileComponent.h
  ${CMAKE_CURRENT_SOURCE_DIR}/TileProcessor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/TileProcessor.h
  ${CMAKE_CURRENT_SOURCE_DIR}/grk_includes.h

  ${CMAKE_CURRENT_SOURCE_DIR}/transform/Wavelet.h
  ${CMAKE_CURRENT_SOURCE_DIR}/transform/Wavelet.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/transform/sparse_array.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/transform/sparse_array.h
  ${CMAKE_CURRENT_SOURCE_DIR}/transform/dwt.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/transform/dwt.h
  ${CMAKE_CURRENT_SOURCE_DIR}/transform/WaveletForward.h
  ${CMAKE_CURRENT_SOURCE_DIR}/transform/dwt_utils.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/transform/dwt_utils.h
  ${CMAKE_CURRENT_SOURCE_DIR}/transform/dwt53.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/transform/dwt53.h
  ${CMAKE_CURRENT_SOURCE_DIR}/transform/dwt97.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/transform/dwt97.h
  
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/T1Decoder.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/T1Decoder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/T1Encoder.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/T1Encoder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/Tier1.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/Tier1.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/T1Factory.cpp  
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/T1Factory.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/T1Interface.h

  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht/T1HT.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht/T1HT.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht/coding/ojph_block_decoder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht/coding/ojph_block_decoder.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht/coding/ojph_block_encoder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht/coding/ojph_block_encoder.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht/coding/table0.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht/coding/table1.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht/common/ojph_arch.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht/common/ojph_arg.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht/common/ojph_base.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht/common/ojph_defs.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht/common/ojph_mem.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht/common/ojph_message.h
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht/others/ojph_arch.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht/others/ojph_mem.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_ht/others/ojph_message.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_part1/t1.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_part1/mqc_enc.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_part1/mqc_dec.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_part1/T1Part1.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_part1/T1Part1.h    
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
  if(BUILD_SHARED_LIBS AND BUILD_STATIC_LIBS)
      # Builds both static and dynamic libs, and install static version
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
if(UNIX)
  target_link_libraries(${GROK_LIBRARY_NAME} ${CMAKE_THREAD_LIBS_INIT})
endif()
set_target_properties(${GROK_LIBRARY_NAME} PROPERTIES ${GROK_LIBRARY_PROPERTIES})
target_compile_options(${GROK_LIBRARY_NAME} PRIVATE ${GROK_COMPILE_OPTIONS})


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

if(BUILD_DOC)
# install man page of the library
install(
  FILES       ${GROK_SOURCE_DIR}/doc/man/man3/libgrokj2k.3
  DESTINATION ${CMAKE_INSTALL_MANDIR}/man3)
endif()

if(BUILD_LUTS_GENERATOR)
# internal utility to generate t1_luts.h (part of the jp2 lib)
# no need to install:
add_executable(t1_generate_luts ${CMAKE_CURRENT_SOURCE_DIR}/t1/t1_part1/t1_generate_luts.cpp)
  if(UNIX)
    target_link_libraries(t1_generate_luts)
  endif()
endif()

# Experimental option; let's how cppcheck performs
# Implementation details:
# I could not figure out how to easily upload a file to CDash. Instead simply
# pretend cppcheck is part of the Build step. Technically cppcheck can even
# output gcc formatted error/warning report
# Another implementation detail: I could not redirect error to the error
# catching mechanism something is busted in cmake 2.8.5, I had to use the
# warning regex to catch them.
if(GROK_CPPCHECK)
  find_package(CPPCHECK REQUIRED)
  foreach(f ${GROK_EXECUTABLES_SRCS} ${GROK_LIBRARY_SRCS})
    # cppcheck complains about too many configuration, pretend to be WIN32:
    add_custom_command(TARGET ${GROK_LIBRARY_NAME}
      COMMAND ${CPPCHECK_EXECUTABLE} -DWIN32 ${f})
  endforeach()
endif()

if(GROK_USE_DSYMUTIL)
  if(BUILD_SHARED_LIBS)
    add_custom_command(TARGET ${GROK_LIBRARY_NAME} POST_BUILD
    COMMAND "dsymutil" "$<TARGET_SONAME_FILE:${GROK_LIBRARY_NAME}>"
    COMMENT "dsymutil $<TARGET_SONAME_FILE:${GROK_LIBRARY_NAME}>"
    DEPENDS ${GROK_LIBRARY_NAME})
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
    add_executable(test_sparse_array util/test_sparse_array.cpp)
    if(UNIX)
        target_link_libraries(test_sparse_array m ${GROK_LIBRARY_NAME})
    endif()
endif(BUILD_UNIT_TESTS)
