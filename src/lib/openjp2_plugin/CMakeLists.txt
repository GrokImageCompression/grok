include_regular_expression("^.*$")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


include_directories(
  ${OPENJPEG_BINARY_DIR}/src/lib/openjp2_plugin
  ${OPENJPEG_BINARY_DIR}/src/lib/openjp2 # opj_config.h and opj_config_private.h
  ${OPENJPEG_SOURCE_DIR}/src/bin/jp2
  ${OPENJPEG_SOURCE_DIR}/src/lib/openjp2
)


# Defines the source code for the library
set(OPENJPEG_PLUGIN_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/Plugin.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/Plugin.h
)

# Build the library
if(WIN32)
    add_definitions(-DOPJ_EXPORTS)
endif()
add_library(${OPENJPEG_PLUGIN_NAME} ${OPENJPEG_PLUGIN_SRCS})
if(UNIX)
  target_link_libraries(${OPENJPEG_PLUGIN_NAME}  m)
endif()

set_target_properties(${OPENJPEG_PLUGIN_NAME} PROPERTIES ${OPENJPEG_LIBRARY_PROPERTIES})
target_link_libraries(${OPENJPEG_PLUGIN_NAME} ${CMAKE_DL_LIBS} )

if(OPJ_USE_DSYMUTIL)
    add_custom_command(TARGET ${OPENJPEG_PLUGIN_NAME} POST_BUILD 
    COMMAND "dsymutil" "$<TARGET_SONAME_FILE:${OPENJPEG_PLUGIN_NAME}>"
    COMMENT "dsymutil $<TARGET_SONAME_FILE:${OPENJPEG_PLUGIN_NAME}>"
    DEPENDS ${OPENJPEG_PLUGIN_NAME})
endif()
