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
