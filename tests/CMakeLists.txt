include_directories(
  ${CMAKE_CURRENT_BINARY_DIR}/../src/lib/core # grk_config.h
  ${CMAKE_CURRENT_BINARY_DIR}/../src/bin/ # grk_apps_config.h
  ${CMAKE_CURRENT_BINARY_DIR}/../src/lib/codec/ # grk_apps_config.h
  ${GROK_SOURCE_DIR}/src/lib/codec
  ${GROK_SOURCE_DIR}/src/lib/core
  ${GROK_SOURCE_DIR}/src/include
)

add_executable(compare_images compare_images.cpp)
target_link_libraries(compare_images ${GROK_CODEC_NAME})

add_executable(compare_dump_files compare_dump_files.cpp GrkCompareDumpFiles.cpp )
target_link_libraries(compare_dump_files ${GROK_CORE_NAME})

add_executable(j2k_random_tile_access j2k_random_tile_access.cpp GrkRandomTileAccess.cpp)
target_link_libraries(j2k_random_tile_access ${GROK_CORE_NAME})

add_executable(compare_raw_files compare_raw_files.cpp GrkCompareRawFiles.cpp)
target_link_libraries(compare_raw_files ${GROK_CORE_NAME})

if(NOT GROK_HAVE_LIBPNG)
  message(WARNING "libpng is not available - running regression tests requires GRK_BUILD_LIBPNG enabled.")
endif()

add_subdirectory(conformance)
add_subdirectory(nonregression)
#add_executable(pdf2jp2 pdf2jp2.cpp)
