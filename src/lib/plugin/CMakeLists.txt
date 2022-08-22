include_regular_expression("^.*$")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

IF(MSVC)
    string(APPEND CMAKE_CXX_FLAGS " /EHsc")
ENDIF(MSVC)

include_directories(
  ${GROK_BINARY_DIR}/src/lib/plugin
  ${GROK_BINARY_DIR}/src/lib/core # grk_config.h and grk_config_private.h
  ${GROK_SOURCE_DIR}/src/bin/codec
  ${GROK_SOURCE_DIR}/src/lib/core
)


# Defines the source code for the library
set(GROK_PLUGIN_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/Plugin.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/Plugin.h
)

# Build the library
if(WIN32)
    add_definitions(-DGRK_EXPORTS)
endif()

add_library(${GROK_PLUGIN_NAME} ${GROK_PLUGIN_SRCS})

set_target_properties(${GROK_PLUGIN_NAME} PROPERTIES ${GROK_LIBRARY_PROPERTIES})

if (GRK_BUILD_PLUGIN_LOADER)
  target_link_libraries(${GROK_PLUGIN_NAME} ${CMAKE_DL_LIBS} )
endif(GRK_BUILD_PLUGIN_LOADER)
