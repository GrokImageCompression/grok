include_regular_expression("^.*$")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

IF(MSVC)
    SET(CMAKE_CXX_FLAGS "/EHsc")
ENDIF(MSVC)

include_directories(
  ${GROK_BINARY_DIR}/src/lib/openjp2_plugin
  ${GROK_BINARY_DIR}/src/lib/openjp2 # opj_config.h and opj_config_private.h
  ${GROK_SOURCE_DIR}/src/bin/jp2
  ${GROK_SOURCE_DIR}/src/lib/openjp2
)


# Defines the source code for the library
set(GROK_PLUGIN_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/Plugin.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/Plugin.h
)

# Build the library
if(WIN32)
    add_definitions(-DOPJ_EXPORTS)
endif()
add_library(${GROK_PLUGIN_NAME} ${GROK_PLUGIN_SRCS})

set_target_properties(${GROK_PLUGIN_NAME} PROPERTIES ${GROK_LIBRARY_PROPERTIES})
target_link_libraries(${GROK_PLUGIN_NAME} ${CMAKE_DL_LIBS} )

if(GROK_USE_DSYMUTIL)
    add_custom_command(TARGET ${GROK_PLUGIN_NAME} POST_BUILD 
    COMMAND "dsymutil" "$<TARGET_SONAME_FILE:${GROK_PLUGIN_NAME}>"
    COMMENT "dsymutil $<TARGET_SONAME_FILE:${GROK_PLUGIN_NAME}>"
    DEPENDS ${GROK_PLUGIN_NAME})
endif()
