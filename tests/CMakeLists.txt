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

add_executable(compare_images compare_images.cpp)
target_link_libraries(compare_images ${GROK_CODEC_NAME})

add_executable(compare_dump_files compare_dump_files.cpp GrkCompareDumpFiles.cpp )
target_link_libraries(compare_dump_files ${GROK_CORE_NAME} spdlog::spdlog)

add_executable(j2k_random_tile_access j2k_random_tile_access.cpp GrkRandomTileAccess.cpp)
target_link_libraries(j2k_random_tile_access ${GROK_CORE_NAME} spdlog::spdlog)

add_executable(j2k_multi_region_decompress j2k_multi_region_decompress.cpp GrkMultiRegionDecompress.cpp)
target_link_libraries(j2k_multi_region_decompress ${GROK_CORE_NAME} spdlog::spdlog)

add_executable(grk_lru_cache_test grk_lru_cache_test.cpp GrkLRUCacheTest.cpp)
target_include_directories(grk_lru_cache_test PRIVATE
  ${GROK_SOURCE_DIR}/src/lib/core/cache
  ${CMAKE_BINARY_DIR}/src/lib/core
)
target_link_libraries(grk_lru_cache_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_lru_cache_test COMMAND grk_lru_cache_test)

add_executable(compare_raw_files compare_raw_files.cpp GrkCompareRawFiles.cpp)
target_link_libraries(compare_raw_files ${GROK_CORE_NAME} spdlog::spdlog)

add_executable(grk_jp2_metadata_test grk_jp2_metadata_test.cpp GrkJP2MetadataTest.cpp)
target_link_libraries(grk_jp2_metadata_test ${GROK_CORE_NAME} spdlog::spdlog)
add_test(NAME grk_jp2_metadata_test COMMAND grk_jp2_metadata_test)

if(NOT GROK_HAVE_LIBPNG)
  message(WARNING "libpng is not available - running regression tests requires GRK_BUILD_LIBPNG enabled.")
endif()

add_subdirectory(conformance)
add_subdirectory(nonregression)
