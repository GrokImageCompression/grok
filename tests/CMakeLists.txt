# Copyright (C) 2016-2026 Grok Image Compression Inc.
#
# This source code is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License, version 3,
# as published by the Free Software Foundation.
#
# This source code is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.


include_directories(
  ${CMAKE_CURRENT_BINARY_DIR}/../src/lib/core # grk_config.h
  ${CMAKE_CURRENT_BINARY_DIR}/../src/bin/ # grk_apps_config.h
  ${CMAKE_CURRENT_BINARY_DIR}/../src/lib/codec/ # grk_apps_config.h
  ${GROK_SOURCE_DIR}/src/lib/codec
  ${GROK_SOURCE_DIR}/src/lib/core
  ${GROK_SOURCE_DIR}/src/include
  ${GROK_SOURCE_DIR}/src/include/CLI11/include
)
find_package(Threads REQUIRED)

add_executable(compare_images compare_images.cpp)
target_link_libraries(compare_images ${GROK_CODEC_NAME})

add_executable(image_canon_dump image_canon_dump.cpp)
target_link_libraries(image_canon_dump ${GROK_CODEC_NAME})

add_executable(compare_dump_files compare_dump_files.cpp GrkCompareDumpFiles.cpp )
target_link_libraries(compare_dump_files ${GROK_CORE_NAME} spdlog::spdlog)

add_executable(j2k_random_tile_access j2k_random_tile_access.cpp GrkRandomTileAccess.cpp)
target_link_libraries(j2k_random_tile_access ${GROK_CORE_NAME} spdlog::spdlog)

add_executable(j2k_multi_region_decompress j2k_multi_region_decompress.cpp GrkMultiRegionDecompress.cpp)
target_link_libraries(j2k_multi_region_decompress ${GROK_CORE_NAME} spdlog::spdlog)

add_executable(grk_lru_cache_test grk_lru_cache_test.cpp GrkLRUCacheTest.cpp)
target_include_directories(grk_lru_cache_test PRIVATE
  ${GROK_SOURCE_DIR}/src/lib/core/cache
  ${GROK_SOURCE_DIR}/src/lib/core/t2
  ${CMAKE_BINARY_DIR}/src/lib/core
)
target_link_libraries(grk_lru_cache_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_lru_cache_test COMMAND grk_lru_cache_test)

add_executable(grk_per_layer_values_test grk_per_layer_values_test.cpp GrkPerLayerValuesTest.cpp)
target_include_directories(grk_per_layer_values_test PRIVATE
  ${GROK_SOURCE_DIR}/src/lib/core/t1/codeblock
  ${GROK_SOURCE_DIR}/src/lib/core/t1/part1/block_coder
  ${GROK_SOURCE_DIR}/src/lib/core/util
  ${CMAKE_BINARY_DIR}/src/lib/core
)
target_link_libraries(grk_per_layer_values_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_per_layer_values_test COMMAND grk_per_layer_values_test)

add_executable(grk_dwt_bench grk_dwt_bench.cpp)
target_link_libraries(grk_dwt_bench ${GROK_CORE_NAME})

add_executable(grk_concurrency_test grk_concurrency_test.cpp GrkConcurrencyTest.cpp)
target_include_directories(grk_concurrency_test PRIVATE
  ${CMAKE_BINARY_DIR}/src/lib/core
)
target_link_libraries(grk_concurrency_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_concurrency_test COMMAND grk_concurrency_test)

add_executable(compare_raw_files compare_raw_files.cpp GrkCompareRawFiles.cpp)
target_link_libraries(compare_raw_files ${GROK_CORE_NAME} spdlog::spdlog)

add_executable(grk_rate_control_test GrkRateControlTest.cpp)
target_link_libraries(grk_rate_control_test ${GROK_CORE_NAME})
add_test(NAME grk_rate_control_test COMMAND grk_rate_control_test)

# synthesizes its own image, so it needs no GRK_DATA_ROOT
add_executable(grk_compress_data_type_test GrkCompressDataTypeTest.cpp)
target_link_libraries(grk_compress_data_type_test ${GROK_CORE_NAME})
add_test(NAME grk_compress_data_type_test COMMAND grk_compress_data_type_test)

# synthesizes its own frames, so it needs no GRK_DATA_ROOT
add_executable(grk_cinema_eight_bit_test GrkCinemaEightBitTest.cpp)
target_link_libraries(grk_cinema_eight_bit_test ${GROK_CORE_NAME})
add_test(NAME grk_cinema_eight_bit_test COMMAND grk_cinema_eight_bit_test)

# the accelerator plugin under grk_compress and grk_decompress on memory buffers;
# needs the plugin library and a device, so only with the loader built
if(GRK_BUILD_PLUGIN_LOADER)
  add_executable(grk_plugin_memory_test GrkPluginMemoryTest.cpp)
  target_link_libraries(grk_plugin_memory_test ${GROK_CORE_NAME})
  add_test(NAME grk_plugin_memory_test COMMAND grk_plugin_memory_test)
  set_tests_properties(grk_plugin_memory_test PROPERTIES
    ENVIRONMENT "GRK_PLUGIN_PATH=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")

  add_executable(grk_plugin_batch_memory_test GrkPluginBatchMemoryTest.cpp)
  target_link_libraries(grk_plugin_batch_memory_test ${GROK_CORE_NAME})
  add_test(NAME grk_plugin_batch_memory_test COMMAND grk_plugin_batch_memory_test)
  set_tests_properties(grk_plugin_batch_memory_test PROPERTIES
    ENVIRONMENT "GRK_PLUGIN_PATH=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
endif()

# synthesizes its own codestream, so it needs no GRK_DATA_ROOT
add_executable(grk_int16_eligibility_test GrkInt16EligibilityTest.cpp)
target_link_libraries(grk_int16_eligibility_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_int16_eligibility_test COMMAND grk_int16_eligibility_test)

add_executable(grk_sycc_rounding_test GrkSyccRoundingTest.cpp)
target_include_directories(grk_sycc_rounding_test PRIVATE
  ${GROK_SOURCE_DIR}/src/lib/core/util
  ${CMAKE_BINARY_DIR}/src/lib/core
)
target_link_libraries(grk_sycc_rounding_test ${GROK_CORE_NAME})
add_test(NAME grk_sycc_rounding_test COMMAND grk_sycc_rounding_test)

add_executable(grk_corrupt_stream_determinism_test GrkCorruptStreamDeterminismTest.cpp)
target_link_libraries(grk_corrupt_stream_determinism_test ${GROK_CORE_NAME})
if(GRK_DATA_ROOT)
  add_test(NAME grk_corrupt_stream_determinism_test
    COMMAND grk_corrupt_stream_determinism_test
            ${GRK_DATA_ROOT}/input/nonregression/crash-ea90940e12a27e75c1a9aaa7c39079c43576f175)
endif()

add_executable(grk_truncated_components_memory_test GrkTruncatedComponentsMemoryTest.cpp)
target_link_libraries(grk_truncated_components_memory_test ${GROK_CORE_NAME})
if(GRK_DATA_ROOT)
  add_test(NAME grk_truncated_components_memory_test
    COMMAND grk_truncated_components_memory_test
            ${GRK_DATA_ROOT}/input/nonregression/oom-dd67eec3c81d56b3dec670a93acfe50fe88d03c6)
endif()

add_executable(grk_rescale_test GrkRescaleTest.cpp)
target_include_directories(grk_rescale_test PRIVATE
  ${GROK_SOURCE_DIR}/src/lib/core/util
  ${CMAKE_BINARY_DIR}/src/lib/core
)
target_link_libraries(grk_rescale_test ${GROK_CORE_NAME})
add_test(NAME grk_rescale_test COMMAND grk_rescale_test)

if(GRK_ENABLE_MERCURY)
  add_executable(grk_mercury_stream_input_test GrkMercuryStreamInputTest.cpp)
  target_link_libraries(grk_mercury_stream_input_test ${GROK_CORE_NAME} spdlog::spdlog)
  if(GRK_DATA_ROOT)
    add_test(NAME grk_mercury_stream_input_test
      COMMAND grk_mercury_stream_input_test ${GRK_DATA_ROOT}/input/nonregression/foo.j2k)
    # jp2 puts the codestream at a non-zero offset, so first_sot_off is non-zero
    add_test(NAME grk_mercury_stream_input_jp2_test
      COMMAND grk_mercury_stream_input_test ${GRK_DATA_ROOT}/input/nonregression/cinema129x129.jp2)
  endif()

  add_executable(grk_mercury_reduce_test GrkMercuryReduceTest.cpp)
  target_link_libraries(grk_mercury_reduce_test ${GROK_CORE_NAME} spdlog::spdlog)
  if(GRK_DATA_ROOT)
    add_test(NAME grk_mercury_reduce_test
      COMMAND grk_mercury_reduce_test ${GRK_DATA_ROOT}/input/nonregression/foo.j2k)
    # 9/7: stopping the synthesis chain early changes the per-level gain
    # normalization, so compare within the A/B sweep's peak tolerance
    add_test(NAME grk_mercury_reduce_irreversible_test
      COMMAND grk_mercury_reduce_test ${GRK_DATA_ROOT}/input/nonregression/foo_irrev.j2k
              --irreversible)
  endif()

  # synthesizes its own codestreams, so it needs no GRK_DATA_ROOT
  add_executable(grk_mercury_progressions_test GrkMercuryProgressionsTest.cpp)
  target_link_libraries(grk_mercury_progressions_test ${GROK_CORE_NAME} spdlog::spdlog)
  add_test(NAME grk_mercury_progressions_test COMMAND grk_mercury_progressions_test)

  add_executable(grk_mercury_layers_test GrkMercuryLayersTest.cpp)
  target_link_libraries(grk_mercury_layers_test ${GROK_CORE_NAME} spdlog::spdlog)
  if(GRK_DATA_ROOT)
    # 16 reversible quality layers, so every truncation point is exercised
    add_test(NAME grk_mercury_layers_test
      COMMAND grk_mercury_layers_test ${GRK_DATA_ROOT}/input/nonregression/pacs.ge.j2k)
  endif()

  # synthesizes its own codestream, so it needs no GRK_DATA_ROOT
  add_executable(grk_mercury_tile_row_race_test GrkMercuryTileRowRaceTest.cpp)
  target_link_libraries(grk_mercury_tile_row_race_test ${GROK_CORE_NAME} spdlog::spdlog)
  add_test(NAME grk_mercury_tile_row_race_test COMMAND grk_mercury_tile_row_race_test)

  # synthesizes its own codestream, so it needs no GRK_DATA_ROOT
  add_executable(grk_repeat_decompress_test GrkRepeatDecompressTest.cpp)
  target_link_libraries(grk_repeat_decompress_test ${GROK_CORE_NAME} spdlog::spdlog)
  add_test(NAME grk_repeat_decompress_test COMMAND grk_repeat_decompress_test)

  # synthesizes its own codestreams, so it needs no GRK_DATA_ROOT
  add_executable(grk_mercury_window_test GrkMercuryWindowTest.cpp)
  target_link_libraries(grk_mercury_window_test ${GROK_CORE_NAME} spdlog::spdlog)
  add_test(NAME grk_mercury_window_test COMMAND grk_mercury_window_test)

  # synthesizes its own codestream, so it needs no GRK_DATA_ROOT
  add_executable(grk_mercury_plt_test GrkMercuryPltTest.cpp)
  target_link_libraries(grk_mercury_plt_test ${GROK_CORE_NAME} spdlog::spdlog)
  add_test(NAME grk_mercury_plt_test COMMAND grk_mercury_plt_test)

  # synthesizes its own codestreams, so it needs no GRK_DATA_ROOT
  add_executable(grk_mercury_precinct_clamp_test GrkMercuryPrecinctClampTest.cpp)
  target_link_libraries(grk_mercury_precinct_clamp_test ${GROK_CORE_NAME} spdlog::spdlog)
  add_test(NAME grk_mercury_precinct_clamp_test COMMAND grk_mercury_precinct_clamp_test)

  add_executable(grk_mercury_composite_type_test GrkMercuryCompositeTypeTest.cpp)
  target_link_libraries(grk_mercury_composite_type_test ${GROK_CORE_NAME} spdlog::spdlog)
  if(GRK_DATA_ROOT)
    add_test(NAME grk_mercury_composite_type_test
      COMMAND grk_mercury_composite_type_test
              ${GRK_DATA_ROOT}/input/nonregression/clusterfuzz-testcase-minimized-grk_decompress_mercury_fuzzer-6270477761576960)
  endif()
endif()

# synthesizes its own codestream, so it needs no GRK_DATA_ROOT
add_executable(grk_tile_part_rejection_race_test GrkTilePartRejectionRaceTest.cpp)
target_link_libraries(grk_tile_part_rejection_race_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_tile_part_rejection_race_test COMMAND grk_tile_part_rejection_race_test)

# Regression for an OSS-Fuzz leak: a late tile part of an already-scheduled tile
# re-initialized the tile while its decompress was running, leaking precincts and
# racing the tile release into a use-after-free.
add_executable(grk_late_tile_part_reschedule_test GrkLateTilePartRescheduleTest.cpp)
target_link_libraries(grk_late_tile_part_reschedule_test ${GROK_CORE_NAME} spdlog::spdlog)
if(GRK_DATA_ROOT)
  add_test(NAME grk_late_tile_part_reschedule_test
    COMMAND grk_late_tile_part_reschedule_test
            ${GRK_DATA_ROOT}/input/nonregression/clusterfuzz-testcase-minimized-grk_decompress_fuzzer-6359702766944256)
  set_tests_properties(grk_late_tile_part_reschedule_test PROPERTIES TIMEOUT 120)
endif()

# Regression for an OSS-Fuzz timeout: a bogus precinct grid with PLT markers made
# T2 packet parsing skip corrupt packets across the whole grid without end. The
# test must RETURN; the TIMEOUT turns a regression hang into a failure.
add_executable(grk_t2_corrupt_packet_bound_test GrkT2CorruptPacketBoundTest.cpp)
target_link_libraries(grk_t2_corrupt_packet_bound_test ${GROK_CORE_NAME} spdlog::spdlog)
if(GRK_DATA_ROOT)
  add_test(NAME grk_t2_corrupt_packet_bound_test
    COMMAND grk_t2_corrupt_packet_bound_test
            ${GRK_DATA_ROOT}/input/nonregression/clusterfuzz-testcase-minimized-grk_decompress_mercury_fuzzer-6491002333102080)
  set_tests_properties(grk_t2_corrupt_packet_bound_test PROPERTIES TIMEOUT 60)
endif()

# Regression for an OSS-Fuzz timeout: an RPCL tile 2.2 billion columns wide with
# component dy=255 made the packet iterator scan every x on a row that aligns to
# no precinct. The test must RETURN; the TIMEOUT turns a regression hang into a
# failure.
add_executable(grk_packet_iter_row_skip_test GrkPacketIterRowSkipTest.cpp)
target_link_libraries(grk_packet_iter_row_skip_test ${GROK_CORE_NAME} spdlog::spdlog)
if(GRK_DATA_ROOT)
  add_test(NAME grk_packet_iter_row_skip_test
    COMMAND grk_packet_iter_row_skip_test
            ${GRK_DATA_ROOT}/input/nonregression/clusterfuzz-testcase-minimized-grk_decompress_fuzzer-6420654795325440)
  set_tests_properties(grk_packet_iter_row_skip_test PROPERTIES TIMEOUT 60)
endif()

# Regression for an OSS-Fuzz timeout: a ~2-billion-column CPRL tile with a
# POC whose end layer is 0 made the packet iterator scan every x looking for
# a packet it could never yield. The test must RETURN; the TIMEOUT turns a
# regression hang into a failure.
add_executable(grk_packet_iter_empty_poc_test GrkPacketIterEmptyPocTest.cpp)
target_link_libraries(grk_packet_iter_empty_poc_test ${GROK_CORE_NAME} spdlog::spdlog)
if(GRK_DATA_ROOT)
  add_test(NAME grk_packet_iter_empty_poc_test
    COMMAND grk_packet_iter_empty_poc_test
            ${GRK_DATA_ROOT}/input/nonregression/clusterfuzz-testcase-minimized-grk_decompress_fuzzer-6042331325988864)
  set_tests_properties(grk_packet_iter_empty_poc_test PROPERTIES TIMEOUT 60)
endif()

# Regression for an OSS-Fuzz out-of-memory: a stream declaring 58368 quality
# layers made T2 packet parsing create a block per code block in the precinct.
# The test fails if peak resident memory exceeds 1 GB.
add_executable(grk_packet_parser_oom_test GrkPacketParserOomTest.cpp)
target_link_libraries(grk_packet_parser_oom_test ${GROK_CORE_NAME} spdlog::spdlog)
if(WIN32)
  target_link_libraries(grk_packet_parser_oom_test psapi)
endif()
if(GRK_DATA_ROOT)
  add_test(NAME grk_packet_parser_oom_test
    COMMAND grk_packet_parser_oom_test
            ${GRK_DATA_ROOT}/input/nonregression/clusterfuzz-testcase-minimized-grk_decompress_fuzzer-6299706624638976)
  set_tests_properties(grk_packet_parser_oom_test PROPERTIES TIMEOUT 60)
endif()

# Regression for an OSS-Fuzz out-of-memory: a stream declaring a canvas
# 0x2020ff20 samples wide made the mercury plan size its block-record tables
# from the declared geometry instead of the decode window.
# The test fails if peak resident memory exceeds 1 GB.
add_executable(grk_mercury_plan_oom_test GrkMercuryPlanOomTest.cpp)
target_link_libraries(grk_mercury_plan_oom_test ${GROK_CORE_NAME} spdlog::spdlog)
if(WIN32)
  target_link_libraries(grk_mercury_plan_oom_test psapi)
endif()
if(GRK_DATA_ROOT)
  add_test(NAME grk_mercury_plan_oom_test
    COMMAND grk_mercury_plan_oom_test
            ${GRK_DATA_ROOT}/input/nonregression/clusterfuzz-testcase-minimized-grk_decompress_mercury_fuzzer-6390098506350592)
  set_tests_properties(grk_mercury_plan_oom_test PROPERTIES TIMEOUT 60)
endif()

# Regression for an OSS-Fuzz signed overflow in load_h_p0_53.
add_executable(grk_wavelet_53_overflow_test GrkWavelet53OverflowTest.cpp)
target_link_libraries(grk_wavelet_53_overflow_test ${GROK_CORE_NAME} spdlog::spdlog)
if(GRK_DATA_ROOT)
  add_test(NAME grk_wavelet_53_overflow_test
    COMMAND grk_wavelet_53_overflow_test
            ${GRK_DATA_ROOT}/input/nonregression/clusterfuzz-testcase-minimized-grk_decompress_mercury_fuzzer-6413516559679488)
  set_tests_properties(grk_wavelet_53_overflow_test PROPERTIES TIMEOUT 60)
endif()

# synthesizes its own codestreams, so it needs no GRK_DATA_ROOT
add_executable(grk_short_tile_round_trip_test GrkShortTileRoundTripTest.cpp)
target_link_libraries(grk_short_tile_round_trip_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_short_tile_round_trip_test COMMAND grk_short_tile_round_trip_test)

add_executable(grk_truncated_stream_test GrkTruncatedStreamTest.cpp)
target_link_libraries(grk_truncated_stream_test ${GROK_CORE_NAME} spdlog::spdlog)
if(GRK_DATA_ROOT)
  add_test(NAME grk_truncated_stream_test
    COMMAND grk_truncated_stream_test
            ${GRK_DATA_ROOT}/input/nonregression/multi_tile_truncated_rgb.jp2)
endif()

add_executable(grk_jp2_metadata_test grk_jp2_metadata_test.cpp GrkJP2MetadataTest.cpp)
target_link_libraries(grk_jp2_metadata_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_jp2_metadata_test COMMAND grk_jp2_metadata_test)

add_executable(grk_mj2_test grk_mj2_test.cpp GrkMJ2Test.cpp)
target_link_libraries(grk_mj2_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_mj2_test COMMAND grk_mj2_test)

add_executable(grk_messenger_test grk_messenger_test.cpp GrkMessengerTest.cpp)
target_include_directories(grk_messenger_test PRIVATE
  ${GROK_SOURCE_DIR}/src/lib/codec/shared
)
target_link_libraries(grk_messenger_test Threads::Threads)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(grk_messenger_test rt)
endif()
add_test(NAME grk_messenger_test COMMAND grk_messenger_test)
set_tests_properties(grk_messenger_test PROPERTIES RESOURCE_LOCK shm_grok)

add_executable(grk_messenger_loopback grk_messenger_loopback.cpp)
target_include_directories(grk_messenger_loopback PRIVATE
  ${GROK_SOURCE_DIR}/src/lib/codec/shared
)
target_link_libraries(grk_messenger_loopback Threads::Threads)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(grk_messenger_loopback rt)
endif()
add_test(NAME grk_messenger_loopback COMMAND grk_messenger_loopback)
set_tests_properties(grk_messenger_loopback PROPERTIES RESOURCE_LOCK shm_grok)

add_executable(grk_shm_client_init_test GrkShmClientInitTest.cpp)
target_include_directories(grk_shm_client_init_test PRIVATE
  ${GROK_SOURCE_DIR}/src/lib/codec/shared
)
target_link_libraries(grk_shm_client_init_test Threads::Threads)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(grk_shm_client_init_test rt)
endif()
add_test(NAME grk_shm_client_init_test COMMAND grk_shm_client_init_test)
set_tests_properties(grk_shm_client_init_test PROPERTIES RESOURCE_LOCK shm_grok)

add_executable(grk_shm_batch_compress grk_shm_batch_compress.cpp)
target_include_directories(grk_shm_batch_compress PRIVATE
  ${GROK_SOURCE_DIR}/src/lib/codec/shared
)
target_link_libraries(grk_shm_batch_compress ${GROK_CORE_NAME} Threads::Threads)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(grk_shm_batch_compress rt)
endif()
add_test(NAME grk_shm_batch_compress
  COMMAND grk_shm_batch_compress $<TARGET_FILE:grk_compress>)
set_tests_properties(grk_shm_batch_compress PROPERTIES RESOURCE_LOCK shm_grok)

add_executable(grk_shm_batch_decompress grk_shm_batch_decompress.cpp)
target_include_directories(grk_shm_batch_decompress PRIVATE
  ${GROK_SOURCE_DIR}/src/lib/codec/shared
)
target_link_libraries(grk_shm_batch_decompress ${GROK_CORE_NAME} Threads::Threads)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(grk_shm_batch_decompress rt)
endif()
add_test(NAME grk_shm_batch_decompress
  COMMAND grk_shm_batch_decompress $<TARGET_FILE:grk_decompress>)
set_tests_properties(grk_shm_batch_decompress PROPERTIES RESOURCE_LOCK shm_grok)

if(NOT GROK_HAVE_LIBPNG)
  message(WARNING "libpng is not available - running regression tests requires GRK_BUILD_LIBPNG enabled.")
endif()

# synthesizes its own codestream, so it needs no GRK_DATA_ROOT
add_executable(grk_region_decompress_test GrkRegionDecompressTest.cpp)
target_link_libraries(grk_region_decompress_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_region_decompress_test COMMAND grk_region_decompress_test)

# synthesizes its own codestreams, so it needs no GRK_DATA_ROOT
add_executable(grk_region_decompress_int16_test GrkRegionDecompressInt16Test.cpp)
target_link_libraries(grk_region_decompress_int16_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_region_decompress_int16_test COMMAND grk_region_decompress_int16_test)

# synthesizes its own codestreams, so it needs no GRK_DATA_ROOT
add_executable(grk_region_decompress_97_int16_test GrkRegionDecompress97Int16Test.cpp)
target_link_libraries(grk_region_decompress_97_int16_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_region_decompress_97_int16_test COMMAND grk_region_decompress_97_int16_test)

# synthesizes its own codestreams, so it needs no GRK_DATA_ROOT
add_executable(grk_region_decompress_ht_test GrkRegionDecompressHTTest.cpp)
target_link_libraries(grk_region_decompress_ht_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_region_decompress_ht_test COMMAND grk_region_decompress_ht_test)

# synthesizes its own codestreams, so it needs no GRK_DATA_ROOT
add_executable(grk_reduced_decompress_97_int16_test GrkReducedDecompress97Int16Test.cpp)
target_link_libraries(grk_reduced_decompress_97_int16_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_reduced_decompress_97_int16_test COMMAND grk_reduced_decompress_97_int16_test)

# synthesizes its own codestreams, so it needs no GRK_DATA_ROOT
add_executable(grk_window_change_decompress_test GrkWindowChangeDecompressTest.cpp)
target_link_libraries(grk_window_change_decompress_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_window_change_decompress_test COMMAND grk_window_change_decompress_test)

add_subdirectory(conformance)
add_subdirectory(nonregression)

# output-side regression tests
add_executable(grk_incomplete_decode_test GrkIncompleteDecodeTest.cpp)
target_link_libraries(grk_incomplete_decode_test ${GROK_CODEC_NAME})
if(GRK_DATA_ROOT)
  # both codestreams stop before any tile row completes, so the tif never gets pixels
  add_test(NAME grk_incomplete_decode_issue363_test
    COMMAND grk_incomplete_decode_test
            ${GRK_DATA_ROOT}/input/nonregression/issue363-4740.jp2
            ${GRK_DATA_ROOT}/input/nonregression/foo.j2k)
  add_test(NAME grk_incomplete_decode_issue391_test
    COMMAND grk_incomplete_decode_test
            ${GRK_DATA_ROOT}/input/nonregression/issue391.jp2
            ${GRK_DATA_ROOT}/input/nonregression/foo.j2k)
endif()

add_executable(grk_differential_layers_test GrkDifferentialLayersTest.cpp)
target_link_libraries(grk_differential_layers_test ${GROK_CORE_NAME} spdlog::spdlog)
if(GRK_DATA_ROOT)
  # TERMALL: every coding pass is its own segment
  add_test(NAME grk_differential_layers_p0_04_test
    COMMAND grk_differential_layers_test
            ${GRK_DATA_ROOT}/input/conformance/p0_04.j2k)
endif()

add_executable(grk_subsampled_tiff_readback_test GrkSubsampledTiffReadbackTest.cpp)
target_link_libraries(grk_subsampled_tiff_readback_test ${GROK_CODEC_NAME})
if(GRK_DATA_ROOT)
  # 10 bit 4:2:2, where a clump sample no longer fits in one byte
  add_test(NAME grk_subsampled_tiff_readback_issue142_test
    COMMAND grk_subsampled_tiff_readback_test
            ${GRK_DATA_ROOT}/input/nonregression/issue142.j2k)
  # 8 bit 4:2:0, where clump samples stay byte aligned
  add_test(NAME grk_subsampled_tiff_readback_ycc420_test
    COMMAND grk_subsampled_tiff_readback_test
            ${GRK_DATA_ROOT}/input/nonregression/issue411-ycc420.jp2)
endif()

# synthesizes its own images, so it needs no GRK_DATA_ROOT
add_executable(grk_degenerate_97_test GrkDegenerate97Test.cpp)
target_link_libraries(grk_degenerate_97_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_degenerate_97_test COMMAND grk_degenerate_97_test)

# synthesizes its own images, so it needs no GRK_DATA_ROOT
add_executable(grk_irreversible_lifting_test GrkIrreversibleLiftingTest.cpp)
target_link_libraries(grk_irreversible_lifting_test ${GROK_CORE_NAME})
add_test(NAME grk_irreversible_lifting_test COMMAND grk_irreversible_lifting_test)

add_executable(grk_rate_allocation_test GrkRateAllocationTest.cpp)
target_link_libraries(grk_rate_allocation_test ${GROK_CORE_NAME})
add_test(NAME grk_rate_allocation_test COMMAND grk_rate_allocation_test)

# synthesizes its own codestream, so it needs no GRK_DATA_ROOT
add_executable(grk_ht_band_bit_planes_test GrkHTBandBitPlanesTest.cpp)
target_link_libraries(grk_ht_band_bit_planes_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_ht_band_bit_planes_test COMMAND grk_ht_band_bit_planes_test)

# synthesizes its own image, so it needs no GRK_DATA_ROOT
add_executable(grk_flat_image_rate_control_test GrkFlatImageRateControlTest.cpp)
target_link_libraries(grk_flat_image_rate_control_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_flat_image_rate_control_test COMMAND grk_flat_image_rate_control_test)

add_executable(grk_qfactor_test GrkQfactorTest.cpp)
target_link_libraries(grk_qfactor_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_qfactor_test COMMAND grk_qfactor_test)

# synthesizes its own image, so it needs no GRK_DATA_ROOT
add_executable(grk_decode_range_test GrkDecodeRangeTest.cpp)
target_link_libraries(grk_decode_range_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_decode_range_test COMMAND grk_decode_range_test)

# synthesizes its own image, so it needs no GRK_DATA_ROOT
add_executable(grk_int32_reversible_53_test GrkInt32Reversible53Test.cpp)
target_link_libraries(grk_int32_reversible_53_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_int32_reversible_53_test COMMAND grk_int32_reversible_53_test)

# synthesizes its own codestreams, so it needs no GRK_DATA_ROOT
add_executable(grk_double_decompress_test GrkDoubleDecompressTest.cpp)
target_link_libraries(grk_double_decompress_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_double_decompress_test COMMAND grk_double_decompress_test)

# synthesizes its own images, so it needs no GRK_DATA_ROOT
add_executable(grk_compare_images_test GrkCompareImagesTest.cpp)
target_link_libraries(grk_compare_images_test ${GROK_CODEC_NAME})
add_test(NAME grk_compare_images_test
  COMMAND grk_compare_images_test ${CMAKE_CURRENT_BINARY_DIR}/Temporary)

# a network stream builds its fetcher before the path is known to be usable
if(GRK_ENABLE_LIBCURL AND GRK_HAVE_LIBCURL AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(grk_fetch_stream_leak_test GrkFetchStreamLeakTest.cpp)
  target_link_libraries(grk_fetch_stream_leak_test ${GROK_CORE_NAME} spdlog::spdlog)
  add_test(NAME grk_fetch_stream_leak_test COMMAND grk_fetch_stream_leak_test)
endif()

# ycbcr at 1,1 subsampling stores plain interleaved triples rather than clumps, and needs
# a jp2 intermediate because a raw codestream cannot record the colour space
if(GRK_DATA_ROOT)
  add_test(NAME grk_nonsubsampled_ycbcr_tiff_readback_ycc444_test
    COMMAND grk_subsampled_tiff_readback_test
            ${GRK_DATA_ROOT}/input/nonregression/issue411-ycc444.jp2 jp2)
  add_test(NAME grk_nonsubsampled_ycbcr_tiff_readback_esycc_test
    COMMAND grk_subsampled_tiff_readback_test
            ${GRK_DATA_ROOT}/input/nonregression/issue236-ESYCC-CDEF.jp2 jp2)

  # tiff cannot record eYCC apart from rec.601 sYCC, so the writer has to say so.
  # GRK_DEBUG=2 lifts the log level from errors to warnings
  add_test(NAME grk_eycc_tiff_warning_test
    COMMAND grk_decompress
            -i ${GRK_DATA_ROOT}/input/nonregression/issue236-ESYCC-CDEF.jp2
            -o ${CMAKE_CURRENT_BINARY_DIR}/eycc_tiff_warning.tif)
  set_tests_properties(grk_eycc_tiff_warning_test PROPERTIES
    ENVIRONMENT "GRK_DEBUG=2"
    PASS_REGULAR_EXPRESSION "eYCC is recorded as rec.601 YCbCr")
endif()

# needs page sized files, so it only runs where the page size is known
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(grk_mapped_offset_test GrkMappedOffsetTest.cpp)
  target_link_libraries(grk_mapped_offset_test ${GROK_CORE_NAME} spdlog::spdlog)
  add_test(NAME grk_mapped_offset_test COMMAND grk_mapped_offset_test)
endif()

# serves range requests from the loopback address, so it needs no object store.
# it redefines two curl symbols to count them, which collides with a libcurl
# linked statically into the test, so it needs the shared grok library
if(GRK_ENABLE_LIBCURL AND GRK_HAVE_LIBCURL AND BUILD_SHARED_LIBS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(grk_fetch_header_leak_test GrkFetchHeaderLeakTest.cpp)
  target_link_libraries(grk_fetch_header_leak_test ${GROK_CORE_NAME} spdlog::spdlog)
  add_test(NAME grk_fetch_header_leak_test COMMAND grk_fetch_header_leak_test)

  add_executable(grk_fetch_shutdown_leak_test GrkFetchShutdownLeakTest.cpp)
  target_link_libraries(grk_fetch_shutdown_leak_test ${GROK_CORE_NAME} spdlog::spdlog)
  add_test(NAME grk_fetch_shutdown_leak_test COMMAND grk_fetch_shutdown_leak_test)

  # freezes the fetch worker one curl call short of prepareAuthHeaders, then
  # drops the codec, to catch the pure virtual dispatch during destruction
  add_executable(grk_fetch_pure_virtual_test GrkFetchPureVirtualTest.cpp)
  target_link_libraries(grk_fetch_pure_virtual_test ${GROK_CORE_NAME} spdlog::spdlog)
  add_test(NAME grk_fetch_pure_virtual_test COMMAND grk_fetch_pure_virtual_test)
  set_tests_properties(grk_fetch_pure_virtual_test PROPERTIES TIMEOUT 90)
endif()

# five windows per file, each decoded asynchronously and waited on swath by swath
if(GRK_DATA_ROOT)
  # single tile, tile grid on the image origin
  add_test(NAME j2k_multi_region_p0_01
    COMMAND j2k_multi_region_decompress ${GRK_DATA_ROOT}/input/conformance/p0_01.j2k)
  # 2x2 tiles
  add_test(NAME j2k_multi_region_p0_03
    COMMAND j2k_multi_region_decompress ${GRK_DATA_ROOT}/input/conformance/p0_03.j2k)
  # 8x8 tiles, so a window covers several tile rows
  add_test(NAME j2k_multi_region_p1_04
    COMMAND j2k_multi_region_decompress ${GRK_DATA_ROOT}/input/conformance/p1_04.j2k)
  # single tile whose grid starts left of and above the image origin
  add_test(NAME j2k_multi_region_p1_01
    COMMAND j2k_multi_region_decompress ${GRK_DATA_ROOT}/input/conformance/p1_01.j2k)
  # 5x3 tiles, grid origin left of and above the image origin: the grid has more
  # columns and rows than the image area alone accounts for
  add_test(NAME j2k_multi_region_b1_mono
    COMMAND j2k_multi_region_decompress ${GRK_DATA_ROOT}/input/conformance/b1_mono.j2c)
  # 5x5 tiles, image origin inside the first tile
  add_test(NAME j2k_multi_region_b2_mono
    COMMAND j2k_multi_region_decompress ${GRK_DATA_ROOT}/input/conformance/b2_mono.j2c)
endif()

add_executable(grk_subsampled_strip_geometry_test GrkSubsampledStripGeometryTest.cpp)
target_link_libraries(grk_subsampled_strip_geometry_test ${GROK_CODEC_NAME})
if(GRK_DATA_ROOT)
  # 3906x2602 4:2:0, where the strip height lands on an odd number of rows
  add_test(NAME grk_subsampled_strip_geometry_zoo1_test
    COMMAND grk_subsampled_strip_geometry_test
            ${GRK_DATA_ROOT}/input/conformance/zoo1.jp2)
endif()

add_executable(grk_incremental_band_write_test GrkIncrementalBandWriteTest.cpp)
target_link_libraries(grk_incremental_band_write_test ${GROK_CODEC_NAME})
if(GRK_DATA_ROOT)
  # tile 0 carries a second tile part after every other tile, so its row finishes last
  add_test(NAME grk_incremental_band_write_p0_07_test
    COMMAND grk_incremental_band_write_test
            ${GRK_DATA_ROOT}/input/conformance/p0_07.j2k
            0,0,2048,2048)
  set_tests_properties(grk_incremental_band_write_p0_07_test PROPERTIES TIMEOUT 300)
endif()

add_executable(grk_async_swath_decompress_test GrkAsyncSwathDecompressTest.cpp)
target_link_libraries(grk_async_swath_decompress_test ${GROK_CORE_NAME})
if(GRK_DATA_ROOT)
  # tile 0 carries a second tile part after every other tile, so its row finishes last
  add_test(NAME grk_async_swath_decompress_p0_07_test
    COMMAND grk_async_swath_decompress_test
            ${GRK_DATA_ROOT}/input/conformance/p0_07.j2k)
  set_tests_properties(grk_async_swath_decompress_p0_07_test PROPERTIES TIMEOUT 300)
endif()
