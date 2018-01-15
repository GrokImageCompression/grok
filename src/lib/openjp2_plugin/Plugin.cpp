/*
*    Copyright (C) 2016-2018 Grok Image Compression Inc.
*
*    This source code is free software: you can redistribute it and/or  modify
*    it under the terms of the GNU Affero General Public License, version 3,
*    as published by the Free Software Foundation.
*
*    This source code is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*/

#include "plugin_interface.h"
#include "Plugin.h"

static const char* PluginId = "SamplePlugin";

extern "C"  PLUGIN_API int32_t exit_func()
{
	return 0;
}

extern "C"  PLUGIN_API  void * create(grk::minpf_object_params *params) {
	return 0;
}

extern "C"  PLUGIN_API  int32_t destroy(void * object) {
	return 0;
}

extern "C" PLUGIN_API grk::minpf_exit_func minpf_post_load_plugin(const char* pluginPath, const grk::minpf_platform_services * params)
{
	int res = 0;

	grk::minpf_register_params rp;
	rp.version.major = 1;
	rp.version.minor = 0;

	rp.createFunc = create;
	rp.destroyFunc = destroy;

	res = params->registerObject(PluginId, &rp);
	if (res < 0)
		return nullptr;

	// custom plugin initialization can happen here
	//printf("Loaded plugin has been registered\n");
	return exit_func;
}

///////////////////////////////////
// Initialization
//////////////////////////////////

extern "C"  PLUGIN_API bool plugin_init(grok_plugin_init_info_t initInfo) {
	return false;
}

////////////////////////////////////
// Encoder Interface Implementation
////////////////////////////////////

extern "C"  PLUGIN_API int32_t plugin_encode(opj_cparameters_t* encode_parameters,
											grk::PLUGIN_ENCODE_USER_CALLBACK userCallback) {
	grk::plugin_encode_user_callback_info_t dummy;
	dummy.error_code = 0;
	return -1;
}

extern "C"  PLUGIN_API int32_t plugin_batch_encode(const char* input_dir,
													const char* output_dir,
													opj_cparameters_t* encode_parameters,
													grk::PLUGIN_ENCODE_USER_CALLBACK userCallback) {
	return -1;
}

extern "C"  PLUGIN_API bool plugin_is_batch_complete(void) {
	return true;
}



extern "C"  PLUGIN_API void plugin_stop_batch_encode(void) {
	
}

////////////////////////////////////
// Decoder Interface Implementation
////////////////////////////////////

extern "C"  PLUGIN_API int32_t plugin_decode(opj_decompress_parameters* decode_parameters,
											grk::PLUGIN_DECODE_USER_CALLBACK userCallback) {
	return -1;
}

extern "C"  PLUGIN_API int32_t plugin_init_batch_decode(const char* input_dir,
													const char* output_dir,
													opj_decompress_parameters* decode_parameters,
													grk::PLUGIN_DECODE_USER_CALLBACK userCallback) {

	return -1;
}

extern "C"  PLUGIN_API int32_t plugin_batch_decode(void) {
	return -1;
}
extern "C"  PLUGIN_API void plugin_stop_batch_decode(void) {
}



//////////////////////////////////
// Debug Interface
/////////////////////////////////

extern "C"  PLUGIN_API uint32_t plugin_get_debug_state(void) {
	return GROK_PLUGIN_STATE_NO_DEBUG;
}

extern "C"  PLUGIN_API void plugin_debug_next_cxd(grk::plugin_debug_mqc_t *mqc, uint32_t d) {

}

extern "C"  PLUGIN_API void plugin_debug_mqc_next_plane(grk::plugin_debug_mqc_t *mqc) {

}




