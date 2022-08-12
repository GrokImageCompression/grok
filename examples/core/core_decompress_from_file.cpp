/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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
#include <filesystem>

#include "grok.h"
#include "grk_examples_config.h"

const std::string dataRoot = GRK_DATA_ROOT;

void errorCallback(const char* msg, void* client_data)
{
	(void)(client_data);
	std::string t = std::string(msg) + "\n";
	fprintf(stderr,t.c_str());
}
void warningCallback(const char* msg, void* client_data)
{
	(void)(client_data);
	std::string t = std::string(msg) + "\n";
	fprintf(stdout,t.c_str());
}
void infoCallback(const char* msg, void* client_data)
{
	(void)(client_data);
	std::string t = std::string(msg) + "\n";
	fprintf(stdout,t.c_str());
}
// parse file format
#define JP2_RFC3745_MAGIC "\x00\x00\x00\x0c\x6a\x50\x20\x20\x0d\x0a\x87\x0a"
#define J2K_CODESTREAM_MAGIC "\xff\x4f\xff\x51"
static bool jpeg2000_file_format(const char* fname, GRK_SUPPORTED_FILE_FMT* fmt)
{
	GRK_SUPPORTED_FILE_FMT magic_format = GRK_UNK_FMT;
	uint8_t buf[12];
	auto reader = fopen(fname, "rb");
	if(reader == nullptr)
		return false;

	memset(buf, 0, 12);
	auto bytesRead = fread(buf, 1, 12, reader);
	if(fclose(reader))
		return false;
	if(bytesRead != 12)
		return false;

	if(memcmp(buf, JP2_RFC3745_MAGIC, 12) == 0)
	{
		magic_format = GRK_JP2_FMT;
	}
	else if(memcmp(buf, J2K_CODESTREAM_MAGIC, 4) == 0)
	{
		magic_format = GRK_J2K_FMT;
	}
	else
	{
		fprintf(stderr,"%s does not contain a JPEG 2000 code stream\n", fname);
		*fmt = GRK_UNK_FMT;

		return false;
	}

	*fmt = magic_format;

	return true;
}

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;
    int rc = EXIT_FAILURE;

	std::string inputFilePath = dataRoot + std::filesystem::path::preferred_separator +
			"input" +  std::filesystem::path::preferred_separator +
			"nonregression" + std::filesystem::path::preferred_separator + "boats_cprl.j2k";

	// initialize decompress parameters
	grk_decompress_parameters param;
	memset(&param, 0, sizeof(grk_decompress_parameters));
	param.repeats = 1;
	param.compressionLevel = GRK_DECOMPRESS_COMPRESSION_LEVEL_DEFAULT;
    grk_decompress_set_default_params(&param.core);

	grk_codec *codec = nullptr;
	grk_image *image = nullptr;

	// decompress tile 0
	uint16_t tile_index = 0;

	// set decode window to {0,0,1000,1000} (optional)
	float da_x0 = 0;
	float da_y0 = 0;
	float da_x1 = 1000;
	float da_y1 = 1000;

	// initialize library
	grk_initialize(nullptr, 0);

	// create j2k file stream
    auto inputFileStr = inputFilePath.c_str();
	auto stream = grk_stream_create_file_stream(inputFileStr, 1024 * 1024, true);
	if(!stream)
	{
		fprintf(stderr, "Failed to create a stream from file %s\n", inputFileStr);
		goto beach;
	}

	// parse jpeg 2000 format : j2k or jp2
	if(!jpeg2000_file_format(inputFileStr, &param.decod_format))
	{
		fprintf(stderr, "Failed to parse input file format\n");
		goto beach;
	}

	// create codec
	switch(param.decod_format)
	{
		case GRK_J2K_FMT: { /* JPEG-2000 codestream */
			codec = grk_decompress_create(GRK_CODEC_J2K, stream);
			break;
		}
		case GRK_JP2_FMT: { /* JPEG 2000 compressed image data */
			codec = grk_decompress_create(GRK_CODEC_JP2, stream);
			break;
		}
		default: {
			fprintf(stderr,"Not a valid JPEG2000 file\n");
			goto beach;
			break;
		}
	}

	// set message handlers for info,warning and error
	grk_set_msg_handlers(infoCallback, nullptr, warningCallback, nullptr,
						 errorCallback, nullptr);

	// initialize decompressor
	if(!grk_decompress_init(codec, &param.core))
	{
		fprintf(stderr, "Failed to set up decompressor\n");
		goto beach;
	}

	// read j2k header
	if(!grk_decompress_read_header(codec, nullptr))
	{
		fprintf(stderr, "Failed to read the header\n");
		goto beach;
	}
	// set decode window (optional)
	if(!grk_decompress_set_window(codec, da_x0, da_y0, da_x1, da_y1))
	{
		fprintf(stderr, "Failed to set decompress region\n");
		goto beach;
	}

	// decompress tile
	if(!grk_decompress_tile(codec, tile_index))
		goto beach;

    // retrieve image holding uncompressed image data
    image = grk_decompress_get_composited_image(codec);
    if (!image){
        fprintf(stderr, "Failed to retrieve image \n");
        goto beach;
    }
    // see grok.h header for full details of image structure
    /*
    for (uint16_t compno = 0; compno < image->numcomps; ++compno){
        auto comp = image->comps + compno;
        auto compWidth = comp->w;
        auto compHeight = comp->h;
        auto compData = comp->data;
    }
    */

	rc = EXIT_SUCCESS;
beach:
    // cleanup
	grk_object_unref(stream);
	grk_object_unref(codec);
    grk_deinitialize();

	return rc;
}
