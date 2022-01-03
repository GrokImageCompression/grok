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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */
#include "grk_config.h"
#include <filesystem>
#include "common.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <strings.h>
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#endif /* _WIN32 */

#define TCLAP_NAMESTARTSTRING "-"
#include "tclap/CmdLine.h"
#include "convert.h"
#include "grk_string.h"
#include <string>

typedef struct _dircnt
{
	/** Buffer for holding images read from Directory*/
	char* filename_buf;
	/** Pointer to the buffer*/
	char** filename;
} dircnt;

typedef struct _img_folder
{
	/** The directory path of the folder containing input images*/
	char* imgdirpath;
	/** Output format*/
	const char* out_format;
	/** Enable option*/
	bool set_imgdir;
	/** Enable Cod Format for output*/
	bool set_out_format;

	uint32_t flag;
} inputFolder;

static int loadImages(dircnt* dirptr, char* imgdirpath);
static char nextFile(size_t imageno, dircnt* dirptr, inputFolder* inputFolder,
					 grk_decompress_core_params* parameters);
static int parseCommandLine(int argc, char** argv, grk_decompress_core_params* parameters,
							inputFolder* inputFolder);

/* -------------------------------------------------------------------------- */
static void decompress_help_display(void)
{
	fprintf(stdout,
			"\nThis is the grk_dump utility from the Grok project.\n"
			"It dumps JPEG 2000 code stream info to stdout or a given file.\n"
			"It has been compiled against Grok library v%s.\n\n",
			grk_version());

	fprintf(stdout, "Parameters:\n");
	fprintf(stdout, "-----------\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "  -ImgDir <directory>\n");
	fprintf(stdout, "	Image file Directory path \n");
	fprintf(stdout, "  -i <compressed file>\n");
	fprintf(stdout, "    REQUIRED only if an Input image directory not specified\n");
	fprintf(stdout, "    Currently accepts J2K-files and JP2-files. The file type\n");
	fprintf(stdout, "    is identified based on its suffix.\n");
	fprintf(stdout, "  -o <output file>\n");
	fprintf(stdout, "    OPTIONAL\n");
	fprintf(stdout, "    Output file where file info will be dump.\n");
	fprintf(stdout, "    By default it will be in the stdout.\n");
	fprintf(stdout, "  -v ");
	fprintf(stdout, "    OPTIONAL\n");
	fprintf(stdout, "    Enable informative messages\n");
	fprintf(stdout, "    By default verbose mode is off.\n");
	fprintf(stdout, "\n");
}

class GrokOutput : public TCLAP::StdOutput
{
  public:
	virtual void usage(TCLAP::CmdLineInterface& c)
	{
		(void)c;
		decompress_help_display();
	}
};

/* -------------------------------------------------------------------------- */
static int loadImages(dircnt* dirptr, char* imgdirpath)
{
	int i = 0;

	for (const auto & entry : std::filesystem::directory_iterator(imgdirpath))
	{
		strcpy(dirptr->filename[i], entry.path().filename().string().c_str());
		i++;
	}

	return 0;
}
/* -------------------------------------------------------------------------- */
static char nextFile(size_t imageno, dircnt* dirptr, inputFolder* inputFolder,
					 grk_decompress_core_params* parameters)
{
	char inputFile[GRK_PATH_LEN], infilename[3 * GRK_PATH_LEN], temp_ofname[GRK_PATH_LEN];
	char *temp_p, temp1[GRK_PATH_LEN] = "";

	strcpy(inputFile, dirptr->filename[imageno]);
	spdlog::info("File Number {} \"{}\"", imageno, inputFile);
	if(!grk::jpeg2000_file_format(inputFile, &parameters->decod_format))
		return 1;
	sprintf(infilename, "%s/%s", inputFolder->imgdirpath, inputFile);
	if(grk::strcpy_s(parameters->infile, sizeof(parameters->infile), infilename) != 0)
	{
		return 1;
	}

	/*Set output file*/
	strcpy(temp_ofname, strtok(inputFile, "."));
	while((temp_p = strtok(nullptr, ".")) != nullptr)
	{
		strcat(temp_ofname, temp1);
		sprintf(temp1, ".%s", temp_p);
	}
	if(inputFolder->set_out_format)
	{
		char outfilename[3 * GRK_PATH_LEN];
		sprintf(outfilename, "%s/%s.%s", inputFolder->imgdirpath, temp_ofname,
				inputFolder->out_format);
		if(grk::strcpy_s(parameters->outfile, sizeof(parameters->outfile), outfilename) != 0)
		{
			return 1;
		}
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/**
 * Parse the command line
 */
/* -------------------------------------------------------------------------- */
static int parseCommandLine(int argc, char** argv, grk_decompress_core_params* parameters,
							inputFolder* inputFolder)
{
	try
	{
		TCLAP::CmdLine cmd("grk_dump command line", ' ', grk_version());

		// set the output
		GrokOutput output;
		cmd.setOutput(&output);

		TCLAP::ValueArg<std::string> inputArg("i", "input", "input file", false, "", "string", cmd);

		TCLAP::ValueArg<std::string> outputArg("o", "output", "output file", false, "", "string",
											   cmd);

		TCLAP::ValueArg<std::string> imgDirArg("y", "ImgDir", "image directory", false, "",
											   "string", cmd);

		TCLAP::SwitchArg verboseArg("v", "verbose", "verbose", cmd);
		TCLAP::ValueArg<uint32_t> flagArg("f", "flag", "flag", false, 0, "unsigned integer", cmd);

		cmd.parse(argc, argv);

		if(inputArg.isSet())
		{
			const char* infile = inputArg.getValue().c_str();
			if(!grk::jpeg2000_file_format(infile, &parameters->decod_format))
			{
				spdlog::error("Unknown input file format: {} \n"
							  "        Known file formats are *.j2k, *.jp2 or *.jpc",
							  infile);
				return 1;
			}
			if(grk::strcpy_s(parameters->infile, sizeof(parameters->infile), infile) != 0)
			{
				spdlog::error("Path is too long");
				return 1;
			}
		}

		if(outputArg.isSet())
		{
			if(grk::strcpy_s(parameters->outfile, sizeof(parameters->outfile),
							 outputArg.getValue().c_str()) != 0)
			{
				spdlog::error("Path is too long");
				return 1;
			}
		}

		if(imgDirArg.isSet())
		{
			inputFolder->imgdirpath = (char*)malloc(imgDirArg.getValue().length() + 1);
			if(!inputFolder->imgdirpath)
				return 1;
			strcpy(inputFolder->imgdirpath, imgDirArg.getValue().c_str());
			inputFolder->set_imgdir = true;
		}
		if(flagArg.isSet())
			inputFolder->flag = flagArg.getValue();
	}
	catch(TCLAP::ArgException& e) // catch any exceptions
	{
		std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
		return 1;
	}

	/* check for possible errors */
	if(inputFolder->set_imgdir)
	{
		if(!(parameters->infile[0] == 0))
		{
			spdlog::error("options -ImgDir and -i cannot be used together.");
			return 1;
		}
		if(!inputFolder->set_out_format)
		{
			spdlog::error("When -ImgDir is used, -OutFor <FORMAT> must be used.");
			spdlog::error("Only one format allowed.\n"
						  "Valid format are PGM, PPM, PNM, PGX, BMP, TIF and RAW.");
			return 1;
		}
		if(!(parameters->outfile[0] == 0))
		{
			spdlog::error("options -ImgDir and -o cannot be used together");
			return 1;
		}
	}
	else
	{
		if(parameters->infile[0] == 0)
		{
			spdlog::error("Required parameter is missing");
			spdlog::error("Example: {} -i image.j2k", argv[0]);
			spdlog::error("Help: {} -h", argv[0]);
			return 1;
		}
	}

	return 0;
}

/* -------------------------------------------------------------------------- */

/**
 sample error debug callback expecting no client object
 */
static void errorCallback(const char* msg, void* client_data)
{
	(void)client_data;
	spdlog::error(msg);
}
/**
 sample warning debug callback expecting no client object
 */
static void warningCallback(const char* msg, void* client_data)
{
	(void)client_data;
	spdlog::warn(msg);
}
/**
 sample debug callback expecting no client object
 */
static void infoCallback(const char* msg, void* client_data)
{
	(void)client_data;
	spdlog::info(msg);
}

/* -------------------------------------------------------------------------- */
/**
 * GRK_DUMP MAIN
 */
/* -------------------------------------------------------------------------- */
int main(int argc, char* argv[])
{
	FILE* fout = nullptr;

	grk_decompress_core_params parameters; /* Decompression parameters */
	grk_image* image = nullptr; /* Image structure */
	grk_codec* codec = nullptr; /* Handle to a decompressor */
	grk_stream* stream = nullptr; /* Stream */

	size_t num_images, imageno;
	inputFolder inputFolder;
	dircnt* dirptr = nullptr;
	int rc = EXIT_SUCCESS;

	grk_initialize(nullptr, 0);

	grk_set_info_handler(infoCallback, nullptr);
	grk_set_warning_handler(warningCallback, nullptr);
	grk_set_error_handler(errorCallback, nullptr);

	/* Set decoding parameters to default values */
	grk_decompress_set_default_params(&parameters);

	/* Initialize inputFolder */
	memset(&inputFolder, 0, sizeof(inputFolder));
	inputFolder.flag = GRK_IMG_INFO | GRK_J2K_MH_INFO | GRK_J2K_MH_IND;

	/* Parse input and get user compressing parameters */
	if(parseCommandLine(argc, argv, &parameters, &inputFolder) == 1)
	{
		rc = EXIT_FAILURE;
		goto cleanup;
	}

	/* Initialize reading of directory */
	if(inputFolder.set_imgdir)
	{
		num_images = (size_t)grk::get_num_images(inputFolder.imgdirpath);
		if(num_images == 0)
		{
			spdlog::error("Folder is empty");
			rc = EXIT_FAILURE;
			goto cleanup;
		}

		dirptr = (dircnt*)malloc(sizeof(dircnt));
		if(dirptr)
		{
			dirptr->filename_buf = (char*)malloc(
				num_images * GRK_PATH_LEN * sizeof(char)); /* Stores at max 10 image file names*/
			if(!dirptr->filename_buf)
			{
				rc = EXIT_FAILURE;
				goto cleanup;
			}
			dirptr->filename = (char**)malloc(num_images * sizeof(char*));
			if(!dirptr->filename)
			{
				rc = EXIT_FAILURE;
				goto cleanup;
			}
			for(size_t i = 0; i < num_images; i++)
			{
				dirptr->filename[i] = dirptr->filename_buf + i * GRK_PATH_LEN;
			}
		}
		if(loadImages(dirptr, inputFolder.imgdirpath) == 1)
		{
			rc = EXIT_FAILURE;
			goto cleanup;
		}
	}
	else
	{
		num_images = 1;
	}

	/* Try to open for writing the output file if necessary */
	if(parameters.outfile[0] != 0)
	{
		fout = fopen(parameters.outfile, "w");
		if(!fout)
		{
			spdlog::error("failed to open {} for writing", parameters.outfile);
			rc = EXIT_FAILURE;
			goto cleanup;
		}
	}
	else
		fout = stdout;

	/* Read the header of each image one by one */
	for(imageno = 0; imageno < num_images; imageno++)
	{
		if(inputFolder.set_imgdir)
		{
			if(nextFile(imageno, dirptr, &inputFolder, &parameters))
			{
				continue;
			}
		}
		stream = grk_stream_create_file_stream(parameters.infile, 1024 * 1024, 1);
		if(!stream)
		{
			spdlog::error("failed to create a stream from file {}", parameters.infile);
			rc = EXIT_FAILURE;
			goto cleanup;
		}
		switch(parameters.decod_format)
		{
			case GRK_J2K_FMT: {
				codec = grk_decompress_create(GRK_CODEC_J2K, stream);
				break;
			}
			case GRK_JP2_FMT: {
				codec = grk_decompress_create(GRK_CODEC_JP2, stream);
				break;
			}
			default:
				grk_object_unref(stream);
				stream = nullptr;
				continue;
		}

		/* Setup the decompressor decoding parameters using user parameters */
		if(!grk_decompress_init(codec, &parameters))
		{
			spdlog::error("grk_dump: failed to set up the decompressor");
			rc = EXIT_FAILURE;
			goto cleanup;
		}

		/* Read the main header of the code stream and if necessary the JP2 boxes*/
		if(!grk_decompress_read_header(codec, nullptr))
		{
			spdlog::error("grk_dump: failed to read the header");
			rc = EXIT_FAILURE;
			goto cleanup;
		}

		grk_dump_codec(codec, inputFolder.flag, fout);
		/* close the byte stream */
		if(stream)
		{
			grk_object_unref(stream);
			stream = nullptr;
		}

		/* free remaining structures */
		if(codec)
		{
			grk_object_unref(codec);
			codec = nullptr;
		}

		/* destroy the image header */
		if(image)
		{
			image = nullptr;
		}
	}
cleanup:
	if(dirptr)
	{
		free(dirptr->filename_buf);
		free(dirptr->filename);
		free(dirptr);
	}
	/* close the byte stream */
	grk_object_unref(stream);
	/* free remaining structures */
	grk_object_unref(codec);
	if(fout)
		fclose(fout);
	grk_deinitialize();

	return rc;
}
