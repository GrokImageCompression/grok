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
#include <cassert>

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

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;
    int rc = EXIT_FAILURE;

    uint16_t numTiles = 0;

    // a file can be passed in as a command line argument
    // example:
    // $ core_decompress_from_file foo.jp2
    // otherwise a file from the Grok test suite, specified below, will be used.

	std::string inputFilePath = dataRoot + std::filesystem::path::preferred_separator +
			"input" +  std::filesystem::path::preferred_separator +
			"nonregression" + std::filesystem::path::preferred_separator + "boats_cprl.j2k";
	if (argc > 1)
	    inputFilePath = argv[1];

	// initialize decompress parameters
	grk_decompress_parameters param;
	memset(&param, 0, sizeof(grk_decompress_parameters));
	param.compressionLevel = GRK_DECOMPRESS_COMPRESSION_LEVEL_DEFAULT;
	param.verbose_ = true;
    grk_decompress_set_default_params(&param.core);

	grk_codec *codec = nullptr;
	grk_image *image = nullptr;

	// if true, decompress a particular tile, otherwise decompress
	// all tiles
	bool decompressTile = false;

    // index of tile to decompress.
	uint16_t tileIndex = 0;

	// if true, decompress window of dimension specified below,
	// otherwise decompress entire image
	bool decompressWindow = false;

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
	if(!grk_decompress_detect_format(inputFileStr, &param.decod_format))
	{
		fprintf(stderr, "Failed to parse input file format\n");
		goto beach;
	}

    printf("Decompressing file %s of format %s\n",
            inputFilePath.c_str(),param.decod_format == GRK_CODEC_J2K ? "j2k" : "jp2");
    codec = grk_decompress_create(stream);
	if (!codec){
        fprintf(stderr,"Failed to create codec.\n");
        goto beach;
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
    grk_header_info headerInfo;
    memset(&headerInfo,0,sizeof(headerInfo));
	if(!grk_decompress_read_header(codec, &headerInfo))
	{
		fprintf(stderr, "Failed to read the header\n");
		goto beach;
	}

	// set decompress window
	if (decompressWindow) {
	    // decompress window of dimensions {0,0,1000,1000}
        if(!grk_decompress_set_window(codec, 0, 0, 1000, 1000))
        {
            fprintf(stderr, "Failed to set decompress region\n");
            goto beach;
        }
	}

    // retrieve image that will store uncompressed image data
    image = grk_decompress_get_composited_image(codec);
    if (!image){
        fprintf(stderr, "Failed to retrieve image \n");
        goto beach;
    }

    numTiles = (uint16_t)(headerInfo.t_grid_width * headerInfo.t_grid_height);
    printf("\nImage Info\n");
    printf("Width: %d\n", image->x1 - image->x0);
    printf("Height: %d\n", image->y1 - image->y0);
    printf("Number of components: %d\n", image->numcomps);
    for (uint16_t compno = 0; compno < image->numcomps; ++compno)
        printf("Precision of component %d : %d\n", compno,image->comps[compno].prec);
    printf("Number of tiles: %d\n", numTiles);
    if (numTiles > 1) {
        printf("Nominal tile dimensions: (%d,%d)\n",headerInfo.t_width, headerInfo.t_height);
    }

	if (decompressTile) {
	    // decompress a particular tile
	    if(!grk_decompress_tile(codec, tileIndex))
	        goto beach;
	} else {
	    // decompress all tiles
        if(!grk_decompress(codec, nullptr))
            goto beach;
	}

    // see grok.h header for full details of image structure
    for (uint16_t compno = 0; compno < image->numcomps; ++compno){
        auto comp = image->comps + compno;
        auto compWidth = comp->w;
        (void)compWidth;
        auto compHeight = comp->h;
        (void)compHeight;
        auto compData = comp->data;
        if (!compData){
            fprintf(stderr, "Image has null data for component %d\n",compno);
            goto beach;
        }
    }

	rc = EXIT_SUCCESS;
beach:
    // cleanup
	grk_object_unref(stream);
	grk_object_unref(codec);
    grk_deinitialize();

	return rc;
}
