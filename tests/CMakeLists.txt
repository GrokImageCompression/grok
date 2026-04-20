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

add_subdirectory(conformance)
add_subdirectory(nonregression)
