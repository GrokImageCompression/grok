include_directories(
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/../core # grk_config.h and grk_config_private.h
  ${GROK_SOURCE_DIR}/src/bin/codec
  ${GROK_SOURCE_DIR}/src/lib/core
)

set(GROK_PLUGIN_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/Plugin.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/Plugin.h
)

add_library(${GROK_PLUGIN_NAME} ${GROK_PLUGIN_SRCS})

set_target_properties(${GROK_PLUGIN_NAME} PROPERTIES ${GROK_LIBRARY_PROPERTIES})
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  target_link_options(${GROK_PLUGIN_NAME} PRIVATE "LINKER:-z,now")
endif()

if (GRK_BUILD_PLUGIN_LOADER)
  target_link_libraries(${GROK_PLUGIN_NAME} ${CMAKE_DL_LIBS} )
endif(GRK_BUILD_PLUGIN_LOADER)
