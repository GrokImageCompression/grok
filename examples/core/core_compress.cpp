/*
 *    Copyright (C) 2016-2023 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <cstdio>
#include <string>
#include <cstring>

#include "grok.h"

void errorCallback(const char* msg, [[maybe_unused]] void* client_data)
{
	auto t = std::string(msg) + "\n";
	fprintf(stderr,t.c_str());
}
void warningCallback(const char* msg, [[maybe_unused]] void* client_data)
{
	auto t = std::string(msg) + "\n";
	fprintf(stdout,t.c_str());
}
void infoCallback(const char* msg, [[maybe_unused]] void* client_data)
{
	auto t = std::string(msg) + "\n";
	fprintf(stdout,t.c_str());
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
    // initialize compress parameters
	grk_cparameters param;
    grk_compress_set_default_params(&param);
    param.cod_format = GRK_FMT_JP2;
    param.verbose = true;

	grk_codec *codec = nullptr;
	grk_image *image = nullptr;
	grk_image_comp* compParams = nullptr;
	int32_t rc = EXIT_FAILURE;

	// initialize library
	grk_initialize(nullptr, 0);

	grk_stream_params stream_params;
	memset(&stream_params,0,sizeof(stream_params));
	stream_params.file = "test.jp2";

	// set library message handlers
	grk_set_msg_handlers(infoCallback, nullptr, warningCallback, nullptr,
						 errorCallback, nullptr);

	// create image
	compParams = new grk_image_comp[3];
	for (uint32_t i = 0; i < 3; ++i){
	    auto c = compParams + i;
	    c->w = 640;
	    c->h = 480;
	    c->dx = 1;
	    c->dy = 1;
	    c->prec = 8;
	    c->sgnd = false;
	}
	image = grk_image_new(3, compParams, GRK_CLRSPC_SRGB, true);

	// initialize compressor
	codec = grk_compress_init(&stream_params, &param, image);
	if(!codec)
	{
		fprintf(stderr, "Failed to initialize compressor\n");
		goto beach;
	}

	// compress
    if (!grk_compress(codec, nullptr))
    {
        fprintf(stderr, "Failed to compress\n");
        goto beach;
    }

	rc = EXIT_SUCCESS;
beach:
    // cleanup
    delete[] compParams;
	grk_object_unref(codec);
	grk_object_unref(&image->obj);
    grk_deinitialize();

	return rc;
}
