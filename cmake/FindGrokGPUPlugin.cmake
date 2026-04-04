#
# FindGrokGPUPlugin.cmake
#
# Finds the grok-gpu-plugin shared library (libgrok_gpu_plugin.so)
# built from the grok-gpu-plugin standalone project.
#
# Usage:
#   set(GROK_GPU_PLUGIN_DIR "/path/to/grok-gpu-plugin/build-shared")
#   find_package(GrokGPUPlugin)
#
# Provides:
#   GrokGPUPlugin_FOUND       - TRUE if the plugin library was found
#   GrokGPUPlugin_LIBRARY     - Path to the shared library
#   GrokGPUPlugin_INCLUDE_DIR - Path to plugin_api.h header directory
#

find_path(GrokGPUPlugin_INCLUDE_DIR
    NAMES grk_gpu_plugin/plugin_api.h
    HINTS
        ${GROK_GPU_PLUGIN_DIR}/../include
        ${CMAKE_SOURCE_DIR}/extern/grok-gpu-plugin/include
        /usr/local/include
        /usr/include
)

find_library(GrokGPUPlugin_LIBRARY
    NAMES grok_gpu_plugin
    HINTS
        ${GROK_GPU_PLUGIN_DIR}
        ${CMAKE_SOURCE_DIR}/extern/grok-gpu-plugin/build-shared
        /usr/local/lib
        /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GrokGPUPlugin
    DEFAULT_MSG
    GrokGPUPlugin_LIBRARY
    GrokGPUPlugin_INCLUDE_DIR
)

if(GrokGPUPlugin_FOUND)
    message(STATUS "Found grok-gpu-plugin: ${GrokGPUPlugin_LIBRARY}")
    mark_as_advanced(GrokGPUPlugin_LIBRARY GrokGPUPlugin_INCLUDE_DIR)
endif()
