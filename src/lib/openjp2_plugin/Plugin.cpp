#include "plugin_interface.h"

static const char* PluginId = "SamplePlugin";

extern "C"  PLUGIN_API int32_t exit_func()
{
	return 0;
}

extern "C"  PLUGIN_API  void * create(minpf_object_params *params) {
	return 0;
}

extern "C"  PLUGIN_API  int32_t destroy(void * object) {
	return 0;
}

extern "C" PLUGIN_API minpf_exit_func minpf_init_plugin(const minpf_platform_services * params)
{
	int res = 0;

	minpf_register_params rp;
	rp.version.major = 1;
	rp.version.minor = 0;

	rp.createFunc = create;
	rp.destroyFunc = destroy;

	res = params->registerObject(PluginId, &rp);
	if (res < 0)
		return 0;

	// custom plugin initialization can happen here
	return exit_func;
}
