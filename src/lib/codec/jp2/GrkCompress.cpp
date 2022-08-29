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

#include <cfloat>
#include <cmath>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <sstream>
#ifndef _WIN32
#include <cstring>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif /* _WIN32 */
#include <chrono>

#include <filesystem>
#include "common.h"
#include "codec_common.h"
using namespace grk;
#include "grk_apps_config.h"
#include "grok.h"
#include "RAWFormat.h"
#include "PNMFormat.h"
#include "PGXFormat.h"
#include "BMPFormat.h"
#ifdef GROK_HAVE_LIBJPEG
#include "JPEGFormat.h"
#endif
#ifdef GROK_HAVE_LIBTIFF
#include "TIFFFormat.h"
#endif
#ifdef GROK_HAVE_LIBPNG
#include "PNGFormat.h"
#endif
#include "convert.h"
#include "grk_string.h"
#define TCLAP_NAMESTARTSTRING "-"
#include "tclap/CmdLine.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "exif.h"
#include "GrkCompress.h"

void exit_func()
{
	grk_plugin_stop_batch_compress();
}

#ifdef _WIN32
BOOL sig_handler(DWORD signum)
{
	switch(signum)
	{
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_CLOSE_EVENT:
		case CTRL_LOGOFF_EVENT:
		case CTRL_SHUTDOWN_EVENT:
			exit_func();
			return (TRUE);

		default:
			return FALSE;
	}
}
#else
void sig_handler(int signum)
{
	GRK_UNUSED(signum);
	exit_func();
}
#endif

void setUpSignalHandler()
{
#ifdef _WIN32
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)sig_handler, TRUE);
#else
	struct sigaction sa;
	sa.sa_handler = &sig_handler;
	sigfillset(&sa.sa_mask);
	sigaction(SIGHUP, &sa, nullptr);
#endif
}

namespace grk
{

static bool pluginCompressCallback(grk_plugin_compress_user_callback_info* info);

grk_img_fol img_fol_plugin, out_fol_plugin;

static void compress_help_display(void)
{
	fprintf(stdout,
			"grk_compress compresses various image formats into the JPEG 2000 format.\n"
			"It has been compiled against libgrokj2k v%s.\n\n",
			grk_version());

	fprintf(stdout, "-------------------------\n");
	fprintf(stdout, "Default compressing options:\n");
	fprintf(stdout, "-------------------------\n");
	fprintf(stdout, "\n");
	fprintf(stdout, " * Lossless\n");
	fprintf(stdout, " * 1 tile\n");
	fprintf(stdout, " * RGB->YCC conversion if there are 3 colour components\n");
	fprintf(stdout, " * Size of precinct : 2^15 x 2^15 (i.e. 1 precinct)\n");
	fprintf(stdout, " * Size of code-block : 64 x 64\n");
	fprintf(stdout, " * Number of resolutions: 6\n");
	fprintf(stdout, " * No SOP marker in the code stream\n");
	fprintf(stdout, " * No EPH marker in the code stream\n");
	fprintf(stdout, " * No sub-sampling in x or y direction\n");
	fprintf(stdout, " * No mode switch activated\n");
	fprintf(stdout, " * Progression order: LRCP\n");
	fprintf(stdout, " * No ROI upshifted\n");
	fprintf(stdout, " * No offset of the origin of the image\n");
	fprintf(stdout, " * No offset of the origin of the tiles\n");
	fprintf(stdout, " * Reversible DWT 5-3\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "-----------\n");
	fprintf(stdout, "Parameters:\n");
	fprintf(stdout, "-----------\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "Required Parameters (except with -h):\n");
	fprintf(stdout, "One of the two options [in_dir] or [in_file] must be used\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "[-i|-in_file] <file>\n");
	fprintf(stdout, "    Input file\n");
	fprintf(stdout, "    Supported extensions: <PBM|PGM|PPM|PNM|PAM|PGX|PNG|BMP|TIF|RAW|RAWL>\n");
	fprintf(stdout, "    If used, '-o <file>' must be provided\n");
	fprintf(stdout, "[-o|-out_file] <compressed file>\n");
	fprintf(stdout, "    Output file (supported extensions are j2k or jp2).\n");
	fprintf(stdout, "[-y|-in_dir] <dir>\n");
	fprintf(stdout, "    Uncompressed file directory\n");
	fprintf(stdout, "    When using this option [out_fmt] must be used\n");
	fprintf(stdout, "[-O|-out_fmt] <J2K|J2C|JP2>\n");
	fprintf(stdout, "    Output format for compressed files.\n");
	fprintf(stdout, "    Required only if [in_dir] is used\n");
	fprintf(stdout, "[-K|-in_fmt] <pbm|pgm|ppm|pnm|pam|pgx|png|bmp|tif|raw|rawl>\n");
	fprintf(stdout, "    Input format. Will override file tag.\n");
	fprintf(stdout,
			"[-F|-raw] <width>,<height>,<ncomp>,<bitdepth>,{s,u}@<dx1>x<dy1>:...:<dxn>x<dyn>\n");
	fprintf(stdout, "    Characteristics of the raw input image\n");
	fprintf(stdout, "    If subsampling is omitted, 1x1 is assumed for all components\n");
	fprintf(stdout, "      Example: -F 512,512,3,8,u@1x1:2x2:2x2\n");
	fprintf(stdout, "               for raw 512x512 image with 4:2:0 subsampling\n");
	fprintf(stdout, "    Required only if RAW or RAWL input file is provided.\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "Optional Parameters:\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "[-h|-help]\n");
	fprintf(stdout, "    Display the help information.\n");
	fprintf(stdout, "[-a|-out_dir] <output directory>\n");
	fprintf(stdout, "    Output directory where compressed files are stored.\n");
	fprintf(stdout, "[-r|-compression_ratios] <compression ratio>,<compression ratio>,...\n");
	fprintf(stdout, "    Different compression ratios for successive layers.\n");
	fprintf(stdout, "    The rate specified for each quality level is the desired\n");
	fprintf(stdout, "    compression factor.\n");
	fprintf(stdout, "    Decreasing ratios required.\n");
	fprintf(stdout, "      Example: -r 20,10,1 means \n");
	fprintf(stdout, "            quality layer 1: compress 20x, \n");
	fprintf(stdout, "            quality layer 2: compress 10x \n");
	fprintf(stdout, "            quality layer 3: compress lossless\n");
	fprintf(stdout, "    Not supported for Part 15 HTJ2K compression.\n");
	fprintf(stdout, "    Options -r and -q cannot be used together.\n");
	fprintf(stdout, "[-q|-quality] <psnr value>,<psnr value>,<psnr value>,...\n");
	fprintf(stdout, "    Specify PSNR for successive layers (-q 30,40,50).\n");
	fprintf(stdout, "    Increasing PSNR values required.\n");
	fprintf(stdout, "    Not supported for Part 15 HTJ2K compression.\n");
	fprintf(stdout, "    Note: options -r and -q cannot be used together.\n");
	fprintf(stdout, "[-A|-rate_control_algorithm] <0|1>\n");
	fprintf(stdout, "    Select algorithm used for rate control\n");
	fprintf(stdout, "    0: Bisection search for optimal threshold using all code passes in code "
					"blocks. (default) (slightly higher PSRN than algorithm 1)\n");
	fprintf(stdout, "    1: Bisection search for optimal threshold using only feasible truncation "
					"points, on convex hull.\n");
	fprintf(stdout, "[-n|-num_resolutions] <number of resolutions>\n");
	fprintf(stdout, "    Number of resolutions.\n");
	fprintf(stdout, "    This value corresponds to the (number of DWT decompositions + 1). \n");
	fprintf(stdout, "    Default: 6.\n");
	fprintf(stdout, "[-b|-code_block_dims] <cblk width>,<cblk height>\n");
	fprintf(stdout, "    Code-block dimensions. The dimensions must respect the constraint \n");
	fprintf(stdout, "    defined in the JPEG 2000 standard: no dimension smaller than 4 \n");
	fprintf(stdout, "    or greater than 1024, no code-block with more than 4096 coefficients.\n");
	fprintf(stdout, "    The maximum value permitted is `64,64`. \n");
	fprintf(stdout, "    Default: `64,64`.\n");
	fprintf(stdout,
			"[-c|-precinct_dims] [<prec width>,<prec height>],[<prec width>,<prec height>],...\n");
	fprintf(stdout, "    Precinct dimensions. Dimensions specified must be powers of 2. \n");
	fprintf(stdout,
			"    Multiple records may be specified, in which case the first record refers \n");
	fprintf(stdout, "    to the highest resolution level and subsequent records refer to lower \n");
	fprintf(stdout, "    resolution levels. The last specified record's dimensions are "
					"progressively right-shifted (halved in size) \n");
	fprintf(stdout, "    for each remaining lower resolution level.\n");
	fprintf(stdout, "    Default: 2^15x2^15 at each resolution i.e. precincts are not used.\n");
	fprintf(stdout, "[-t|-tile_dims] <tile width>,<tile height>\n");
	fprintf(stdout, "    Tile dimensions.\n");
	fprintf(stdout, "    Default: the dimension of the whole image, thus only one tile.\n");
	fprintf(stdout, "[-p|-progression_order] <LRCP|RLCP|RPCL|PCRL|CPRL>\n");
	fprintf(stdout, "    Progression order.\n");
	fprintf(stdout, "    Default: LRCP.\n");
	fprintf(stdout, "[-P|-POC] <progression order change>/<progression order change>/...\n");
	fprintf(stdout, "    Progression order change.\n");
	fprintf(stdout, "    The syntax of a progression order change is the following:\n");
	fprintf(stdout,
			"    T<tile>=<resStart>,<compStart>,<layerEnd>,<resEnd>,<compEnd>,<progOrder>\n");
	fprintf(stdout, "      Example: -POC T1=0,0,1,5,3,CPRL/T1=5,0,1,6,3,CPRL\n");
	fprintf(stdout, "[-S|-SOP]\n");
	fprintf(stdout, "    Write SOP marker before each packet.\n");
	fprintf(stdout, "[-E|-EPH]\n");
	fprintf(stdout, "    Write EPH marker after each header packet.\n");
	fprintf(stdout, "[-M|-mode] <key value>\n");
	fprintf(stdout, "    mode switch.\n");
	fprintf(stdout, "    [1=BYPASS(LAZY)\n"
					"     2=RESET\n"
					"     4=RESTART(TERMALL)\n"
					"     8=VSC 16=ERTERM(SEGTERM)\n"
					"     32=SEGMARK(SEGSYM)\n"
					"     64=HT(High Throughput)]\n\n");
	fprintf(stdout, "    Multiple modes can be specified by adding their values together.\n");
	fprintf(stdout, "      Example: RESTART(4) + RESET(2) + SEGMARK(32) => -M 38\n");
	fprintf(stdout, "      Note: 64(HT) cannot be combined with other flags\n");
	fprintf(stdout, "[-u|-tile_parts] <R|L|C>\n");
	fprintf(stdout, "    Divide packets of every tile into tile-parts.\n");
	fprintf(stdout, "    Division is made by grouping Resolutions (R), Layers (L)\n");
	fprintf(stdout, "    or Components (C).\n");
	fprintf(stdout, "[-R|-ROI] c=<component index>,U=<upshifting value>\n");
	fprintf(stdout, "    Quantization indices up-shifted for a component. \n");
	fprintf(stdout, "     This option does not implement the usual ROI (Region of Interest).\n");
	fprintf(stdout, "    It should be understood as a 'Component of Interest'. It offers the \n");
	fprintf(stdout,
			"    possibility to up-shift the value of a component during the quantization step.\n");
	fprintf(stdout,
			"    The value after c= is the component number [0, 1, 2, ...] and the value \n");
	fprintf(stdout, "    after U= is the value of up-shifting. U must be in the range [0, 37].\n");
	fprintf(stdout, "[-d|-image_offset] <image offset X,image offset Y>\n");
	fprintf(stdout, "    Offset of the origin of the image.\n");
	fprintf(stdout, "[-T|-tile_offset] <tile offset X,tile offset Y>\n");
	fprintf(stdout, "    Offset of the origin of the tiles.\n");
	fprintf(stdout, "[-L|-PLT\n");
	fprintf(stdout, "    Use PLT markers.\n");
	fprintf(stdout, "[-I|-Irreversible\n");
	fprintf(stdout, "    Use the irreversible DWT 9-7.\n");
	fprintf(stdout, "[-Y|-MCT] <0|1|2>\n");
	fprintf(stdout, "    Specifies explicitly if a Multiple Component Transform has to be used.\n");
	fprintf(stdout, "    0: no MCT ; 1: RGB->YCC conversion ; 2: custom MCT.\n");
	fprintf(stdout, "    If custom MCT, \"-m\" option has to be used (see hereunder).\n");
	fprintf(stdout,
			"    By default, RGB->YCC conversion is used if there are 3 components or more,\n");
	fprintf(stdout, "    no conversion otherwise.\n");
	fprintf(stdout, "[-m|-custom_mct <file>\n");
	fprintf(stdout, "    Use array-based MCT, values are coma separated, line by line\n");
	fprintf(stdout, "    No specific separators between lines, no space allowed between values.\n");
	fprintf(stdout, "    If this option is used, it automatically sets \"-MCT\" option to 2.\n");
	fprintf(stdout, "[-Z|-rsiz] <rsiz>\n");
	fprintf(stdout, "    Profile, main level, sub level and version.\n");
	fprintf(stdout, "   Note: this flag will be ignored if cinema profile flags are used.\n");
	fprintf(stdout, "[-w|-cinema2K] <24|48>\n");
	fprintf(stdout, "    Digital Cinema 2K profile compliant code stream.\n");
	fprintf(stdout, "   Need to specify the frames per second.\n");
	fprintf(stdout, "    Only 24 or 48 fps are currently allowed.\n");
	fprintf(stdout, "[-x|-cinema4k] <24|48>\n");
	fprintf(stdout, "    Digital Cinema 4K profile compliant code stream.\n");
	fprintf(stdout, "   Need to specify the frames per second.\n");
	fprintf(stdout, "    Only 24 or 48 fps are currently allowed.\n");
	fprintf(stdout, "-U|-broadcast <PROFILE>[,mainlevel=X][,framerate=FPS]\n");
	fprintf(stdout, "    Broadcast compliant code stream.\n");
	fprintf(stdout, "    <PROFILE>=SINGLE,MULTI and MULTI_R.\n");
	fprintf(stdout, "    X >= 0 and X <= 11.\n");
	fprintf(stdout, "    framerate > 0 may be specified to enhance checks and set maximum bit rate "
					"when Y > 0.\n");
	fprintf(stdout, "-z|-IMF <PROFILE>[,mainlevel=X][,sublevel=Y][,framerate=FPS]\n");
	fprintf(stdout, "    Interoperable Master Format compliant code stream.\n");
	fprintf(stdout, "    <PROFILE>=2K, 4K, 8K, 2K_R, 4K_R or 8K_R.\n");
	fprintf(stdout, "    X >= 0 and X <= 11.\n");
	fprintf(stdout, "    Y >= 0 and Y <= 9.\n");
	fprintf(stdout, "    framerate > 0 may be specified to enhance checks and set maximum bit rate "
					"when Y > 0.\n");
	fprintf(stdout, "[-C|-comment] <comment>\n");
	fprintf(stdout, "    Add <comment> in the comment marker segment.\n");
	fprintf(stdout, "[-Q|-capture_res] <capture resolution X,capture resolution Y>\n");
	fprintf(stdout, "    Capture resolution in pixels/metre, in double precision.\n");
	fprintf(
		stdout,
		"    These values will override the resolution stored in the input image, if present \n");
	fprintf(stdout, "    unless the special values <0,0> are passed in, in which case \n");
	fprintf(stdout, "    the image resolution will be used.\n");
	fprintf(stdout, "[-D|-display_res] <display resolution X,display resolution Y>\n");
	fprintf(stdout, "    Display resolution in pixels/metre, in double precision.\n");
	fprintf(stdout, "[-e|-Repetitions] <number of repetitions>\n");
	fprintf(stdout, "    Number of repetitions, for either a single image, or a folder of images.\n"
					"    Default is 1. 0 signifies unlimited repetitions. \n");
	fprintf(stdout, "[-g|-plugin_path] <plugin path>\n");
	fprintf(stdout, "    Path to T1 plugin.\n");
	fprintf(stdout, "[-H|-num_threads] <number of threads>\n");
	fprintf(stdout, "    Number of threads used by libgrokj2k library.\n");
	fprintf(stdout, "[-G|-device_id] <device ID>\n");
	fprintf(stdout, "    (GPU) Specify which GPU accelerator to run codec on.\n");
	fprintf(stdout, "    A value of -1 will specify all devices.\n");
	fprintf(stdout, "[-W | -logfile] <log file name>\n"
					"    log to file. File name will be set to \"log file name\"\n");
}

static GRK_PROG_ORDER getProgression(const char progression[4])
{
	if(strncmp(progression, "LRCP", 4) == 0)
		return GRK_LRCP;
	if(strncmp(progression, "RLCP", 4) == 0)
		return GRK_RLCP;
	if(strncmp(progression, "RPCL", 4) == 0)
		return GRK_RPCL;
	if(strncmp(progression, "PCRL", 4) == 0)
		return GRK_PCRL;
	if(strncmp(progression, "CPRL", 4) == 0)
		return GRK_CPRL;

	return GRK_PROG_UNKNOWN;
}
CompressInitParams::CompressInitParams() : initialized(false), transferExifTags(false)
{
	pluginPath[0] = 0;
	memset(&inputFolder, 0, sizeof(inputFolder));
	memset(&outFolder, 0, sizeof(outFolder));
}
CompressInitParams::~CompressInitParams()
{
	for(size_t i = 0; i < parameters.num_comments; ++i)
		delete[] parameters.comment[i];
	free(parameters.raw_cp.comps);
	free(inputFolder.imgdirpath);
	free(outFolder.imgdirpath);
}
static char nextFile(std::string inputFile, grk_img_fol* inputFolder, grk_img_fol* outFolder,
					 grk_cparameters* parameters)
{
	spdlog::info("File \"{}\"", inputFile.c_str());
	std::string infilename =
		inputFolder->imgdirpath + std::string(grk::pathSeparator()) + inputFile;
	if(parameters->decod_format == GRK_FMT_UNK)
	{
		int fmt = grk_get_file_format((char*)infilename.c_str());
		if(fmt <= GRK_FMT_UNK)
			return 1;
		parameters->decod_format = (GRK_SUPPORTED_FILE_FMT)fmt;
	}
	if(grk::strcpy_s(parameters->infile, sizeof(parameters->infile), infilename.c_str()) != 0)
	{
		return 1;
	}
	std::string outputRootFile;
	// if we don't find a file tag, then just use the full file name
	auto pos = inputFile.find(".");
	if(pos != std::string::npos)
		outputRootFile = inputFile.substr(0, pos);
	else
		outputRootFile = inputFile;
	if(inputFolder->set_out_format)
	{
		std::string outfilename = outFolder->imgdirpath + std::string(grk::pathSeparator()) +
								  outputRootFile + "." + inputFolder->out_format;
		if(grk::strcpy_s(parameters->outfile, sizeof(parameters->outfile), outfilename.c_str()) !=
		   0)
		{
			return 1;
		}
	}
	return 0;
}
static bool isDecodedFormatSupported(GRK_SUPPORTED_FILE_FMT format)
{
	switch(format)
	{
		case GRK_FMT_PGX:
		case GRK_FMT_PXM:
		case GRK_FMT_BMP:
		case GRK_FMT_TIF:
		case GRK_FMT_RAW:
		case GRK_FMT_RAWL:
		case GRK_FMT_PNG:
		case GRK_FMT_JPG:
			break;
		default:
			return false;
	}
	return true;
}

class GrokOutput : public TCLAP::StdOutput
{
  public:
	virtual void usage(TCLAP::CmdLineInterface& c)
	{
		GRK_UNUSED(c);
		compress_help_display();
	}
};
static bool validateCinema(TCLAP::ValueArg<uint32_t>* arg, uint16_t profile,
						   grk_cparameters* parameters)
{
	if(arg->isSet())
	{
		uint16_t fps = (uint16_t)arg->getValue();
		if(fps != 24 && fps != 48)
		{
			spdlog::warn("Incorrect digital cinema frame rate {} : "
						 "      must be either 24 or 48. Ignoring",
						 fps);
			return false;
		}
		parameters->rsiz = profile;
		parameters->framerate = fps;
		if(fps == 24)
		{
			parameters->max_comp_size = GRK_CINEMA_24_COMP;
			parameters->max_cs_size = GRK_CINEMA_24_CS;
		}
		else if(fps == 48)
		{
			parameters->max_comp_size = GRK_CINEMA_48_COMP;
			parameters->max_cs_size = GRK_CINEMA_48_CS;
		}
		if(profile == GRK_PROFILE_CINEMA_2K)
		{
			parameters->numgbits = 1;
		}
		else
		{
			parameters->numgbits = 2;
		}
	}
	return true;
}

int GrkCompress::main(int argc, char** argv)
{
	CompressInitParams initParams;
	int success = 0;
	try
	{
		// try to compress with plugin
		int rc = pluginMain(argc, argv, &initParams);

		// return immediately if either
		// initParams was not initialized (something was wrong with command line params)
		// or plugin was successful
		if(!initParams.initialized)
			return 1;
		if(!rc)
			return 0;
		size_t numCompressedFiles = 0;

		// cache certain settings
		grk_cparameters parametersCache = initParams.parameters;
		auto start = std::chrono::high_resolution_clock::now();
		for(uint32_t i = 0; i < initParams.parameters.repeats; ++i)
		{
			if(!initParams.inputFolder.set_imgdir)
			{
				initParams.parameters = parametersCache;
				if(compress("", &initParams) == 0)
				{
					success = 1;
					goto cleanup;
				}
				numCompressedFiles++;
			}
			else
			{
				for(const auto& entry :
					std::filesystem::directory_iterator(initParams.inputFolder.imgdirpath))
				{
					initParams.parameters = parametersCache;
					if(compress(entry.path().filename().string(), &initParams) == 1)
						numCompressedFiles++;
				}
			}
		}
		auto finish = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> elapsed = finish - start;
		if(numCompressedFiles)
		{
			spdlog::info("compress time: {} {}",
						 (elapsed.count() * 1000) / (double)numCompressedFiles,
						 numCompressedFiles > 1 ? "ms/image" : "ms");
		}
	}
	catch(std::bad_alloc& ba)
	{
		spdlog::error(" Out of memory. Exiting.");
		success = 1;
		goto cleanup;
	}
cleanup:
	grk_deinitialize();

	return success;
}

int GrkCompress::pluginMain(int argc, char** argv, CompressInitParams* initParams)
{
	if(!initParams)
		return 1;
	int32_t success = 0;
	bool isBatch = false;
	uint32_t state = 0;

	/* set compressing parameters to default values */
	grk_compress_set_default_params(&initParams->parameters);
	/* parse input and get user compressing parameters */
	initParams->parameters.mct =
		255; /* This will be set later according to the input image or the provided option */
	initParams->parameters.rateControlAlgorithm = GRK_RATE_CONTROL_PCRD_OPT;
	if(parseCommandLine(argc, argv, initParams) == 1)
	{
		success = 1;
		goto cleanup;
	}
	isBatch = initParams->inputFolder.imgdirpath && initParams->outFolder.imgdirpath;
	state = grk_plugin_get_debug_state();
#ifdef GROK_HAVE_LIBTIFF
	tiffSetErrorAndWarningHandlers(initParams->parameters.verbose);
#endif
	initParams->initialized = true;
	// load plugin but do not actually create codec
	if(!grk_initialize(initParams->pluginPath, initParams->parameters.numThreads))
	{
		success = 1;
		goto cleanup;
	}
	img_fol_plugin = initParams->inputFolder;
	out_fol_plugin = initParams->outFolder;

	// create codec
	grk_plugin_init_info initInfo;
	initInfo.deviceId = initParams->parameters.deviceId;
	initInfo.verbose = initParams->parameters.verbose;
	if(!grk_plugin_init(initInfo))
	{
		success = 1;
		goto cleanup;
	}
	if((state & GRK_PLUGIN_STATE_DEBUG) || (state & GRK_PLUGIN_STATE_PRE_TR1))
		isBatch = 0;
	if(isBatch)
	{
		setUpSignalHandler();
		success = grk_plugin_batch_compress(initParams->inputFolder.imgdirpath,
											initParams->outFolder.imgdirpath,
											&initParams->parameters, pluginCompressCallback);
		// if plugin successfully begins batch compress, then wait for batch to complete
		if(success == 0)
		{
			uint32_t slice = 100; // ms
			uint32_t slicesPerSecond = 1000 / slice;
			uint32_t seconds = initParams->parameters.duration;
			if(!seconds)
				seconds = UINT_MAX;
			for(uint32_t i = 0U; i < seconds * slicesPerSecond; ++i)
			{
				batch_sleep(1);
				if(grk_plugin_is_batch_complete())
					break;
			}
			grk_plugin_stop_batch_compress();
		}
	}
	else
	{
		if(!initParams->inputFolder.set_imgdir)
		{
			success = grk_plugin_compress(&initParams->parameters, pluginCompressCallback);
		}
		else
		{
			// cache certain settings
			auto mct = initParams->parameters.mct;
			auto rateControlAlgorithm = initParams->parameters.rateControlAlgorithm;
			for(const auto& entry :
				std::filesystem::directory_iterator(initParams->inputFolder.imgdirpath))
			{
				if(nextFile(entry.path().filename().string(), &initParams->inputFolder,
							initParams->outFolder.imgdirpath ? &initParams->outFolder
															 : &initParams->inputFolder,
							&initParams->parameters))
				{
					continue;
				}
				// restore cached settings
				initParams->parameters.mct = mct;
				initParams->parameters.rateControlAlgorithm = rateControlAlgorithm;
				success = grk_plugin_compress(&initParams->parameters, pluginCompressCallback);
				if(success != 0)
					break;
			}
		}
	}
cleanup:
	return success;
}

int GrkCompress::parseCommandLine(int argc, char** argv, CompressInitParams* initParams)
{
	grk_cparameters* parameters = &initParams->parameters;
	grk_img_fol* inputFolder = &initParams->inputFolder;
	grk_img_fol* outFolder = &initParams->outFolder;
	char* pluginPath = initParams->pluginPath;

	try
	{
		TCLAP::CmdLine cmd("grk_compress command line", ' ', grk_version());

		// set the output
		GrokOutput output;
		cmd.setOutput(&output);

		TCLAP::ValueArg<std::string> outDirArg("a", "out_dir", "Output directory", false, "",
											   "string", cmd);
		TCLAP::ValueArg<uint32_t> rateControlAlgoArg("A", "rate_control_algorithm",
													 "Rate control algorithm", false, 0,
													 "unsigned integer", cmd);
		TCLAP::ValueArg<std::string> codeBlockDimArg(
			"b", "code_block_dims", "Code block dimensions", false, "", "string", cmd);

		TCLAP::ValueArg<std::string> precinctDimArg("c", "precinct_dims", "Precinct dimensions",
													false, "", "string", cmd);

		TCLAP::ValueArg<std::string> commentArg("C", "comment", "Add a comment", false, "",
												"string", cmd);
		TCLAP::ValueArg<std::string> imageOffsetArg("d", "image_offset",
													"Image offset in reference grid coordinates",
													false, "", "string", cmd);
		TCLAP::ValueArg<std::string> displayResArg("D", "display_res", "Display resolution", false,
												   "", "string", cmd);
		TCLAP::ValueArg<uint32_t> repetitionsArg(
			"e", "repetitions",
			"Number of compress repetitions, for either a folder or a single file", false, 0,
			"unsigned integer", cmd);
		TCLAP::SwitchArg ephArg("E", "EPH", "Add EPH markers", cmd);

		TCLAP::ValueArg<std::string> rawFormatArg("F", "raw", "raw image format parameters", false,
												  "", "string", cmd);
		TCLAP::ValueArg<std::string> pluginPathArg("g", "plugin_path", "Plugin path", false, "",
												   "string", cmd);
		TCLAP::ValueArg<int32_t> deviceIdArg("G", "device_id", "Device ID", false, 0, "integer",
											 cmd);
		TCLAP::ValueArg<uint32_t> numThreadsArg("H", "num_threads", "Number of threads", false, 0,
												"unsigned integer", cmd);
		TCLAP::ValueArg<std::string> inputFileArg("i", "in_file", "Input file", false, "", "string",
												  cmd);
		TCLAP::SwitchArg irreversibleArg("I", "irreversible", "Irreversible", cmd);
		TCLAP::ValueArg<uint32_t> durationArg("J", "duration", "Duration in seconds", false, 0,
											  "unsigned integer", cmd);
		// Kernel build flags:
		// 1 indicates build binary, otherwise load binary
		// 2 indicates generate binaries
		TCLAP::ValueArg<uint32_t> kernelBuildOptionsArg("k", "kernel_build", "Kernel build options",
														false, 0, "unsigned integer", cmd);
		TCLAP::ValueArg<std::string> inForArg("K", "in_fmt", "InputFormat format", false, "",
											  "string", cmd);
		TCLAP::SwitchArg pltArg("L", "PLT", "PLT marker", cmd);
		TCLAP::ValueArg<std::string> customMCTArg("m", "custom_mct", "MCT input file", false, "",
												  "string", cmd);
		TCLAP::ValueArg<uint32_t> cblkSty("M", "mode", "mode", false, 0, "unsigned integer", cmd);

		TCLAP::ValueArg<uint32_t> resolutionArg("n", "num_resolutions", "Resolution", false, 0,
												"unsigned integer", cmd);
		TCLAP::ValueArg<uint32_t> guardBits("N", "guard_bits", "Number of guard bits", false, 2,
											"unsigned integer", cmd);
		TCLAP::ValueArg<std::string> outputFileArg("o", "out_file", "Output file", false, "",
												   "string", cmd);
		TCLAP::ValueArg<std::string> outForArg("O", "out_fmt", "Output format", false, "", "string",
											   cmd);
		TCLAP::ValueArg<std::string> progressionOrderArg(
			"p", "progression_order", "Progression order", false, "", "string", cmd);

		TCLAP::ValueArg<std::string> pocArg("P", "POC", "Progression order changes", false, "",
											"string", cmd);
		TCLAP::ValueArg<std::string> qualityArg("q", "quality", "layer rates expressed as quality",
												false, "", "string", cmd);
		TCLAP::ValueArg<std::string> captureResArg("Q", "capture_res", "Capture resolution", false,
												   "", "string", cmd);
		TCLAP::ValueArg<std::string> compressionRatiosArg(
			"r", "compression_ratios", "layer rates expressed as compression ratios", false, "",
			"string", cmd);
		TCLAP::ValueArg<std::string> roiArg("R", "ROI", "Region of interest", false, "", "string",
											cmd);
		TCLAP::SwitchArg sopArg("S", "SOP", "Add SOP markers", cmd);

		TCLAP::ValueArg<std::string> tileOffsetArg("T", "tile_offset", "Tile offset", false, "",
												   "string", cmd);

		TCLAP::ValueArg<std::string> tilesArg("t", "tile_dims", "Tile dimensions", false, "",
											  "string", cmd);
		TCLAP::ValueArg<uint8_t> tpArg("u", "tile_parts", "Tile part generation", false, 0,
									   "uint8_t", cmd);
		TCLAP::SwitchArg verboseArg("v", "verbose", "Verbose", cmd);
		TCLAP::SwitchArg transferExifTagsArg("V", "transfer_exif_tags", "Transfer Exif tags", cmd);
		TCLAP::ValueArg<std::string> logfileArg("W", "logfile", "Log file", false, "", "string",
												cmd);
		TCLAP::ValueArg<uint32_t> cinema2KArg("w", "cinema2K", "Digital cinema 2K profile", false,
											  24, "unsigned integer", cmd);
		TCLAP::ValueArg<uint32_t> cinema4KArg("x", "cinema4k", "Digital cinema 2K profile", false,
											  24, "unsigned integer", cmd);
		TCLAP::SwitchArg tlmArg("X", "TLM", "TLM marker", cmd);
		TCLAP::ValueArg<std::string> inDirArg("y", "in_dir", "Image directory", false, "", "string",
											  cmd);
		TCLAP::ValueArg<uint32_t> mctArg("Y", "MCT", "Multi component transform", false, 0,
										 "unsigned integer", cmd);
		TCLAP::ValueArg<uint16_t> rsizArg("Z", "rsiz", "rsiz", false, 0, "unsigned integer", cmd);
		TCLAP::ValueArg<std::string> IMFArg("z", "IMF", "IMF profile", false, "", "string", cmd);

		TCLAP::ValueArg<std::string> BroadcastArg("U", "broadcast", "Broadcast profile", false, "",
												  "string", cmd);
		cmd.parse(argc, argv);

		initParams->transferExifTags = transferExifTagsArg.isSet();
		if(logfileArg.isSet())
		{
			auto file_logger = spdlog::basic_logger_mt("grk_compress", logfileArg.getValue());
			spdlog::set_default_logger(file_logger);
		}

		inputFolder->set_out_format = false;
		parameters->raw_cp.width = 0;

		if(pltArg.isSet())
			parameters->writePLT = true;

		if(tlmArg.isSet())
			parameters->writeTLM = true;

		if(verboseArg.isSet())
			parameters->verbose = true;
		else
			spdlog::set_level(spdlog::level::level_enum::err);

		if(repetitionsArg.isSet())
			parameters->repeats = repetitionsArg.getValue();

		if(kernelBuildOptionsArg.isSet())
			parameters->kernelBuildOptions = kernelBuildOptionsArg.getValue();

		if(rateControlAlgoArg.isSet())
		{
			uint32_t algo = rateControlAlgoArg.getValue();
			if(algo > GRK_RATE_CONTROL_PCRD_OPT)
				spdlog::warn("Rate control algorithm %u is not valid. Using default");
			else
				parameters->rateControlAlgorithm =
					(GRK_RATE_CONTROL_ALGORITHM)rateControlAlgoArg.getValue();
		}

		if(numThreadsArg.isSet())
			parameters->numThreads = numThreadsArg.getValue();

		if(deviceIdArg.isSet())
			parameters->deviceId = deviceIdArg.getValue();

		if(durationArg.isSet())
			parameters->duration = durationArg.getValue();

		if(inForArg.isSet())
		{
			auto dummy = "dummy." + inForArg.getValue();
			char* infile = (char*)(dummy).c_str();
			parameters->decod_format = (GRK_SUPPORTED_FILE_FMT)grk_get_file_format((char*)infile);
			if(!isDecodedFormatSupported(parameters->decod_format))
			{
				spdlog::warn(" Ignoring unknown input file format: %s \n"
							 "Known file formats are *.pnm, *.pgm, "
							 "*.ppm, *.pgx, *png, *.bmp, *.tif, *.jpg"
							 " or *.raw",
							 infile);
			}
		}

		if(inputFileArg.isSet())
		{
			char* infile = (char*)inputFileArg.getValue().c_str();
			if(parameters->decod_format == GRK_FMT_UNK)
			{
				parameters->decod_format = (GRK_SUPPORTED_FILE_FMT)grk_get_file_format(infile);
				if(!isDecodedFormatSupported(parameters->decod_format))
				{
					spdlog::error("Unknown input file format: {} \n"
								  "        Known file formats are *.pnm, *.pgm, *.ppm, *.pgx, "
								  "*png, *.bmp, *.tif, *.jpg or *.raw",
								  infile);
					return 1;
				}
			}
			if(grk::strcpy_s(parameters->infile, sizeof(parameters->infile), infile) != 0)
			{
				return 1;
			}
		}
		else
		{
			// check for possible input from STDIN
			if(!inDirArg.isSet())
			{
				bool fromStdin =
					inForArg.isSet() &&
					grk::supportedStdioFormat((GRK_SUPPORTED_FILE_FMT)parameters->decod_format,true);
				if(!fromStdin)
				{
					spdlog::error("Missing input file");
					return 1;
				}
			}
		}

		if(outForArg.isSet())
		{
			char outformat[50];
			char* of = (char*)outForArg.getValue().c_str();
			sprintf(outformat, ".%s", of);
			inputFolder->set_out_format = true;
			parameters->cod_format = (GRK_SUPPORTED_FILE_FMT)grk_get_file_format(outformat);
			switch(parameters->cod_format)
			{
				case GRK_FMT_J2K:
					inputFolder->out_format = "j2k";
					break;
				case GRK_FMT_JP2:
					inputFolder->out_format = "jp2";
					break;
				default:
					spdlog::error("Unknown output format image [only j2k, j2c, jp2] ");
					return 1;
			}
		}

		if(outputFileArg.isSet())
		{
			char* outfile = (char*)outputFileArg.getValue().c_str();
			parameters->cod_format = (GRK_SUPPORTED_FILE_FMT)grk_get_file_format(outfile);
			switch(parameters->cod_format)
			{
				case GRK_FMT_J2K:
				case GRK_FMT_JP2:
					break;
				default:
					spdlog::error("Unknown output format image {} [only *.j2k, *.j2c or *.jp2] ",
								  outfile);
					return 1;
			}
			if(grk::strcpy_s(parameters->outfile, sizeof(parameters->outfile), outfile) != 0)
			{
				return 1;
			}
		}
		bool isHT = false;
		if(cblkSty.isSet())
		{
			parameters->cblk_sty = cblkSty.getValue() & 0X7F;
			if(parameters->cblk_sty & GRK_CBLKSTY_HT)
			{
				if(parameters->cblk_sty != GRK_CBLKSTY_HT)
				{
					spdlog::error("High throughput compressing mode cannot be combined"
								  " with any other block mode switches. Ignoring mode switch");
					parameters->cblk_sty = 0;
				}
				else
				{
					isHT = true;
					parameters->numgbits = 1;
					if(compressionRatiosArg.isSet() || qualityArg.isSet())
					{
						spdlog::warn("HTJ2K compression using rate distortion or quality"
									 " is not currently supported.");
					}
				}
			}
		}
		if(!isHT && compressionRatiosArg.isSet() && qualityArg.isSet())
		{
			spdlog::error("compression by both rate distortion and quality is not allowed");
			return 1;
		}
		if(!isHT && compressionRatiosArg.isSet())
		{
			char* s = (char*)compressionRatiosArg.getValue().c_str();
			parameters->numlayers = 0;
			while(sscanf(s, "%lf", &parameters->layer_rate[parameters->numlayers]) == 1)
			{
				parameters->numlayers++;
				while(*s && *s != ',')
				{
					s++;
				}
				if(!*s)
					break;
				s++;
			}

			// sanity check on rates
			double lastRate = DBL_MAX;
			for(uint32_t i = 0; i < parameters->numlayers; ++i)
			{
				if(parameters->layer_rate[i] > lastRate)
				{
					spdlog::error("rates must be listed in descending order");
					return 1;
				}
				if(parameters->layer_rate[i] < 1.0)
				{
					spdlog::error("rates must be greater than or equal to one");
					return 1;
				}
				lastRate = parameters->layer_rate[i];
			}

			parameters->allocationByRateDistoration = true;
			// set compression ratio of 1 equal to 0, to signal lossless layer
			for(uint32_t i = 0; i < parameters->numlayers; ++i)
			{
				if(parameters->layer_rate[i] == 1)
					parameters->layer_rate[i] = 0;
			}
		}
		else if(!isHT && qualityArg.isSet())
		{
			char* s = (char*)qualityArg.getValue().c_str();
			;
			while(sscanf(s, "%lf", &parameters->layer_distortion[parameters->numlayers]) == 1)
			{
				parameters->numlayers++;
				while(*s && *s != ',')
				{
					s++;
				}
				if(!*s)
					break;
				s++;
			}
			parameters->allocationByQuality = true;

			// sanity check on quality values
			double lastDistortion = -1;
			for(uint16_t i = 0; i < parameters->numlayers; ++i)
			{
				auto distortion = parameters->layer_distortion[i];
				if(distortion < 0)
				{
					spdlog::error("PSNR values must be greater than or equal to zero");
					return 1;
				}
				if(distortion < lastDistortion &&
				   !(i == (uint16_t)(parameters->numlayers - 1) && distortion == 0))
				{
					spdlog::error("PSNR values must be listed in ascending order");
					return 1;
				}
				lastDistortion = distortion;
			}
		}
		else
		{
			/* if no rate was entered, then lossless by default */
			parameters->layer_rate[0] = 0;
			parameters->numlayers = 1;
			parameters->allocationByRateDistoration = true;
		}
		if(rawFormatArg.isSet())
		{
			bool wrong = false;
			int width, height, bitdepth, ncomp;
			uint32_t len;
			bool raw_signed = false;
			char* substr2 = (char*)strchr(rawFormatArg.getValue().c_str(), '@');
			if(substr2 == nullptr)
			{
				len = (uint32_t)rawFormatArg.getValue().length();
			}
			else
			{
				len = (uint32_t)(substr2 - rawFormatArg.getValue().c_str());
				substr2++; /* skip '@' character */
			}
			char* substr1 = (char*)malloc((len + 1) * sizeof(char));
			if(substr1 == nullptr)
			{
				return 1;
			}
			memcpy(substr1, rawFormatArg.getValue().c_str(), len);
			substr1[len] = '\0';
			char signo;
			if(sscanf(substr1, "%u,%u,%u,%u,%c", &width, &height, &ncomp, &bitdepth, &signo) == 5)
			{
				if(signo == 's')
				{
					raw_signed = true;
				}
				else if(signo == 'u')
				{
					raw_signed = false;
				}
				else
				{
					wrong = true;
				}
			}
			else
			{
				wrong = true;
			}
			if(!wrong)
			{
				grk_raw_cparameters* raw_cp = &parameters->raw_cp;
				int compno;
				int lastdx = 1;
				int lastdy = 1;
				raw_cp->width = (uint32_t)width;
				raw_cp->height = (uint32_t)height;
				raw_cp->numcomps = (uint16_t)ncomp;
				raw_cp->prec = (uint8_t)bitdepth;
				raw_cp->sgnd = raw_signed;
				raw_cp->comps = (grk_raw_comp_cparameters*)malloc(((uint32_t)(ncomp)) *
																  sizeof(grk_raw_comp_cparameters));
				if(raw_cp->comps == nullptr)
				{
					free(substr1);
					return 1;
				}
				for(compno = 0; compno < ncomp && !wrong; compno++)
				{
					if(substr2 == nullptr)
					{
						raw_cp->comps[compno].dx = (uint8_t)lastdx;
						raw_cp->comps[compno].dy = (uint8_t)lastdy;
					}
					else
					{
						int dx, dy;
						char* sep = strchr(substr2, ':');
						if(sep == nullptr)
						{
							if(sscanf(substr2, "%ux%u", &dx, &dy) == 2)
							{
								lastdx = dx;
								lastdy = dy;
								raw_cp->comps[compno].dx = (uint8_t)dx;
								raw_cp->comps[compno].dy = (uint8_t)dy;
								substr2 = nullptr;
							}
							else
							{
								wrong = true;
							}
						}
						else
						{
							if(sscanf(substr2, "%ux%u:%s", &dx, &dy, substr2) == 3)
							{
								raw_cp->comps[compno].dx = (uint8_t)dx;
								raw_cp->comps[compno].dy = (uint8_t)dy;
							}
							else
							{
								wrong = true;
							}
						}
					}
				}
			}
			free(substr1);
			if(wrong)
			{
				spdlog::error("\n invalid raw image parameters");
				spdlog::error("Please use the Format option -F:");
				spdlog::error(
					"-F <width>,<height>,<ncomp>,<bitdepth>,{s,u}@<dx1>x<dy1>:...:<dxn>x<dyn>");
				spdlog::error("If subsampling is omitted, 1x1 is assumed for all components");
				spdlog::error("Example: -i image.raw -o image.j2k -F 512,512,3,8,u@1x1:2x2:2x2");
				spdlog::error("         for raw 512x512 image with 4:2:0 subsampling");
				return 1;
			}
		}

		if(resolutionArg.isSet())
			parameters->numresolution = (uint8_t)resolutionArg.getValue();

		if(precinctDimArg.isSet())
		{
			char sep;
			int res_spec = 0;

			char* s = (char*)precinctDimArg.getValue().c_str();
			do
			{
				sep = 0;
				int ret = sscanf(s, "[%u,%u]%c", &parameters->prcw_init[res_spec],
								 &parameters->prch_init[res_spec], &sep);
				if(!(ret == 2 && sep == 0) && !(ret == 3 && sep == ','))
				{
					spdlog::error("Could not parse precinct dimension: {} {}", s, sep);
					spdlog::error("Example: -i lena.raw -o lena.j2k -c [128,128],[128,128]");
					return 1;
				}
				parameters->csty |= 0x01;
				res_spec++;
				s = strpbrk(s, "]") + 2;
			} while(sep == ',');
			parameters->res_spec = (uint32_t)res_spec;
		}

		if(codeBlockDimArg.isSet())
		{
			int cblockw_init = 0, cblockh_init = 0;
			if(sscanf(codeBlockDimArg.getValue().c_str(), "%u,%u", &cblockw_init, &cblockh_init) ==
			   EOF)
			{
				spdlog::error("sscanf failed for code block dimension argument");
				return 1;
			}
			if(cblockw_init * cblockh_init > 4096 || cblockw_init > 1024 || cblockw_init < 4 ||
			   cblockh_init > 1024 || cblockh_init < 4)
			{
				spdlog::error("Size of code block error (option -b)\n\nRestriction :\n"
							  "    * width*height<=4096\n    * 4<=width,height<= 1024");
				return 1;
			}
			parameters->cblockw_init = (uint32_t)cblockw_init;
			parameters->cblockh_init = (uint32_t)cblockh_init;
		}
		if(pocArg.isSet())
		{
			uint32_t numProgressions = 0;
			char* s = (char*)pocArg.getValue().c_str();
			auto progression = parameters->progression;
			uint32_t resS, compS, layE, resE, compE;

			while(sscanf(s, "T%u=%u,%u,%u,%u,%u,%4s", &progression[numProgressions].tileno, &resS,
						 &compS, &layE, &resE, &compE,
						 progression[numProgressions].progressionString) == 7)
			{
				progression[numProgressions].resS = (uint8_t)resS;
				progression[numProgressions].compS = (uint16_t)compS;
				progression[numProgressions].layE = (uint16_t)layE;
				progression[numProgressions].resE = (uint8_t)resE;
				progression[numProgressions].compE = (uint16_t)compE;
				progression[numProgressions].specifiedCompressionPocProg =
					getProgression(progression[numProgressions].progressionString);
				// sanity check on layer
				if(progression[numProgressions].layE > parameters->numlayers)
				{
					spdlog::warn("End layer {} in POC {} is greater than"
								 " total number of layers {}. Truncating.",
								 progression[numProgressions].layE, numProgressions,
								 parameters->numlayers);
					progression[numProgressions].layE = parameters->numlayers;
				}
				if(progression[numProgressions].resE > parameters->numresolution)
				{
					spdlog::warn("POC end resolution {} cannot be greater than"
								 "the number of resolutions {}",
								 progression[numProgressions].resE, parameters->numresolution);
					progression[numProgressions].resE = parameters->numresolution;
				}
				if(progression[numProgressions].resS >= progression[numProgressions].resE)
				{
					spdlog::error(
						"POC beginning resolution must be strictly less than end resolution");
					return 1;
				}
				if(progression[numProgressions].compS >= progression[numProgressions].compE)
				{
					spdlog::error(
						"POC beginning component must be strictly less than end component");
					return 1;
				}
				numProgressions++;
				while(*s && *s != '/')
					s++;
				if(!*s)
					break;
				s++;
			}
			if(numProgressions <= 1)
			{
				spdlog::error("POC argument must have at least two progressions");
				return 1;
			}
			parameters->numpocs = numProgressions - 1;
		}
		else if(progressionOrderArg.isSet())
		{
			bool recognized = false;
			if(progressionOrderArg.getValue().length() == 4)
			{
				char progression[5];
				progression[4] = 0;
				strncpy(progression, progressionOrderArg.getValue().c_str(), 4);
				parameters->prog_order = getProgression(progression);
				recognized = parameters->prog_order != -1;
			}
			if(!recognized)
			{
				spdlog::error("Unrecognized progression order {} is not one of "
							  "[LRCP, RLCP, RPCL, PCRL, CPRL]",
							  progressionOrderArg.getValue());
				return 1;
			}
		}

		if(sopArg.isSet())
			parameters->csty |= 0x02;

		if(ephArg.isSet())
			parameters->csty |= 0x04;

		if(irreversibleArg.isSet())
			parameters->irreversible = true;

		if(pluginPathArg.isSet())
		{
			if(pluginPath)
				strcpy(pluginPath, pluginPathArg.getValue().c_str());
		}

		inputFolder->set_imgdir = false;
		if(inDirArg.isSet())
		{
			inputFolder->imgdirpath = (char*)malloc(strlen(inDirArg.getValue().c_str()) + 1);
			strcpy(inputFolder->imgdirpath, inDirArg.getValue().c_str());
			inputFolder->set_imgdir = true;
		}
		if(outFolder)
		{
			outFolder->set_imgdir = false;
			if(outDirArg.isSet())
			{
				outFolder->imgdirpath = (char*)malloc(strlen(outDirArg.getValue().c_str()) + 1);
				strcpy(outFolder->imgdirpath, outDirArg.getValue().c_str());
				outFolder->set_imgdir = true;
			}
		}
		if(guardBits.isSet())
		{
			if(guardBits.getValue() > 7)
			{
				spdlog::error("Number of guard bits {} is greater than 7", guardBits.getValue());
				return 1;
			}
			parameters->numgbits = (uint8_t)guardBits.getValue();
		}
		// profiles
		if(!isHT)
		{
			if(cinema2KArg.isSet())
			{
				if(!validateCinema(&cinema2KArg, GRK_PROFILE_CINEMA_2K, parameters))
					return 1;
				parameters->writeTLM = true;
				spdlog::warn("CINEMA 2K profile activated");
				spdlog::warn("Other options specified may be overridden\n");
			}
			else if(cinema4KArg.isSet())
			{
				if(!validateCinema(&cinema4KArg, GRK_PROFILE_CINEMA_4K, parameters))
				{
					return 1;
				}
				spdlog::warn(" CINEMA 4K profile activated\n"
							 "Other options specified may be overridden");
				parameters->writeTLM = true;
			}
			else if(BroadcastArg.isSet())
			{
				int mainlevel = 0;
				int profile = 0;
				int framerate = 0;
				const char* msg = "Wrong value for -broadcast. Should be "
								  "<PROFILE>[,mainlevel=X][,framerate=FPS] where <PROFILE> is one "
								  "of SINGLE/MULTI/MULTI_R.";
				char* arg = (char*)BroadcastArg.getValue().c_str();
				char* comma;

				comma = strstr(arg, ",mainlevel=");
				if(comma && sscanf(comma + 1, "mainlevel=%u", &mainlevel) != 1)
				{
					spdlog::error("{}", msg);
					return 1;
				}
				comma = strstr(arg, ",framerate=");
				if(comma && sscanf(comma + 1, "framerate=%u", &framerate) != 1)
				{
					spdlog::error("{}", msg);
					return 1;
				}

				comma = strchr(arg, ',');
				if(comma != nullptr)
				{
					*comma = 0;
				}

				if(strcmp(arg, "SINGLE") == 0)
				{
					profile = GRK_PROFILE_BC_SINGLE;
				}
				else if(strcmp(arg, "MULTI") == 0)
				{
					profile = GRK_PROFILE_BC_MULTI;
				}
				else if(strcmp(arg, "MULTI_R") == 0)
				{
					profile = GRK_PROFILE_BC_MULTI_R;
				}
				else
				{
					spdlog::error("{}", msg);
					return 1;
				}

				if(!(mainlevel >= 0 && mainlevel <= 11))
				{
					/* Voluntarily rough validation. More fine grained done in library */
					spdlog::error("Invalid mainlevel value {}.", mainlevel);
					return 1;
				}
				parameters->rsiz = (uint16_t)(profile | mainlevel);
				spdlog::info(
					"Broadcast profile activated. Other options specified may be overridden");
				parameters->framerate = (uint16_t)framerate;
				if(framerate > 0)
				{
					const int limitMBitsSec[] = {0,
												 GRK_BROADCAST_LEVEL_1_MBITSSEC,
												 GRK_BROADCAST_LEVEL_2_MBITSSEC,
												 GRK_BROADCAST_LEVEL_3_MBITSSEC,
												 GRK_BROADCAST_LEVEL_4_MBITSSEC,
												 GRK_BROADCAST_LEVEL_5_MBITSSEC,
												 GRK_BROADCAST_LEVEL_6_MBITSSEC,
												 GRK_BROADCAST_LEVEL_7_MBITSSEC,
												 GRK_BROADCAST_LEVEL_8_MBITSSEC,
												 GRK_BROADCAST_LEVEL_9_MBITSSEC,
												 GRK_BROADCAST_LEVEL_10_MBITSSEC,
												 GRK_BROADCAST_LEVEL_11_MBITSSEC};
					parameters->max_cs_size =
						(uint64_t)(limitMBitsSec[mainlevel] * (1000.0 * 1000 / 8) / framerate);
					spdlog::info("Setting max code stream size to {} bytes.",
								 parameters->max_cs_size);
					parameters->writeTLM = true;
				}
			}
			if(IMFArg.isSet())
			{
				int mainlevel = 0;
				int sublevel = 0;
				int profile = 0;
				int framerate = 0;
				const char* msg =
					"Wrong value for -IMF. Should be "
					"<PROFILE>[,mainlevel=X][,sublevel=Y][,framerate=FPS] where <PROFILE> is one "
					"of 2K/4K/8K/2K_R/4K_R/8K_R.";
				char* arg = (char*)IMFArg.getValue().c_str();
				char* comma;

				comma = strstr(arg, ",mainlevel=");
				if(comma && sscanf(comma + 1, "mainlevel=%u", &mainlevel) != 1)
				{
					spdlog::error("{}", msg);
					return 1;
				}

				comma = strstr(arg, ",sublevel=");
				if(comma && sscanf(comma + 1, "sublevel=%u", &sublevel) != 1)
				{
					spdlog::error("{}", msg);
					return 1;
				}

				comma = strstr(arg, ",framerate=");
				if(comma && sscanf(comma + 1, "framerate=%u", &framerate) != 1)
				{
					spdlog::error("{}", msg);
					return 1;
				}

				comma = strchr(arg, ',');
				if(comma != nullptr)
				{
					*comma = 0;
				}

				if(strcmp(arg, "2K") == 0)
				{
					profile = GRK_PROFILE_IMF_2K;
				}
				else if(strcmp(arg, "4K") == 0)
				{
					profile = GRK_PROFILE_IMF_4K;
				}
				else if(strcmp(arg, "8K") == 0)
				{
					profile = GRK_PROFILE_IMF_8K;
				}
				else if(strcmp(arg, "2K_R") == 0)
				{
					profile = GRK_PROFILE_IMF_2K_R;
				}
				else if(strcmp(arg, "4K_R") == 0)
				{
					profile = GRK_PROFILE_IMF_4K_R;
				}
				else if(strcmp(arg, "8K_R") == 0)
				{
					profile = GRK_PROFILE_IMF_8K_R;
				}
				else
				{
					spdlog::error("{}", msg);
					return 1;
				}

				if(!(mainlevel >= 0 && mainlevel <= 11))
				{
					/* Voluntarily rough validation. More fine grained done in library */
					spdlog::error("Invalid main level {}.", mainlevel);
					return 1;
				}
				if(!(sublevel >= 0 && sublevel <= 9))
				{
					/* Voluntarily rough validation. More fine grained done in library */
					spdlog::error("Invalid sub-level {}.", sublevel);
					return 1;
				}
				parameters->rsiz = (uint16_t)(profile | (sublevel << 4) | mainlevel);
				spdlog::info("IMF profile activated. Other options specified may be overridden");

				parameters->framerate = (uint16_t)framerate;
				if(framerate > 0 && sublevel > 0 && sublevel <= 9)
				{
					const int limitMBitsSec[] = {0,
												 GRK_IMF_SUBLEVEL_1_MBITSSEC,
												 GRK_IMF_SUBLEVEL_2_MBITSSEC,
												 GRK_IMF_SUBLEVEL_3_MBITSSEC,
												 GRK_IMF_SUBLEVEL_4_MBITSSEC,
												 GRK_IMF_SUBLEVEL_5_MBITSSEC,
												 GRK_IMF_SUBLEVEL_6_MBITSSEC,
												 GRK_IMF_SUBLEVEL_7_MBITSSEC,
												 GRK_IMF_SUBLEVEL_8_MBITSSEC,
												 GRK_IMF_SUBLEVEL_9_MBITSSEC};
					parameters->max_cs_size =
						(uint64_t)(limitMBitsSec[sublevel] * (1000.0 * 1000 / 8) / framerate);
					spdlog::info("Setting max code stream size to {} bytes.",
								 parameters->max_cs_size);
				}
				parameters->writeTLM = true;
			}

			if(rsizArg.isSet())
			{
				if(cinema2KArg.isSet() || cinema4KArg.isSet())
				{
					grk::warningCallback("Cinema profile set - rsiz parameter ignored.", nullptr);
				}
				else if(IMFArg.isSet())
				{
					grk::warningCallback("IMF profile set - rsiz parameter ignored.", nullptr);
				}
				else
				{
					parameters->rsiz = rsizArg.getValue();
				}
			}
		}
		else
		{
			parameters->rsiz |= GRK_JPH_RSIZ_FLAG;
		}
		if(captureResArg.isSet())
		{
			if(sscanf(captureResArg.getValue().c_str(), "%lf,%lf", parameters->capture_resolution,
					  parameters->capture_resolution + 1) != 2)
			{
				spdlog::error("-Q 'capture resolution' argument error  [-Q X0,Y0]");
				return 1;
			}
			parameters->write_capture_resolution = true;
		}
		if(displayResArg.isSet())
		{
			if(sscanf(captureResArg.getValue().c_str(), "%lf,%lf", parameters->display_resolution,
					  parameters->display_resolution + 1) != 2)
			{
				spdlog::error("-D 'display resolution' argument error  [-D X0,Y0]");
				return 1;
			}
			parameters->write_display_resolution = true;
		}

		if(mctArg.isSet())
		{
			uint32_t mct_mode = mctArg.getValue();
			if(mct_mode > 2)
			{
				spdlog::error("Incorrect MCT value {}. Must be equal to 0, 1 or 2.", mct_mode);
				return 1;
			}
			parameters->mct = (uint8_t)mct_mode;
		}

		if(customMCTArg.isSet())
		{
			char* lFilename = (char*)customMCTArg.getValue().c_str();
			char* lMatrix = nullptr;
			char* lCurrentPtr = nullptr;
			float* lCurrentDoublePtr = nullptr;
			float* lSpace = nullptr;
			int* int_ptr = nullptr;
			int lNbComp = 0, lTotalComp, lMctComp, i2;
			size_t lStrLen, lStrFread;
			uint32_t rc = 1;

			/* Open file */
			FILE* lFile = fopen(lFilename, "r");
			if(!lFile)
				goto cleanup;

			/* Set size of file and read its content*/
			if(GRK_FSEEK(lFile, 0U, SEEK_END))
				goto cleanup;

			lStrLen = (size_t)GRK_FTELL(lFile);
			if(GRK_FSEEK(lFile, 0U, SEEK_SET))
				goto cleanup;

			lMatrix = (char*)malloc(lStrLen + 1);
			if(!lMatrix)
				goto cleanup;
			lStrFread = fread(lMatrix, 1, lStrLen, lFile);
			fclose(lFile);
			lFile = nullptr;
			if(lStrLen != lStrFread)
				goto cleanup;

			lMatrix[lStrLen] = 0;
			lCurrentPtr = lMatrix;

			/* replace ',' by 0 */
			while(*lCurrentPtr != 0)
			{
				if(*lCurrentPtr == ' ')
				{
					*lCurrentPtr = 0;
					++lNbComp;
				}
				++lCurrentPtr;
			}
			++lNbComp;
			lCurrentPtr = lMatrix;

			lNbComp = (int)(sqrt(4 * lNbComp + 1) / 2. - 0.5);
			lMctComp = lNbComp * lNbComp;
			lTotalComp = lMctComp + lNbComp;
			lSpace = (float*)malloc((size_t)lTotalComp * sizeof(float));
			if(lSpace == nullptr)
			{
				free(lMatrix);
				return 1;
			}
			lCurrentDoublePtr = lSpace;
			for(i2 = 0; i2 < lMctComp; ++i2)
			{
				lStrLen = strlen(lCurrentPtr) + 1;
				*lCurrentDoublePtr++ = (float)atof(lCurrentPtr);
				lCurrentPtr += lStrLen;
			}

			int_ptr = (int*)lCurrentDoublePtr;
			for(i2 = 0; i2 < lNbComp; ++i2)
			{
				lStrLen = strlen(lCurrentPtr) + 1;
				*int_ptr++ = atoi(lCurrentPtr);
				lCurrentPtr += lStrLen;
			}

			/* TODO should not be here ! */
			grk_set_MCT(parameters, lSpace, (int*)(lSpace + lMctComp), (uint32_t)lNbComp);

			rc = 0;
		cleanup:
			if(lFile)
				fclose(lFile);
			free(lSpace);
			free(lMatrix);
			if(rc)
				return false;
		}

		if(roiArg.isSet())
		{
			if(sscanf(roiArg.getValue().c_str(), "c=%u,U=%u", &parameters->roi_compno,
					  &parameters->roi_shift) != 2)
			{
				spdlog::error("ROI argument must be of the form: [-ROI c='compno',U='shift']");
				return 1;
			}
		}
		// Canvas coordinates
		if(tilesArg.isSet())
		{
			int32_t t_width = 0, t_height = 0;
			if(sscanf(tilesArg.getValue().c_str(), "%u,%u", &t_width, &t_height) == EOF)
			{
				spdlog::error("sscanf failed for tiles argument");
				return 1;
			}
			// sanity check on tile dimensions
			if(t_width <= 0 || t_height <= 0)
			{
				spdlog::error("Tile dimensions ({}, {}) must be "
							  "strictly positive",
							  t_width, t_height);
				return 1;
			}
			parameters->t_width = (uint32_t)t_width;
			parameters->t_height = (uint32_t)t_height;
			parameters->tile_size_on = true;
		}
		if(tileOffsetArg.isSet())
		{
			int32_t off1, off2;
			if(sscanf(tileOffsetArg.getValue().c_str(), "%u,%u", &off1, &off2) != 2)
			{
				spdlog::error("-T 'tile offset' argument must be in the form: -T X0,Y0");
				return 1;
			}
			if(off1 < 0 || off2 < 0)
			{
				spdlog::error("-T 'tile offset' values ({},{}) can't be negative", off1, off2);
				return 1;
			}
			parameters->tx0 = (uint32_t)off1;
			parameters->ty0 = (uint32_t)off2;
		}
		if(imageOffsetArg.isSet())
		{
			int32_t off1, off2;
			if(sscanf(imageOffsetArg.getValue().c_str(), "%u,%u", &off1, &off2) != 2)
			{
				spdlog::error("-d 'image offset' argument must be specified as:  -d x0,y0");
				return 1;
			}
			if(off1 < 0 || off2 < 0)
			{
				spdlog::error("-T 'image offset' values ({},{}) can't be negative", off1, off2);
				return 1;
			}
			parameters->image_offset_x0 = (uint32_t)off1;
			parameters->image_offset_y0 = (uint32_t)off2;
		}

		if(!imageOffsetArg.isSet() && tileOffsetArg.isSet())
		{
			parameters->image_offset_x0 = parameters->tx0;
			parameters->image_offset_y0 = parameters->ty0;
		}
		else
		{
			if(parameters->tx0 > parameters->image_offset_x0 ||
			   parameters->ty0 > parameters->image_offset_y0)
			{
				spdlog::error("Tile offset ({},{}) must be top left of "
							  "image offset ({},{})",
							  parameters->tx0, parameters->ty0, parameters->image_offset_x0,
							  parameters->image_offset_y0);
				return 1;
			}
			if(tilesArg.isSet())
			{
				auto tx1 = uint_adds(parameters->tx0, parameters->t_width); /* manage overflow */
				auto ty1 = uint_adds(parameters->ty0, parameters->t_height); /* manage overflow */
				if(tx1 <= parameters->image_offset_x0 || ty1 <= parameters->image_offset_y0)
				{
					spdlog::error(
						"Tile grid: first tile bottom, right hand corner\n"
						"({},{}) must lie to the right and bottom of"
						" image offset ({},{})\n so that the tile overlaps with the image area.",
						tx1, ty1, parameters->image_offset_x0, parameters->image_offset_y0);
					return 1;
				}
			}
		}
		if(commentArg.isSet())
		{
			std::istringstream f(commentArg.getValue());
			std::string s;
			while(getline(f, s, '|'))
			{
				if(s.empty())
					continue;
				if(s.length() > GRK_MAX_COMMENT_LENGTH)
				{
					spdlog::warn(
						" Comment length {} is greater than maximum comment length {}. Ignoring",
						(uint32_t)s.length(), GRK_MAX_COMMENT_LENGTH);
					continue;
				}
				size_t count = parameters->num_comments;
				if(count == GRK_NUM_COMMENTS_SUPPORTED)
				{
					spdlog::warn(
						" Grok compressor is limited to {} comments. Ignoring subsequent comments.",
						GRK_NUM_COMMENTS_SUPPORTED);
					break;
				}
				// ISO Latin comment
				parameters->is_binary_comment[count] = false;
				parameters->comment[count] = (char*)new uint8_t[s.length()];
				memcpy(parameters->comment[count], s.c_str(), s.length());
				parameters->comment_len[count] = (uint16_t)s.length();
				parameters->num_comments++;
			}
		}
		if(tpArg.isSet())
		{
			parameters->newTilePartProgressionDivider = tpArg.getValue();
			parameters->enableTilePartGeneration = true;
		}
	}
	catch(TCLAP::ArgException& e) // catch any exceptions
	{
		std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
		return 1;
	}
	if(inputFolder->set_imgdir)
	{
		if(!(parameters->infile[0] == 0))
		{
			spdlog::error("options -in_dir and -in_file cannot be used together ");
			return 1;
		}
		if(!inputFolder->set_out_format)
		{
			spdlog::error("When -in_dir is used, -out_fmt <FORMAT> must be used ");
			spdlog::error("Only one format allowed! Valid formats are j2k and jp2");
			return 1;
		}
		if(!((parameters->outfile[0] == 0)))
		{
			spdlog::error("options -in_dir and -out_file cannot be used together ");
			spdlog::error("Specify OutputFormat using -out_fmt<FORMAT> ");
			return 1;
		}
	}
	else
	{
		if(parameters->cod_format == GRK_FMT_UNK)
		{
			if(parameters->infile[0] == 0)
			{
				spdlog::error("Missing input file parameter\n"
							  "Example: {} -i image.pgm -o image.j2k",
							  argv[0]);
				spdlog::error("   Help: {} -h", argv[0]);
				return 1;
			}
		}

		if(parameters->outfile[0] == 0)
		{
			spdlog::error("Missing output file parameter\n"
						  "Example: {} -i image.pgm -o image.j2k",
						  argv[0]);
			spdlog::error("   Help: {} -h", argv[0]);
			return 1;
		}
	}
	if((parameters->decod_format == GRK_FMT_RAW && parameters->raw_cp.width == 0) ||
	   (parameters->decod_format == GRK_FMT_RAWL && parameters->raw_cp.width == 0))
	{
		spdlog::error("invalid raw image parameters");
		spdlog::error("Please use the Format option -F:");
		spdlog::error("-F rawWidth,rawHeight,rawComp,rawBitDepth,s/u (Signed/Unsigned)");
		spdlog::error("Example: -i lena.raw -o lena.j2k -F 512,512,3,8,u");
		return 1;
	}
	if((parameters->tx0 > 0 && parameters->tx0 > parameters->image_offset_x0) ||
	   (parameters->ty0 > 0 && parameters->ty0 > parameters->image_offset_y0))
	{
		spdlog::error("Tile offset cannot be greater than image offset : TX0({})<=IMG_X0({}) "
					  "TYO({})<=IMG_Y0({}) ",
					  parameters->tx0, parameters->image_offset_x0, parameters->ty0,
					  parameters->image_offset_y0);
		return 1;
	}
	for(uint32_t i = 0; i < parameters->numpocs; i++)
	{
		if(parameters->progression[i].progression == -1)
		{
			spdlog::error("Unrecognized progression order in option -P (POC n {}) [LRCP, RLCP, "
						  "RPCL, PCRL, CPRL] ",
						  i + 1);
		}
	}
	/* If subsampled image is provided, automatically disable MCT */
	if(((parameters->decod_format == GRK_FMT_RAW) || (parameters->decod_format == GRK_FMT_RAWL)) &&
	   (((parameters->raw_cp.numcomps > 1) &&
		 ((parameters->raw_cp.comps[1].dx > 1) || (parameters->raw_cp.comps[1].dy > 1))) ||
		((parameters->raw_cp.numcomps > 2) &&
		 ((parameters->raw_cp.comps[2].dx > 1) || (parameters->raw_cp.comps[2].dy > 1)))))
	{
		parameters->mct = 0;
	}
	if(parameters->mct == 2 && !parameters->mct_data)
	{
		spdlog::error("Custom MCT has been set but no array-based MCT has been provided.");
		return false;
	}

	return 0;
}

static bool pluginCompressCallback(grk_plugin_compress_user_callback_info* info)
{
	auto parameters = info->compressor_parameters;
	bool bSuccess = true;
	grk_stream* stream = nullptr;
	grk_codec* codec = nullptr;
	grk_image* image = info->image;
	char outfile[3 * GRK_PATH_LEN];
	char temp_ofname[GRK_PATH_LEN];
	bool createdImage = false;
	bool inMemoryCompression = false;

	// get output file
	outfile[0] = 0;
	if(info->output_file_name && info->output_file_name[0])
	{
		if(info->outputFileNameIsRelative)
		{
			strcpy(temp_ofname, get_file_name((char*)info->output_file_name));
			if(img_fol_plugin.set_out_format)
			{
				sprintf(outfile, "%s%s%s.%s",
						out_fol_plugin.imgdirpath ? out_fol_plugin.imgdirpath
												  : img_fol_plugin.imgdirpath,
						grk::pathSeparator(), temp_ofname, img_fol_plugin.out_format);
			}
		}
		else
		{
			strcpy(outfile, info->output_file_name);
		}
	}
	else
	{
		bSuccess = false;
		goto cleanup;
	}

	if(!image)
	{
		if(parameters->decod_format == GRK_FMT_UNK)
		{
			int fmt = grk_get_file_format((char*)info->input_file_name);
			if(fmt <= GRK_FMT_UNK)
			{
				bSuccess = false;
				goto cleanup;
			}
			parameters->decod_format = (GRK_SUPPORTED_FILE_FMT)fmt;
			if(!isDecodedFormatSupported(parameters->decod_format))
			{
				bSuccess = false;
				goto cleanup;
			}
		}
		/* decode the source image */
		/* ----------------------- */

		switch(info->compressor_parameters->decod_format)
		{
			case GRK_FMT_PGX: {
				PGXFormat pgx;
				image = pgx.decode(info->input_file_name, info->compressor_parameters);
				if(!image)
				{
					spdlog::error("Unable to load pgx file");
					bSuccess = false;
					goto cleanup;
				}
			}
			break;

			case GRK_FMT_PXM: {
				PNMFormat pnm(false);
				image = pnm.decode(info->input_file_name, info->compressor_parameters);
				if(!image)
				{
					spdlog::error("Unable to load pnm file");
					bSuccess = false;
					goto cleanup;
				}
			}
			break;

			case GRK_FMT_BMP: {
				BMPFormat bmp;
				image = bmp.decode(info->input_file_name, info->compressor_parameters);
				if(!image)
				{
					spdlog::error("Unable to load bmp file");
					bSuccess = false;
					goto cleanup;
				}
			}
			break;

#ifdef GROK_HAVE_LIBTIFF
			case GRK_FMT_TIF: {
				TIFFFormat tif;
				image = tif.decode(info->input_file_name, info->compressor_parameters);
				if(!image)
				{
					bSuccess = false;
					goto cleanup;
				}
			}
			break;
#endif /* GROK_HAVE_LIBTIFF */

			case GRK_FMT_RAW: {
				RAWFormat raw(true);
				image = raw.decode(info->input_file_name, info->compressor_parameters);
				if(!image)
				{
					spdlog::error("Unable to load raw file");
					bSuccess = false;
					goto cleanup;
				}
			}
			break;

			case GRK_FMT_RAWL: {
				RAWFormat raw(false);
				image = raw.decode(info->input_file_name, info->compressor_parameters);
				if(!image)
				{
					spdlog::error("Unable to load raw file");
					bSuccess = false;
					goto cleanup;
				}
			}
			break;

#ifdef GROK_HAVE_LIBPNG
			case GRK_FMT_PNG: {
				PNGFormat png;
				image = png.decode(info->input_file_name, info->compressor_parameters);
				if(!image)
				{
					spdlog::error("Unable to load png file");
					bSuccess = false;
					goto cleanup;
				}
			}
			break;
#endif /* GROK_HAVE_LIBPNG */

#ifdef GROK_HAVE_LIBJPEG
			case GRK_FMT_JPG: {
				JPEGFormat jpeg;
				image = jpeg.decode(info->input_file_name, info->compressor_parameters);
				if(!image)
				{
					spdlog::error("Unable to load jpeg file");
					bSuccess = false;
					goto cleanup;
				}
			}
			break;
#endif /* GROK_HAVE_LIBPNG */
			default: {
				spdlog::error("Input file format {} is not supported",
							  convertFileFmtToString(info->compressor_parameters->decod_format));
				bSuccess = false;
				goto cleanup;
			}
			break;
		}

		/* Can happen if input file is TIFF or PNG
		 * and GROK_HAVE_LIBTIF or GROK_HAVE_LIBPNG is undefined
		 */
		if(!image)
		{
			spdlog::error("Unable to load file: no image generated.");
			bSuccess = false;
			goto cleanup;
		}
		createdImage = true;
	}

	if(inMemoryCompression)
	{
		auto fp = fopen(info->input_file_name, "rb");
		if(!fp)
		{
			spdlog::error("grk_compress: unable to open file {} for reading",
						  info->input_file_name);
			bSuccess = false;
			goto cleanup;
		}

		auto rc = GRK_FSEEK(fp, 0U, SEEK_END);
		if(rc == -1)
		{
			spdlog::error("grk_compress: unable to seek on file {}", info->input_file_name);
			fclose(fp);
			bSuccess = false;
			goto cleanup;
		}
		auto fileLength = GRK_FTELL(fp);
		if(fileLength == -1)
		{
			spdlog::error("grk_compress: unable to ftell on file {}", info->input_file_name);
			fclose(fp);
			bSuccess = false;
			goto cleanup;
		}
		fclose(fp);

		if(fileLength)
		{
			//  option to write to buffer, assuming one knows how large compressed stream will be
			uint64_t imageSize =
				(((uint64_t)(image->x1 - image->x0) * (uint64_t)(image->y1 - image->y0) *
				  image->numcomps * ((image->comps[0].prec + 7U) / 8U)) *
				 3U) /
				2U;
			info->compressBufferLen =
				(size_t)fileLength > imageSize ? (size_t)fileLength : imageSize;
			info->compressBuffer = new uint8_t[info->compressBufferLen];
		}
	}

	// limit to 16 bit precision
	for(uint32_t i = 0; i < image->numcomps; ++i)
	{
		if(image->comps[i].prec > GRK_MAX_SUPPORTED_IMAGE_PRECISION)
		{
			spdlog::error("precision = {} not supported:", image->comps[i].prec);
			bSuccess = false;
			goto cleanup;
		}
	}

	/* Decide if MCT should be used */
	if(parameters->mct == 255)
	{ /* mct mode has not been set in commandline */
		parameters->mct = (image->numcomps >= 3) ? 1 : 0;
	}
	else
	{ /* mct mode has been set in commandline */
		if((parameters->mct == 1) && (image->numcomps < 3))
		{
			spdlog::error("RGB->YCC conversion cannot be used:");
			spdlog::error("Input image has less than 3 components");
			bSuccess = false;
			goto cleanup;
		}
		if((parameters->mct == 2) && (!parameters->mct_data))
		{
			spdlog::error("Custom MCT has been set but no array-based MCT");
			spdlog::error("has been provided.");
			bSuccess = false;
			goto cleanup;
		}
	}

	if((GRK_IS_BROADCAST(parameters->rsiz) || GRK_IS_IMF(parameters->rsiz)) &&
	   parameters->framerate != 0)
	{
		uint32_t avgcomponents = image->numcomps;
		if(image->numcomps == 3 && image->comps[1].dx == 2 && image->comps[1].dy == 2)
		{
			avgcomponents = 2;
		}
		double msamplespersec =
			(double)image->x1 * image->y1 * avgcomponents * parameters->framerate / 1e6;
		uint32_t limit = 0;
		const uint32_t level = GRK_GET_LEVEL(parameters->rsiz);
		if(level > 0U && level <= GRK_LEVEL_MAX)
		{
			if(GRK_IS_BROADCAST(parameters->rsiz))
			{
				const uint32_t limitMSamplesSec[] = {0,
													 GRK_BROADCAST_LEVEL_1_MSAMPLESSEC,
													 GRK_BROADCAST_LEVEL_2_MSAMPLESSEC,
													 GRK_BROADCAST_LEVEL_3_MSAMPLESSEC,
													 GRK_BROADCAST_LEVEL_4_MSAMPLESSEC,
													 GRK_BROADCAST_LEVEL_5_MSAMPLESSEC,
													 GRK_BROADCAST_LEVEL_6_MSAMPLESSEC,
													 GRK_BROADCAST_LEVEL_7_MSAMPLESSEC,
													 GRK_BROADCAST_LEVEL_8_MSAMPLESSEC,
													 GRK_BROADCAST_LEVEL_9_MSAMPLESSEC,
													 GRK_BROADCAST_LEVEL_10_MSAMPLESSEC,
													 GRK_BROADCAST_LEVEL_11_MSAMPLESSEC};
				limit = limitMSamplesSec[level];
			}
			else if(GRK_IS_IMF(parameters->rsiz))
			{
				const uint32_t limitMSamplesSec[] = {0,
													 GRK_IMF_MAINLEVEL_1_MSAMPLESSEC,
													 GRK_IMF_MAINLEVEL_2_MSAMPLESSEC,
													 GRK_IMF_MAINLEVEL_3_MSAMPLESSEC,
													 GRK_IMF_MAINLEVEL_4_MSAMPLESSEC,
													 GRK_IMF_MAINLEVEL_5_MSAMPLESSEC,
													 GRK_IMF_MAINLEVEL_6_MSAMPLESSEC,
													 GRK_IMF_MAINLEVEL_7_MSAMPLESSEC,
													 GRK_IMF_MAINLEVEL_8_MSAMPLESSEC,
													 GRK_IMF_MAINLEVEL_9_MSAMPLESSEC,
													 GRK_IMF_MAINLEVEL_10_MSAMPLESSEC,
													 GRK_IMF_MAINLEVEL_11_MSAMPLESSEC};
				limit = limitMSamplesSec[level];
			}
		}
		if(msamplespersec > limit)
			spdlog::warn("MSamples/sec is {}, whereas limit is {}.", msamplespersec, limit);
	}

	if(info->compressBuffer)
	{
		// let stream clean up compress buffer
		stream = grk_stream_create_mem_stream(info->compressBuffer, info->compressBufferLen, true,
											  false);
	}
	else
	{
		stream = grk_stream_create_file_stream(outfile, 1024 * 1024, false);
		// stream = grk_stream_create_mapped_file_stream(outfile, false);
	}
	if(!stream)
	{
		spdlog::error("failed to create stream");
		bSuccess = false;
		goto cleanup;
	}

	switch(parameters->cod_format)
	{
		case GRK_FMT_J2K: /* JPEG 2000 code stream */
			codec = grk_compress_create(GRK_CODEC_J2K, stream);
			break;
		case GRK_FMT_JP2: /* JPEG 2000 compressed image data */
			codec = grk_compress_create(GRK_CODEC_JP2, stream);
			break;
		default:
			bSuccess = false;
			goto cleanup;
	}
	grk_set_msg_handlers(parameters->verbose ? infoCallback : nullptr, nullptr,
						 parameters->verbose ? warningCallback : nullptr, nullptr, errorCallback,
						 nullptr);
	if(!grk_compress_init(codec, parameters, image))
	{
		spdlog::error("failed to compress image: grk_compress_init");
		bSuccess = false;
		goto cleanup;
	}

	/* compress the image */
	bSuccess = grk_compress_start(codec);
	if(!bSuccess)
	{
		spdlog::error("failed to compress image: grk_compress_start");
		bSuccess = false;
		goto cleanup;
	}

	bSuccess = grk_compress_with_plugin(codec, info->tile);
	if(!bSuccess)
	{
		spdlog::error("failed to compress image: grk_compress");
		bSuccess = false;
		goto cleanup;
	}

	bSuccess = bSuccess && grk_compress_end(codec);
	if(!bSuccess)
	{
		spdlog::error("failed to compress image: grk_compress_end");
		bSuccess = false;
		goto cleanup;
	}
#ifdef GROK_HAVE_EXIFTOOL
	if(bSuccess && info->transferExifTags && info->compressor_parameters->cod_format == GRK_FMT_JP2)
		transferExifTags(info->input_file_name, info->output_file_name);
#endif
	if(info->compressBuffer)
	{
		auto fp = fopen(outfile, "wb");
		if(!fp)
		{
			spdlog::error("Buffer compress: failed to open file {} for writing", outfile);
		}
		else
		{
			auto len = grk_stream_get_write_mem_stream_length(stream);
			size_t written = fwrite(info->compressBuffer, 1, len, fp);
			if(written != len)
			{
				spdlog::error("Buffer compress: only {} bytes written out of {} total", len,
							  written);
				bSuccess = false;
			}
			fclose(fp);
		}
	}
cleanup:
	if(stream)
		grk_object_unref(stream);
	grk_object_unref(codec);
	if(createdImage)
		grk_object_unref(&image->obj);
	if(!bSuccess)
	{
		spdlog::error("failed to compress image");
		if(parameters->outfile[0])
		{
			bool allocated = false;
			char* p = actual_path(parameters->outfile, &allocated);
			GRK_UNUSED(remove)(p);
			if(allocated)
				free(p);
		}
	}
	return bSuccess;
}

// returns 0 if failed, 1 if succeeded,
// and 2 if file is not suitable for compression
int GrkCompress::compress(const std::string& inputFile, CompressInitParams* initParams)
{
	// clear for next file compress
	initParams->parameters.write_capture_resolution_from_file = false;
	// don't reset format if reading from STDIN
	if(initParams->parameters.infile[0])
		initParams->parameters.decod_format = GRK_FMT_UNK;
	if(initParams->inputFolder.set_imgdir)
	{
		if(nextFile(inputFile, &initParams->inputFolder,
					initParams->outFolder.set_imgdir ? &initParams->outFolder
													 : &initParams->inputFolder,
					&initParams->parameters))
		{
			return 2;
		}
	}
	grk_plugin_compress_user_callback_info callbackInfo;
	memset(&callbackInfo, 0, sizeof(grk_plugin_compress_user_callback_info));
	callbackInfo.compressor_parameters = &initParams->parameters;
	callbackInfo.image = nullptr;
	callbackInfo.output_file_name = initParams->parameters.outfile;
	callbackInfo.input_file_name = initParams->parameters.infile;
	callbackInfo.transferExifTags = initParams->transferExifTags;

	return pluginCompressCallback(&callbackInfo) ? 1 : 0;
}

} // namespace grk
