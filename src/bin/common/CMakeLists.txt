#-----------------------------------------------------------------------------
# grk_apps_config.h generation
configure_file(
 ${CMAKE_CURRENT_SOURCE_DIR}/grk_apps_config.h.cmake.in
 ${CMAKE_CURRENT_BINARY_DIR}/grk_apps_config.h
 @ONLY
 )
