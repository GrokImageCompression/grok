add_subdirectory(core)
if (GRK_BUILD_PLUGIN_LOADER)
  add_subdirectory(plugin)
endif(GRK_BUILD_PLUGIN_LOADER)
