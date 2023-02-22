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
#include <string>
#include <cstring>
#include <vector>
#include <filesystem>

#include "grok.h"
#include "grok_codec.h"
#include "grk_examples_config.h"

const std::string dataRoot = GRK_DATA_ROOT;

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
    const uint32_t dimX = 640;
    const uint32_t dimY = 480;
    const uint32_t numComps = 3;
    const uint32_t precision = 8;
    grk_image_comp* compParams = nullptr;
    grk_image *image = nullptr;

    uint64_t compressedLength = 0;

    bool inputFromImage = false;

    std::vector<std::string> argString;
    std::vector<char *> args;

    std::string inputFile, outputFile;
    int rc = 1;

    if (inputFromImage) {
        // create blank image
        compParams = new grk_image_comp[numComps];
        for (uint32_t i = 0; i < numComps; ++i){
            auto c = compParams + i;
            c->w = dimX;
            c->h = dimY;
            c->dx = 1;
            c->dy = 1;
            c->prec = precision;
            c->sgnd = false;
        }
        image = grk_image_new(numComps, compParams, GRK_CLRSPC_SRGB, true);

        // fill in component data
        // see grok.h header for full details of image structure
        for (uint16_t compno = 0; compno < image->numcomps; ++compno){
            auto comp = image->comps + compno;
            auto compWidth = comp->w;
            auto compHeight = comp->h;
            auto compData = comp->data;
            if (!compData){
                fprintf(stderr, "Image has null data for component %d\n",compno);
                goto beach;
            }
            // fill in component data, taking component stride into account
            auto srcData = new int32_t[compWidth * compHeight];
            auto srcPtr = srcData;
            for (uint32_t j = 0; j < compHeight; ++j) {
               memcpy(compData, srcPtr, compWidth * sizeof(int32_t));
               srcPtr += compWidth;
               compData += comp->stride;
            }
            delete[] srcData;
        }


    }

	//1. form vector of command line args

	// first entry must always be the name of the program, as is
	// required by argv/argc variables in main method
	argString.push_back("codec_compress");

	// verbose output
	argString.push_back("-v");

   // a file can be passed in as a command line argument
    // example:
    // $ codec_compress foo.tif
    // otherwise a file from the Grok test suite, specified below, will be used.

	inputFile = dataRoot + std::filesystem::path::preferred_separator +
			"input" +  std::filesystem::path::preferred_separator +
			"nonregression" + std::filesystem::path::preferred_separator + "basn6a08.tif";
	outputFile = "basn6a08.jp2";
    if (argc > 1) {
        inputFile = argv[1];
        outputFile = inputFile + ".tif";
    }
	argString.push_back("-i " + inputFile);

	// output file
	argString.push_back("-o " + outputFile);

	// 2. convert to array of C strings
	for (auto& s : argString){
	  char *arg = new char[s.size() + 1];
	  copy(s.begin(), s.end(), arg);
	  arg[s.size()] = '\0';
	  args.push_back(arg);
	}

	// 3. decompress
	rc =  grk_codec_compress((int)args.size(),&args[0], image);
	if (rc)
	    fprintf(stderr, "Failed to compress\n");

beach:
	//4. cleanup
	for (auto& s : args)
	  delete[] s;
    delete[] compParams;
    grk_object_unref(&image->obj);
    grk_deinitialize();

	return rc;
}
