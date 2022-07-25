set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

IF(MSVC)
    string(APPEND CMAKE_CXX_FLAGS " /EHsc")
ENDIF(MSVC)

include_directories(
  ${GROK_BINARY_DIR}/src/lib/jp2 # grk_config.h
  ${GROK_BINARY_DIR}/src/bin/ # grk_apps_config.h
  ${GROK_BINARY_DIR}/src/lib/codec/ # grk_apps_config.h
  ${GROK_SOURCE_DIR}/src/lib/codec
  ${GROK_SOURCE_DIR}/src/lib/jp2
  ${GROK_SOURCE_DIR}/src/include
  ${GROK_SOURCE_DIR}/src/bin/image_format
  ${LCMS_INCLUDE_DIRNAME}
  ${PNG_INCLUDE_DIRNAME}
  ${TIFF_INCLUDE_DIRNAME}
  &{JPEG_INCLUDE_DIRNAME}
  )

 if(WIN32)
  add_definitions(-DNOMINMAX)
  if(BUILD_SHARED_LIBS)
    add_definitions(-DGRK_EXPORTS)
  else()
    add_definitions(-DGRK_STATIC)
  endif()
endif()

add_executable(compare_images compare_images.cpp)
target_link_libraries(compare_images
  ${GROK_LIBRARY_NAME} ${GROK_CODEC_NAME}
  ${PNG_LIBNAME} ${TIFF_LIBNAME} ${LCMS_LIBNAME} ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT}
  )

if(ZLIB_FOUND AND APPLE)
  target_link_libraries(compare_images z)
else(ZLIB_FOUND AND APPLE)
  target_link_libraries(compare_images ${Z_LIBNAME})
endif()
if(URING)
  target_link_libraries(compare_images uring)
endif()

add_executable(compare_dump_files compare_dump_files.cpp)
target_link_libraries(compare_dump_files ${GROK_LIBRARY_NAME} ${GROK_CODEC_NAME} ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT} )
add_executable(j2k_random_tile_access j2k_random_tile_access.cpp)
target_link_libraries(j2k_random_tile_access ${GROK_LIBRARY_NAME} ${GROK_CODEC_NAME} ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT} )
add_executable(compare_raw_files compare_raw_files.cpp)
target_link_libraries(compare_raw_files ${GROK_LIBRARY_NAME} ${GROK_CODEC_NAME} ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT} )
add_executable(test_tile_encoder test_tile_encoder.cpp)
target_link_libraries(test_tile_encoder ${GROK_LIBRARY_NAME} ${GROK_CODEC_NAME} ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})

# Let's try a couple of possibilities:
add_test(NAME tte0 COMMAND test_tile_encoder)
add_test(NAME tte1 COMMAND test_tile_encoder 3 2048 2048 1024 1024 8 1 tte1.j2k)
add_test(NAME tte2 COMMAND test_tile_encoder 3 2048 2048 1024 1024 8 1 tte2.jp2)
add_test(NAME tte3 COMMAND test_tile_encoder 1 2048 2048 1024 1024 8 1 tte3.j2k)
add_test(NAME tte4 COMMAND test_tile_encoder 1  256  256  128  128 8 0 tte4.j2k)
add_test(NAME tte5 COMMAND test_tile_encoder 1  512  512  256  256 8 0 tte5.j2k)
#add_test(NAME tte6 COMMAND test_tile_encoder 1 8192 8192  512  512 8 0 tte6.j2k)
#add_test(NAME tte7 COMMAND test_tile_encoder 1 32768 32768 512  512 8 0 tte7.jp2)

add_executable(test_tile_decoder test_tile_decoder.cpp ${test_tile_decoder_SRCS})
target_link_libraries(test_tile_decoder ${GROK_LIBRARY_NAME}  ${GROK_CODEC_NAME} ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})

add_test(NAME ttd0 COMMAND test_tile_decoder)
set_property(TEST ttd0 APPEND PROPERTY DEPENDS tte0)
add_test(NAME ttd1 COMMAND test_tile_decoder 0 0 1024 1024 tte1.j2k)
set_property(TEST ttd1 APPEND PROPERTY DEPENDS tte1)
add_test(NAME ttd2 COMMAND test_tile_decoder 0 0 1024 1024 tte2.jp2)
set_property(TEST ttd2 APPEND PROPERTY DEPENDS tte2)
#add_test(NAME ttd6 COMMAND test_tile_decoder 0 0  512  512 tte6.j2k)
#set_property(TEST ttd6 APPEND PROPERTY DEPENDS tte6)
#add_test(NAME ttd7 COMMAND test_tile_decoder 0 0  512  512 tte7.jp2)
#set_property(TEST ttd7 APPEND PROPERTY DEPENDS tte7)

add_test(NAME rta1 COMMAND j2k_random_tile_access tte1.j2k)
set_property(TEST rta1 APPEND PROPERTY DEPENDS tte1)
add_test(NAME rta2 COMMAND j2k_random_tile_access tte2.jp2)
set_property(TEST rta2 APPEND PROPERTY DEPENDS tte2)
add_test(NAME rta3 COMMAND j2k_random_tile_access tte3.j2k)
set_property(TEST rta3 APPEND PROPERTY DEPENDS tte3)
add_test(NAME rta4 COMMAND j2k_random_tile_access tte4.j2k)
set_property(TEST rta4 APPEND PROPERTY DEPENDS tte4)
add_test(NAME rta5 COMMAND j2k_random_tile_access tte5.j2k)
set_property(TEST rta5 APPEND PROPERTY DEPENDS tte5)

if(NOT GROK_HAVE_LIBPNG)
  message(WARNING "libpng is not available - running regression tests requires GRK_BUILD_LIBPNG enabled.")
endif()

add_subdirectory(conformance)
add_subdirectory(nonregression)
#add_executable(pdf2jp2 pdf2jp2.cpp)
