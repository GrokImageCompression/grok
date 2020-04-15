/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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
 *
 *    This source code incorporates work covered by the following copyright and
 *    permission notice:
 *
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2003-2007, Francois-Olivier Devaux
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2006-2007, Parvatha Elangovan
 * Copyright (c) 2008, Jerome Fimes, Communications & Systemes <jerome.fimes@c-s.fr>
 * Copyright (c) 2011-2012, Centre National d'Etudes Spatiales (CNES), France
 * Copyright (c) 2012, CS Systemes d'Information, France
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef _WIN32
#include "../common/windirent.h"
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <dirent.h>
#include <strings.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif /* _WIN32 */

#include "common.h"
using namespace grk;

#include "grk_apps_config.h"
#include "grok.h"
#include "RAWFormat.h"
#include "PNMFormat.h"
#include "PGXFormat.h"
#include "TGAFormat.h"
#include "BMPFormat.h"
#include "JPEGFormat.h"
#include "TIFFFormat.h"
#include "PNGFormat.h"
#include "convert.h"
#include "grok_string.h"
#include "color.h"

#include <float.h>
#include <math.h>
#include <assert.h>
#include <limits.h>
#include <cstddef>
#include <cstdlib>
#include <sstream>
#define TCLAP_NAMESTARTSTRING "-"
#include "tclap/CmdLine.h"
#include <chrono>  // for high_resolution_clock

using namespace TCLAP;
using namespace std;

static bool plugin_compress_callback(
		grk_plugin_encode_user_callback_info *info);

void exit_func() {
	grk_plugin_stop_batch_encode();
}

#ifdef  _WIN32
BOOL sig_handler(DWORD signum){
	switch (signum)	{
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		exit_func();
		return(TRUE);

	default:
		return FALSE;
	}
}
#else
void sig_handler(int signum) {
	(void) signum;
	exit_func();
}
#endif 

void setup_signal_handler() {
#ifdef  _WIN32
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)sig_handler, TRUE);
#else
	struct sigaction sa;
	sa.sa_handler = &sig_handler;
	sigfillset(&sa.sa_mask);
	sigaction(SIGHUP, &sa, NULL);
#endif  
}

/**
 sample error debug callback expecting no client object
 */
static void error_callback(const char *msg, void *client_data) {
	(void) client_data;
	spdlog::default_logger()->error(msg);
}
/**
 sample warning debug callback expecting no client object
 */
static void warning_callback(const char *msg, void *client_data) {
	(void) client_data;
	spdlog::default_logger()->warn(msg);
}
/**
 sample debug callback expecting no client object
 */
static void info_callback(const char *msg, void *client_data) {
	(void) client_data;
	spdlog::default_logger()->info(msg);
}

static void encode_help_display(void) {
	fprintf(stdout,
			"\nThis is the grk_compress utility from the Grok project.\n"
					"It compresses various image formats with the JPEG 2000 algorithm.\n"
					"It has been compiled against Grok library v%s.\n\n",
			grk_version());

	fprintf(stdout, "Default encoding options:\n");
	fprintf(stdout, "-------------------------\n");
	fprintf(stdout, "\n");
	fprintf(stdout, " * Lossless\n");
	fprintf(stdout, " * 1 tile\n");
	fprintf(stdout,
			" * RGB->YCC conversion if there are 3 colour components\n");
	fprintf(stdout, " * Size of precinct : 2^15 x 2^15 (i.e. 1 precinct)\n");
	fprintf(stdout, " * Size of code-block : 64 x 64\n");
	fprintf(stdout, " * Number of resolutions: 6\n");
	fprintf(stdout, " * No SOP marker in the codestream\n");
	fprintf(stdout, " * No EPH marker in the codestream\n");
	fprintf(stdout, " * No sub-sampling in x or y direction\n");
	fprintf(stdout, " * No mode switch activated\n");
	fprintf(stdout, " * Progression order: LRCP\n");
	fprintf(stdout, " * No ROI upshifted\n");
	fprintf(stdout, " * No offset of the origin of the image\n");
	fprintf(stdout, " * No offset of the origin of the tiles\n");
	fprintf(stdout, " * Reversible DWT 5-3\n");
	fprintf(stdout, "\n");

	fprintf(stdout, "Note:\n");
	fprintf(stdout, "-----\n");
	fprintf(stdout, "\n");
	fprintf(stdout,
			"The markers written to the main_header are : SOC SIZ COD QCD COM.\n");
	fprintf(stdout, "COD and QCD never appear in the tile_header.\n");
	fprintf(stdout, "\n");

	fprintf(stdout, "Parameters:\n");
	fprintf(stdout, "-----------\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "Required Parameters (except with -h):\n");
	fprintf(stdout, "One of the two options -ImgDir or -i must be used\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "[-i|-InputFile] <file>\n");
	fprintf(stdout, "    Input file\n");
	fprintf(stdout,
			"    Known extensions are <PBM|PGM|PPM|PNM|PAM|PGX|PNG|BMP|TIF|RAW|RAWL|TGA>\n");
	fprintf(stdout, "    If used, '-o <file>' must be provided\n");
	fprintf(stdout, "[-o|-OutputFile] <compressed file>\n");
	fprintf(stdout, "    Output file (accepted extensions are j2k or jp2).\n");
	fprintf(stdout, "[-y|-ImgDir] <dir>\n");
	fprintf(stdout, "    Image file Directory path (example ../Images) \n");
	fprintf(stdout, "    When using this option -OutFor must be used\n");
	fprintf(stdout, "[-O|-OutFor] <J2K|J2C|JP2>\n");
	fprintf(stdout, "    Output format for compressed files.\n");
	fprintf(stdout, "    Required only if -ImgDir is used\n");
	fprintf(stdout,
			"[-K|-InFor] <pbm|pgm|ppm|pnm|pam|pgx|png|bmp|tif|raw|rawl|tga>\n");
	fprintf(stdout, "    Input format. Will override file tag.\n");
	fprintf(stdout,
			"[-F|-Raw] <width>,<height>,<ncomp>,<bitdepth>,{s,u}@<dx1>x<dy1>:...:<dxn>x<dyn>\n");
	fprintf(stdout, "    Characteristics of the raw input image\n");
	fprintf(stdout,
			"    If subsampling is omitted, 1x1 is assumed for all components\n");
	fprintf(stdout, "      Example: -F 512,512,3,8,u@1x1:2x2:2x2\n");
	fprintf(stdout,
			"               for raw 512x512 image with 4:2:0 subsampling\n");
	fprintf(stdout,
			"    Required only if RAW or RAWL input file is provided.\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "Optional Parameters:\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "[-h|-help]\n");
	fprintf(stdout, "    Display the help information.\n");
	fprintf(stdout, "[-a|-OutDir] <output directory>\n");
	fprintf(stdout,
			"    Output directory where compressed files are stored.\n");
	fprintf(stdout,
			"[-r|-CompressionRatios] <compression ratio>,<compression ratio>,...\n");
	fprintf(stdout,
			"    Different compression ratios for successive layers.\n");
	fprintf(stdout,
			"    The rate specified for each quality level is the desired\n");
	fprintf(stdout, "    compression factor.\n");
	fprintf(stdout, "    Decreasing ratios required.\n");
	fprintf(stdout, "      Example: -r 20,10,1 means \n");
	fprintf(stdout, "            quality layer 1: compress 20x, \n");
	fprintf(stdout, "            quality layer 2: compress 10x \n");
	fprintf(stdout, "            quality layer 3: compress lossless\n");
	fprintf(stdout, "    Options -r and -q cannot be used together.\n");
	fprintf(stdout,
			"[-q|-Quality] <psnr value>,<psnr value>,<psnr value>,...\n");
	fprintf(stdout,
			"    Different psnr for successive layers (-q 30,40,50).\n");
	fprintf(stdout, "    Increasing PSNR values required.\n");
	fprintf(stdout, "    Options -r and -q cannot be used together.\n");
	fprintf(stdout, "[-A|-RateControlAlgorithm] <0|1>\n");
	fprintf(stdout, "    Select algorithm used for rate control\n");
	fprintf(stdout,
			"    0: Bisection search for optimal threshold using all code passes in code blocks. (default) (slightly higher PSRN than algorithm 1)\n");
	fprintf(stdout,
			"    1: Bisection search for optimal threshold using only feasible truncation points, on convex hull.\n");
	fprintf(stdout, "[-n|-Resolutions] <number of resolutions>\n");
	fprintf(stdout, "    Number of resolutions.\n");
	fprintf(stdout,
			"    It corresponds to the number of DWT decompositions +1. \n");
	fprintf(stdout, "    Default: 6.\n");
	fprintf(stdout, "[-b|-CodeBlockDim] <cblk width>,<cblk height>\n");
	fprintf(stdout,
			"    Code-block dimensions. The dimensions must respect the constraint \n");
	fprintf(stdout,
			"    defined in the JPEG-2000 standard (no dimension smaller than 4 \n");
	fprintf(stdout,
			"    or greater than 1024, no code-block with more than 4096 coefficients).\n");
	fprintf(stdout, "    The maximum value permitted is 64x64. \n");
	fprintf(stdout, "    Default: 64x64.\n");
	fprintf(stdout,
			"[-c|-PrecinctDims] [<prec width>,<prec height>],[<prec width>,<prec height>],...\n");
	fprintf(stdout,
			"    Precinct dimensions. Dimensions specified must be powers of 2. \n");
	fprintf(stdout,
			"    Multiple records may be specified, in which case the first record refers \n");
	fprintf(stdout,
			"    to the highest resolution level and subsequent records refer to lower \n");
	fprintf(stdout,
			"    resolution levels. The last specified record's dimensions are progressively right-shifted (halved in size) \n");
	fprintf(stdout, "    for each remaining lower resolution level.\n");
	fprintf(stdout,
			"    Default: 2^15x2^15 at each resolution i.e. precincts are not used.\n");
	fprintf(stdout, "[-t|-TileDim] <tile width>,<tile height>\n");
	fprintf(stdout, "    Tile dimensions.\n");
	fprintf(stdout,
			"    Default: the dimension of the whole image, thus only one tile.\n");
	fprintf(stdout, "[-p|-ProgressionOrder] <LRCP|RLCP|RPCL|PCRL|CPRL>\n");
	fprintf(stdout, "    Progression order.\n");
	fprintf(stdout, "    Default: LRCP.\n");
	/*
	 fprintf(stdout,"[-s|-Subsampling]  <subX,subY>\n");
	 fprintf(stdout,"    Subsampling factor.\n");
	 fprintf(stdout,"    Subsampling bigger than 2 can produce error\n");
	 fprintf(stdout,"    Default: no subsampling.\n");
	 */
	fprintf(stdout,
			"[-P|-POC] <progression order change>/<progression order change>/...\n");
	fprintf(stdout, "    Progression order change.\n");
	fprintf(stdout,
			"    The syntax of a progression order change is the following:\n");
	fprintf(stdout,
			"    T<tile>=<resStart>,<compStart>,<layerEnd>,<resEnd>,<compEnd>,<progOrder>\n");
	fprintf(stdout,
			"      Example: -POC T1=0,0,1,5,3,CPRL/T1=5,0,1,6,3,CPRL\n");
	fprintf(stdout, "[-S|-SOP]\n");
	fprintf(stdout, "    Write SOP marker before each packet.\n");
	fprintf(stdout, "[-E|-EPH]\n");
	fprintf(stdout, "    Write EPH marker after each header packet.\n");
	fprintf(stdout, "[-M|-Mode] <key value>\n");
	fprintf(stdout, "    Mode switch.\n");
	fprintf(stdout, "    [1=BYPASS(LAZY) 2=RESET 4=RESTART(TERMALL)\n");
	fprintf(stdout, "    8=VSC 16=ERTERM(SEGTERM) 32=SEGMARK(SEGSYM)]\n");
	fprintf(stdout, "    64=HT]\n");
	fprintf(stdout, "    Indicate multiple modes by adding their values.\n");
	fprintf(stdout,
			"      Example: RESTART(4) + RESET(2) + SEGMARK(32) => -M 38\n");
	fprintf(stdout,
			"      Note: HT(64) for High Throughput cannot be combined with other flags\n");
	fprintf(stdout, "[-u|-TP] <R|L|C>\n");
	fprintf(stdout, "    Divide packets of every tile into tile-parts.\n");
	fprintf(stdout,
			"    Division is made by grouping Resolutions (R), Layers (L)\n");
	fprintf(stdout, "    or Components (C).\n");
	fprintf(stdout, "[-R|-ROI] c=<component index>,U=<upshifting value>\n");
	fprintf(stdout, "    Quantization indices upshifted for a component. \n");
	fprintf(stdout,
			"     This option does not implement the usual ROI (Region of Interest).\n");
	fprintf(stdout,
			"    It should be understood as a 'Component of Interest'. It offers the \n");
	fprintf(stdout,
			"    possibility to upshift the value of a component during quantization step.\n");
	fprintf(stdout,
			"    The value after c= is the component number [0, 1, 2, ...] and the value \n");
	fprintf(stdout,
			"    after U= is the value of upshifting. U must be in the range [0, 37].\n");
	fprintf(stdout, "[-d|-ImageOffset] <image offset X,image offset Y>\n");
	fprintf(stdout, "    Offset of the origin of the image.\n");
	fprintf(stdout, "[-T|-TileOffset] <tile offset X,tile offset Y>\n");
	fprintf(stdout, "    Offset of the origin of the tiles.\n");
	fprintf(stdout, "[-I|-Irreversible\n");
	fprintf(stdout, "    Use the irreversible DWT 9-7.\n");
	fprintf(stdout, "[-Y|-mct] <0|1|2>\n");
	fprintf(stdout,
			"    Explicitly specifies if a Multiple Component Transform has to be used.\n");
	fprintf(stdout,
			"    0: no MCT ; 1: RGB->YCC conversion ; 2: custom MCT.\n");
	fprintf(stdout,
			"    If custom MCT, \"-m\" option has to be used (see hereunder).\n");
	fprintf(stdout,
			"    By default, RGB->YCC conversion is used if there are 3 components or more,\n");
	fprintf(stdout, "    no conversion otherwise.\n");
	fprintf(stdout, "[-m|-CustomMCT <file>\n");
	fprintf(stdout,
			"    Use array-based MCT, values are coma separated, line by line\n");
	fprintf(stdout,
			"    No specific separators between lines, no space allowed between values.\n");
	fprintf(stdout,
			"    If this option is used, it automatically sets \"-mct\" option to 2.\n");
	fprintf(stdout, "[-Z|-RSIZ] <rsiz>\n");
	fprintf(stdout, "    Profile, main level, sub level and version.\n");
	fprintf(stdout,
			"	Note: this flag will be ignored if cinema profile flags are used.\n");
	fprintf(stdout, "[-w|-cinema2K] <24|48>\n");
	fprintf(stdout, "    Digital Cinema 2K profile compliant codestream.\n");
	fprintf(stdout, "	Need to specify the frames per second.\n");
	fprintf(stdout, "    Only 24 or 48 fps are currently allowed.\n");
	fprintf(stdout, "[-x|-cinema4K] <24|48>\n");
	fprintf(stdout, "    Digital Cinema 4K profile compliant codestream.\n");
	fprintf(stdout, "	Need to specify the frames per second.\n");
	fprintf(stdout, "    Only 24 or 48 fps are currently allowed.\n");
	fprintf(stdout,
			"-U|-BROADCAST <PROFILE>[,mainlevel=X][,framerate=FPS]\n");
	fprintf(stdout, "    Broadcast compliant codestream.\n");
	fprintf(stdout, "    <PROFILE>=SINGLE,MULTI and MULTI_R.\n");
	fprintf(stdout, "    X >= 0 and X <= 11.\n");
	fprintf(stdout,
			"    framerate > 0 may be specified to enhance checks and set maximum bit rate when Y > 0.\n");
	fprintf(stdout,
			"-z|-IMF <PROFILE>[,mainlevel=X][,sublevel=Y][,framerate=FPS]\n");
	fprintf(stdout, "    Interoperable Master Format compliant codestream.\n");
	fprintf(stdout, "    <PROFILE>=2K, 4K, 8K, 2K_R, 4K_R or 8K_R.\n");
	fprintf(stdout, "    X >= 0 and X <= 11.\n");
	fprintf(stdout, "    Y >= 0 and Y <= 9.\n");
	fprintf(stdout,
			"    framerate > 0 may be specified to enhance checks and set maximum bit rate when Y > 0.\n");
	fprintf(stdout, "[-C|-Comment] <comment>\n");
	fprintf(stdout, "    Add <comment> in the comment marker segment.\n");
	fprintf(stdout,
			"[-Q|-CaptureRes] <capture resolution X,capture resolution Y>\n");
	fprintf(stdout,
			"    Capture resolution in pixels/metre, in double precision.\n");
	fprintf(stdout,
			"    These values will override the resolution stored in the input image, if present \n");
	fprintf(stdout,
			"    unless the special values <0,0> are passed in, in which case \n");
	fprintf(stdout, "    the image resolution will be used.\n");
	fprintf(stdout,
			"[-D|-DisplayRes] <display resolution X,display resolution Y>\n");
	fprintf(stdout,
			"    Display resolution in pixels/metre, in double precision.\n");
	fprintf(stdout, "[-e|-Repetitions] <number of repetitions>\n");
	fprintf(stdout,
			"    Number of repetitions, for either a single image, or a folder of images. Default is 1. 0 signifies unlimited repetitions. \n");
	fprintf(stdout, "[-g|-PluginPath] <plugin path>\n");
	fprintf(stdout, "    Path to T1 plugin.\n");
	fprintf(stdout, "[-H|-num_threads] <number of threads>\n");
	fprintf(stdout, "    Number of threads to use for T1.\n");
	fprintf(stdout, "[-G|-DeviceId] <device ID>\n");
	fprintf(stdout,
			"    (GPU) Specify which GPU accelerator to run codec on.\n");
	fprintf(stdout, "    A value of -1 will specify all devices.\n");
	fprintf(stdout, "\n");
}

static GRK_PROG_ORDER give_progression(const char progression[4]) {
	if (strncmp(progression, "LRCP", 4) == 0) {
		return GRK_LRCP;
	}
	if (strncmp(progression, "RLCP", 4) == 0) {
		return GRK_RLCP;
	}
	if (strncmp(progression, "RPCL", 4) == 0) {
		return GRK_RPCL;
	}
	if (strncmp(progression, "PCRL", 4) == 0) {
		return GRK_PCRL;
	}
	if (strncmp(progression, "CPRL", 4) == 0) {
		return GRK_CPRL;
	}

	return GRK_PROG_UNKNOWN;
}

static int load_images(grk_dircnt *dirptr, char *imgdirpath) {
	/*Reading the input images from given input directory*/

	DIR *dir = opendir(imgdirpath);
	if (!dir) {
		spdlog::error("Could not open Folder {}", imgdirpath);
		return 1;
	}

	struct dirent *content = nullptr;
	int i = 0;
	while ((content = readdir(dir)) != nullptr) {
		if (strcmp(".", content->d_name) == 0
				|| strcmp("..", content->d_name) == 0)
			continue;

		strcpy(dirptr->filename[i], content->d_name);
		i++;
	}
	closedir(dir);
	return 0;
}

static char get_next_file(std::string image_filename, grk_img_fol *img_fol,
		grk_img_fol *out_fol, grk_cparameters *parameters) {

	if (parameters->verbose)
		spdlog::info("File \"{}\"", image_filename.c_str());
	std::string infilename = img_fol->imgdirpath
			+ std::string(grk::get_path_separator()) + image_filename;
	if (parameters->decod_format == GRK_UNK_FMT) {
		int fmt = get_file_format((char*) infilename.c_str());
		if (fmt <= GRK_UNK_FMT)
			return 1;
		parameters->decod_format = (GRK_SUPPORTED_FILE_FMT) fmt;
	}
	if (grk::strcpy_s(parameters->infile, sizeof(parameters->infile),
			infilename.c_str()) != 0) {
		return 1;
	}
	std::string output_root_filename;
	// if we don't find a file tag, then just use the full file name
	auto pos = image_filename.find(".");
	if (pos != std::string::npos)
		output_root_filename = image_filename.substr(0, pos);
	else
		output_root_filename = image_filename;
	if (img_fol->set_out_format) {
		std::string outfilename = out_fol->imgdirpath
				+ std::string(grk::get_path_separator()) + output_root_filename
				+ "." + img_fol->out_format;
		if (grk::strcpy_s(parameters->outfile, sizeof(parameters->outfile),
				outfilename.c_str()) != 0) {
			return 1;
		}
	}
	return 0;
}

static bool isDecodedFormatSupported(GRK_SUPPORTED_FILE_FMT format) {
	switch (format) {
	case GRK_PGX_FMT:
	case GRK_PXM_FMT:
	case GRK_BMP_FMT:
	case GRK_TIF_FMT:
	case GRK_RAW_FMT:
	case GRK_RAWL_FMT:
	case GRK_TGA_FMT:
	case GRK_PNG_FMT:
	case GRK_JPG_FMT:
		break;
	default:
		return false;
	}
	return true;
}

class GrokOutput: public StdOutput {
public:
	virtual void usage(CmdLineInterface &c) {
		(void) c;
		encode_help_display();
	}
};

/* ------------------------------------------------------------------------------------ */

static bool checkCinema(ValueArg<uint32_t> *arg, uint16_t profile,
		grk_cparameters *parameters) {
	bool isValid = true;
	if (arg->isSet()) {
		uint32_t fps = arg->getValue();
		if (fps == 24) {
			parameters->rsiz = profile;
			parameters->framerate = 24;
			parameters->max_comp_size = GRK_CINEMA_24_COMP;
			parameters->max_cs_size = GRK_CINEMA_24_CS;
		} else if (fps == 48) {
			parameters->rsiz = profile;
			parameters->framerate = 48;
			parameters->max_comp_size = GRK_CINEMA_48_COMP;
			parameters->max_cs_size = GRK_CINEMA_48_CS;
		} else {
			isValid = false;
			if (parameters->verbose)
				spdlog::error(
						"Incorrect digital cinema frame rate {} : must be either 24 or 48",
						fps);
		}
	}
	return isValid;
}
static int parse_cmdline_encoder_ex(int argc, char **argv,
		grk_cparameters *parameters, grk_img_fol *img_fol, grk_img_fol *out_fol,
		char *indexfilename, size_t indexfilename_size, char *plugin_path) {
	(void) indexfilename;
	(void) indexfilename_size;
	try {

		// Define the command line object.
		CmdLine cmd("Command description message", ' ', grk_version());

		// set the output
		GrokOutput output;
		cmd.setOutput(&output);

		// Kernel build flags:
		// 1 indicates build binary, otherwise load binary
		// 2 indicates generate binaries
		ValueArg<uint32_t> kernelBuildOptionsArg("k", "KernelBuild",
				"Kernel build options", false, 0, "unsigned integer", cmd);

		ValueArg<uint32_t> repetitionsArg("e", "Repetitions",
				"Number of encode repetitions, for either a folder or a single file",
				false, 0, "unsigned integer", cmd);

		ValueArg<uint16_t> rsizArg("Z", "RSIZ", "RSIZ", false, 0,
				"unsigned integer", cmd);

		ValueArg<uint32_t> cinema2KArg("w", "cinema2K",
				"Digital cinema 2K profile", false, 24, "unsigned integer",
				cmd);
		ValueArg<uint32_t> cinema4KArg("x", "cinema4K",
				"Digital cinema 2K profile", false, 24, "unsigned integer",
				cmd);
		ValueArg<string> IMFArg("z", "IMF", "IMF profile", false, "", "string",
				cmd);
		ValueArg<string> BroadcastArg("U", "BROADCAST", "Broadcast profile", false, "", "string",
				cmd);
		ValueArg<string> imgDirArg("y", "ImgDir", "Image directory", false, "",
				"string", cmd);
		ValueArg<string> outDirArg("a", "OutDir", "Output directory", false, "",
				"string", cmd);

		ValueArg<string> pluginPathArg("g", "PluginPath", "Plugin path", false,
				"", "string", cmd);
		ValueArg<uint32_t> numThreadsArg("H", "num_threads",
				"Number of threads", false, 0, "unsigned integer", cmd);

		ValueArg<int32_t> deviceIdArg("G", "DeviceId", "Device ID", false, 0,
				"integer", cmd);

		ValueArg<string> inputFileArg("i", "InputFile", "Input file", false, "",
				"string", cmd);
		ValueArg<string> outputFileArg("o", "OutputFile", "Output file", false,
				"", "string", cmd);

		ValueArg<string> outForArg("O", "OutFor", "Output format", false, "",
				"string", cmd);

		ValueArg<string> inForArg("K", "InFor", "InputFormat format", false, "",
				"string", cmd);

		SwitchArg sopArg("S", "SOP", "Add SOP markers", cmd);

		SwitchArg ephArg("E", "EPH", "Add EPH markers", cmd);

		ValueArg<uint8_t> tpArg("u", "TP", "Tile part generation", false, 0,
				"uint8_t", cmd);

		ValueArg<string> tileOffsetArg("T", "TileOffset", "Tile offset", false,
				"", "string", cmd);

		ValueArg<string> pocArg("P", "POC", "Progression order changes", false,
				"", "string", cmd);

		ValueArg<string> roiArg("R", "ROI", "Region of interest", false, "",
				"string", cmd);

		ValueArg<uint32_t> mctArg("Y", "mct", "Multi component transform",
				false, 0, "unsigned integer", cmd);

		ValueArg<string> captureResArg("Q", "CaptureRes", "Capture resolution",
				false, "", "string", cmd);

		ValueArg<string> displayResArg("D", "DisplayRes", "Display resolution",
				false, "", "string", cmd);

		ValueArg<string> compressionRatiosArg("r", "CompressionRatios",
				"Layer rates expressed as compression ratios", false, "",
				"string", cmd);

		ValueArg<string> qualityArg("q", "Quality",
				"Layer rates expressed as quality", false, "", "string", cmd);

		ValueArg<string> rawFormatArg("F", "Raw", "Raw image format parameters",
				false, "", "string", cmd);

		ValueArg<string> tilesArg("t", "TileDim", "Tile parameters", false, "",
				"string", cmd);

		ValueArg<uint32_t> resolutionArg("n", "Resolutions", "Resolution",
				false, 0, "unsigned integer", cmd);

		ValueArg<string> precinctDimArg("c", "PrecinctDim",
				"Precinct dimensions", false, "", "string", cmd);

		ValueArg<string> codeBlockDimArg("b", "CodeBlockDim",
				"Code block dimension", false, "", "string", cmd);

		ValueArg<string> progressionOrderArg("p", "ProgressionOrder",
				"Progression order", false, "", "string", cmd);

		// this flag is currently disabled 
		ValueArg<string> subsamplingFactorArg("s", "SubsamplingFactor",
				"Subsampling factor", false, "", "string"/*, cmd*/);

		ValueArg<string> imageOffsetArg("d", "ImageOffset",
				"Image offset in reference grid coordinates", false, "",
				"string", cmd);

		ValueArg<uint32_t> cblkSty("M", "Mode", "Mode", false, 0,
				"unsigned integer", cmd);

		ValueArg<string> commentArg("C", "Comment", "Add a comment", false, "",
				"string", cmd);

		SwitchArg irreversibleArg("I", "Irreversible", "Irreversible", cmd);

		ValueArg<string> customMCTArg("m", "CustomMCT", "MCT input file", false,
				"", "string", cmd);

		ValueArg<uint32_t> durationArg("J", "Duration", "Duration in seconds",
				false, 0, "unsigned integer", cmd);

		ValueArg<uint32_t> rateControlAlgoArg("A", "RateControlAlgorithm",
				"Rate control algorithm", false, 0, "unsigned integer", cmd);

		SwitchArg verboseArg("v", "verbose", "Verbose", cmd);

		cmd.parse(argc, argv);

		img_fol->set_out_format = false;
		parameters->raw_cp.width = 0;

		if (verboseArg.isSet())
			parameters->verbose = verboseArg.getValue();

		if (repetitionsArg.isSet())
			parameters->repeats = repetitionsArg.getValue();

		if (kernelBuildOptionsArg.isSet())
			parameters->kernelBuildOptions = kernelBuildOptionsArg.getValue();

		if (rateControlAlgoArg.isSet())
			parameters->rateControlAlgorithm = rateControlAlgoArg.getValue();

		if (numThreadsArg.isSet())
			parameters->numThreads = numThreadsArg.getValue();

		if (deviceIdArg.isSet())
			parameters->deviceId = deviceIdArg.getValue();

		if (durationArg.isSet())
			parameters->duration = durationArg.getValue();

		if (inForArg.isSet()) {
			auto dummy = "dummy." + inForArg.getValue();
			char *infile = (char*) (dummy).c_str();
			parameters->decod_format = (GRK_SUPPORTED_FILE_FMT) get_file_format(
					(char*) infile);
			if (parameters->verbose
					&& !isDecodedFormatSupported(parameters->decod_format)) {
				spdlog::warn(
						" Ignoring unknown input file format: %s \n"
								"        Known file formats are *.pnm, *.pgm, *.ppm, *.pgx, *png, *.bmp, *.tif, *.jpg, *.raw or *.tga",
						infile);
			}
		}

		if (inputFileArg.isSet()) {
			char *infile = (char*) inputFileArg.getValue().c_str();
			if (parameters->decod_format == GRK_UNK_FMT) {
				parameters->decod_format =
						(GRK_SUPPORTED_FILE_FMT) get_file_format(infile);
				if (!isDecodedFormatSupported(parameters->decod_format)) {
					spdlog::error(
							"Unknown input file format: {} \n"
									"        Known file formats are *.pnm, *.pgm, *.ppm, *.pgx, *png, *.bmp, *.tif, *.jpg, *.raw or *.tga",
							infile);
					return 1;
				}
			}
			if (grk::strcpy_s(parameters->infile, sizeof(parameters->infile),
					infile) != 0) {
				return 1;
			}
		} else {
			// check for possible input from STDIN
			if (!imgDirArg.isSet()) {
				bool fromStdin =
						inForArg.isSet()
								&& grk::supportedStdioFormat(
										(GRK_SUPPORTED_FILE_FMT) parameters->decod_format);
				if (!fromStdin) {
					spdlog::error("Missing input file");
					return 1;
				}
			}
		}

		if (outForArg.isSet()) {
			char outformat[50];
			char *of = (char*) outForArg.getValue().c_str();
			sprintf(outformat, ".%s", of);
			img_fol->set_out_format = true;
			parameters->cod_format = (GRK_SUPPORTED_FILE_FMT) get_file_format(
					outformat);
			switch (parameters->cod_format) {
			case GRK_J2K_FMT:
				img_fol->out_format = "j2k";
				break;
			case GRK_JP2_FMT:
				img_fol->out_format = "jp2";
				break;
			default:
				spdlog::error(
						"Unknown output format image [only j2k, j2c, jp2]!! ");
				return 1;
			}
		}

		if (outputFileArg.isSet()) {
			char *outfile = (char*) outputFileArg.getValue().c_str();
			parameters->cod_format = (GRK_SUPPORTED_FILE_FMT) get_file_format(
					outfile);
			switch (parameters->cod_format) {
			case GRK_J2K_FMT:
			case GRK_JP2_FMT:
				break;
			default:
				spdlog::error(
						"Unknown output format image {} [only *.j2k, *.j2c or *.jp2]!! ",
						outfile);
				return 1;
			}
			if (grk::strcpy_s(parameters->outfile, sizeof(parameters->outfile),
					outfile) != 0) {
				return 1;
			}
		}

		if (compressionRatiosArg.isSet()) {
			char *s = (char*) compressionRatiosArg.getValue().c_str();
			parameters->tcp_numlayers = 0;
			while (sscanf(s, "%lf",
					&parameters->tcp_rates[parameters->tcp_numlayers]) == 1) {
				parameters->tcp_numlayers++;
				while (*s && *s != ',') {
					s++;
				}
				if (!*s)
					break;
				s++;
			}

			// sanity check on rates
			double lastRate = DBL_MAX;
			for (uint32_t i = 0; i < parameters->tcp_numlayers; ++i) {
				if (parameters->tcp_rates[i] > lastRate) {
					spdlog::error("rates must be listed in descending order");
					return 1;
				}
				if (parameters->tcp_rates[i] < 1.0) {
					spdlog::error("rates must be greater than or equal to one");
					return 1;
				}
				lastRate = parameters->tcp_rates[i];
			}

			parameters->cp_disto_alloc = 1;
			// set compression ratio of 1 equal to 0, to signal lossless layer
			for (uint32_t i = 0; i < parameters->tcp_numlayers; ++i) {
				if (parameters->tcp_rates[i] == 1)
					parameters->tcp_rates[i] = 0;
			}
		}

		if (qualityArg.isSet()) {
			char *s = (char*) qualityArg.getValue().c_str();
			;
			while (sscanf(s, "%lf",
					&parameters->tcp_distoratio[parameters->tcp_numlayers]) == 1) {
				parameters->tcp_numlayers++;
				while (*s && *s != ',') {
					s++;
				}
				if (!*s)
					break;
				s++;
			}
			parameters->cp_fixed_quality = 1;

			// sanity check on quality values
			double lastDistortion = -1;
			for (uint32_t i = 0; i < parameters->tcp_numlayers; ++i) {
				auto distortion = parameters->tcp_distoratio[i];
				if (distortion < 0) {
					spdlog::error(
							"PSNR values must be greater than or equal to zero");
					return 1;
				}
				if (distortion < lastDistortion
						&& !(i == parameters->tcp_numlayers - 1
								&& distortion == 0)) {
					spdlog::error(
							"PSNR values must be listed in ascending order");
					return 1;
				}
				lastDistortion = distortion;
			}

		}

		if (rawFormatArg.isSet()) {
			bool wrong = false;
			char *substr1;
			char *substr2;
			char *sep;
			char signo;
			int width, height, bitdepth, ncomp;
			uint32_t len;
			bool raw_signed = false;
			substr2 = (char*) strchr(rawFormatArg.getValue().c_str(), '@');
			if (substr2 == nullptr) {
				len = (uint32_t) rawFormatArg.getValue().length();
			} else {
				len = (uint32_t) (substr2 - rawFormatArg.getValue().c_str());
				substr2++; /* skip '@' character */
			}
			substr1 = (char*) malloc((len + 1) * sizeof(char));
			if (substr1 == nullptr) {
				return 1;
			}
			memcpy(substr1, rawFormatArg.getValue().c_str(), len);
			substr1[len] = '\0';
			if (sscanf(substr1, "%d,%d,%d,%d,%c", &width, &height, &ncomp,
					&bitdepth, &signo) == 5) {
				if (signo == 's') {
					raw_signed = true;
				} else if (signo == 'u') {
					raw_signed = false;
				} else {
					wrong = true;
				}
			} else {
				wrong = true;
			}
			if (!wrong) {
				grk_raw_cparameters *raw_cp = &parameters->raw_cp;
				int compno;
				int lastdx = 1;
				int lastdy = 1;
				raw_cp->width = (uint32_t) width;
				raw_cp->height = (uint32_t) height;
				raw_cp->numcomps = (uint16_t) ncomp;
				raw_cp->prec = (uint32_t) bitdepth;
				raw_cp->sgnd = raw_signed;
				raw_cp->comps = (grk_raw_comp_cparameters*) malloc(
						((uint32_t) (ncomp))
								* sizeof(grk_raw_comp_cparameters));
				if (raw_cp->comps == nullptr) {
					free(substr1);
					return 1;
				}
				for (compno = 0; compno < ncomp && !wrong; compno++) {
					if (substr2 == nullptr) {
						raw_cp->comps[compno].dx = (uint32_t) lastdx;
						raw_cp->comps[compno].dy = (uint32_t) lastdy;
					} else {
						int dx, dy;
						sep = strchr(substr2, ':');
						if (sep == nullptr) {
							if (sscanf(substr2, "%dx%d", &dx, &dy) == 2) {
								lastdx = dx;
								lastdy = dy;
								raw_cp->comps[compno].dx = (uint32_t) dx;
								raw_cp->comps[compno].dy = (uint32_t) dy;
								substr2 = nullptr;
							} else {
								wrong = true;
							}
						} else {
							if (sscanf(substr2, "%dx%d:%s", &dx, &dy, substr2)
									== 3) {
								raw_cp->comps[compno].dx = (uint32_t) dx;
								raw_cp->comps[compno].dy = (uint32_t) dy;
							} else {
								wrong = true;
							}
						}
					}
				}
			}
			free(substr1);
			if (wrong) {
				spdlog::error("\n invalid raw image parameters");
				spdlog::error("Please use the Format option -F:");
				spdlog::error(
						"-F <width>,<height>,<ncomp>,<bitdepth>,{s,u}@<dx1>x<dy1>:...:<dxn>x<dyn>");
				spdlog::error(
						"If subsampling is omitted, 1x1 is assumed for all components");
				spdlog::error(
						"Example: -i image.raw -o image.j2k -F 512,512,3,8,u@1x1:2x2:2x2");
				spdlog::error(
						"         for raw 512x512 image with 4:2:0 subsampling");
				return 1;
			}
		}

		if (tilesArg.isSet()) {
			int32_t cp_tdx = 0, cp_tdy = 0;
			if (sscanf(tilesArg.getValue().c_str(), "%d,%d", &cp_tdx,
					&cp_tdy) == EOF) {
				spdlog::error("sscanf failed for tiles argument");
				return 1;
			}
			// sanity check on tile dimensions
			if (cp_tdx <= 0 || cp_tdy <= 0) {
				spdlog::error("Tile dimensions must be strictly positive");
				return 1;
			}
			parameters->cp_tdx = (uint32_t) cp_tdx;
			parameters->cp_tdy = (uint32_t) cp_tdy;
			parameters->tile_size_on = true;

		}

		if (resolutionArg.isSet())
			parameters->numresolution = resolutionArg.getValue();

		if (precinctDimArg.isSet()) {
			char sep;
			int res_spec = 0;

			char *s = (char*) precinctDimArg.getValue().c_str();
			int ret;
			do {
				sep = 0;
				ret = sscanf(s, "[%d,%d]%c", &parameters->prcw_init[res_spec],
						&parameters->prch_init[res_spec], &sep);
				if (!(ret == 2 && sep == 0) && !(ret == 3 && sep == ',')) {
					spdlog::error(
							"\n could not parse precinct dimension: '{}' {0:x}",
							s, sep);
					spdlog::error(
							"Example: -i lena.raw -o lena.j2k -c [128,128],[128,128]");
					return 1;
				}
				parameters->csty |= 0x01;
				res_spec++;
				s = strpbrk(s, "]") + 2;
			} while (sep == ',');
			parameters->res_spec = (uint32_t) res_spec;
		}

		if (codeBlockDimArg.isSet()) {
			int cblockw_init = 0, cblockh_init = 0;
			if (sscanf(codeBlockDimArg.getValue().c_str(), "%d,%d",
					&cblockw_init, &cblockh_init) == EOF) {
				spdlog::error(
						"sscanf failed for code block dimension argument");
				return 1;
			}
			if (cblockw_init * cblockh_init > 4096 || cblockw_init > 1024
					|| cblockw_init < 4 || cblockh_init > 1024
					|| cblockh_init < 4) {
				spdlog::error(
						"Size of code block error (option -b)\n\nRestriction :\n"
								"    * width*height<=4096\n    * 4<=width,height<= 1024");
				return 1;
			}
			parameters->cblockw_init = (uint32_t) cblockw_init;
			parameters->cblockh_init = (uint32_t) cblockh_init;
		}

		if (progressionOrderArg.isSet()) {
			char progression[4];

			strncpy(progression, progressionOrderArg.getValue().c_str(), 4);
			parameters->prog_order = give_progression(progression);
			if (parameters->prog_order == -1) {
				spdlog::error("Unrecognized progression order "
						"[LRCP, RLCP, RPCL, PCRL, CPRL] !!");
				return 1;
			}
		}

		if (subsamplingFactorArg.isSet()) {
			if (sscanf(subsamplingFactorArg.getValue().c_str(), "%d,%d",
					&parameters->subsampling_dx, &parameters->subsampling_dy)
					!= 2) {
				spdlog::error("'-s' sub-sampling argument error !  [-s dx,dy]");
				return 1;
			}
		}

		if (imageOffsetArg.isSet()) {
			if (sscanf(imageOffsetArg.getValue().c_str(), "%d,%d",
					&parameters->image_offset_x0, &parameters->image_offset_y0)
					!= 2) {
				spdlog::error("-d 'image offset' argument "
						"error !! [-d x0,y0]");
				return 1;
			}
		}

		if (pocArg.isSet()) {
			uint32_t numpocs = 0; /* number of progression order change (POC) default 0 */
			grk_poc *POC = nullptr; /* POC : used in case of Progression order change */

			char *s = (char*) pocArg.getValue().c_str();
			POC = parameters->POC;

			while (sscanf(s, "T%u=%u,%u,%u,%u,%u,%4s", &POC[numpocs].tile,
					&POC[numpocs].resno0, &POC[numpocs].compno0,
					&POC[numpocs].layno1, &POC[numpocs].resno1,
					&POC[numpocs].compno1, POC[numpocs].progorder) == 7) {
				POC[numpocs].prg1 = give_progression(POC[numpocs].progorder);
				numpocs++;
				while (*s && *s != '/') {
					s++;
				}
				if (!*s) {
					break;
				}
				s++;
			}
			parameters->numpocs = numpocs;
		}

		if (sopArg.isSet())
			parameters->csty |= 0x02;

		if (ephArg.isSet())
			parameters->csty |= 0x04;

		if (irreversibleArg.isSet())
			parameters->irreversible = true;

		if (pluginPathArg.isSet()) {
			if (plugin_path)
				strcpy(plugin_path, pluginPathArg.getValue().c_str());
		}

		img_fol->set_imgdir = false;
		if (imgDirArg.isSet()) {
			img_fol->imgdirpath = (char*) malloc(
					strlen(imgDirArg.getValue().c_str()) + 1);
			strcpy(img_fol->imgdirpath, imgDirArg.getValue().c_str());
			img_fol->set_imgdir = true;
		}
		if (out_fol) {
			out_fol->set_imgdir = false;
			if (outDirArg.isSet()) {
				out_fol->imgdirpath = (char*) malloc(
						strlen(outDirArg.getValue().c_str()) + 1);
				strcpy(out_fol->imgdirpath, outDirArg.getValue().c_str());
				out_fol->set_imgdir = true;
			}
		}
		if (cblkSty.isSet()) {
			parameters->cblk_sty = cblkSty.getValue() & 0X7F;
			if (parameters->cblk_sty & GRK_CBLKSTY_HT) {
				if (parameters->cblk_sty != GRK_CBLKSTY_HT) {
					spdlog::error(
							"High throughput encoding mode cannot be combined"
									" with any other block mode switches. Ignoring mode switch");
					parameters->cblk_sty = 0;
				} else {
					parameters->isHT = true;
				}
			}
		}
		// profiles
		if (!parameters->isHT) {
			if (cinema2KArg.isSet()) {
				if (!checkCinema(&cinema2KArg, GRK_PROFILE_CINEMA_2K,
						parameters)) {
					return 1;
				}
				if (parameters->verbose) {
					spdlog::warn("CINEMA 2K profile activated\n"
							"Other options specified may be overridden");
				}
			}
			if (cinema4KArg.isSet()) {
				if (!checkCinema(&cinema4KArg, GRK_PROFILE_CINEMA_4K,
						parameters)) {
					return 1;
				}
				if (parameters->verbose) {
					spdlog::warn(" CINEMA 4K profile activated\n"
							"Other options specified may be overridden");
				}
			}
			if (BroadcastArg.isSet()) {
				int mainlevel = 0;
				int profile = 0;
				int framerate = 0;
				const char *msg =
						"Wrong value for -BROADCAST. Should be "
								"<PROFILE>[,mainlevel=X][,framerate=FPS] where <PROFILE> is one "
								"of SINGLE/MULTI/MULTI_R.";
				char *arg = (char*) BroadcastArg.getValue().c_str();
				char *comma;

				comma = strstr(arg, ",mainlevel=");
				if (comma
						&& sscanf(comma + 1, "mainlevel=%d", &mainlevel) != 1) {
					spdlog::error("{}", msg);
					return 1;
				}
				comma = strstr(arg, ",framerate=");
				if (comma
						&& sscanf(comma + 1, "framerate=%d", &framerate) != 1) {
					spdlog::error("{}", msg);
					return 1;
				}

				comma = strchr(arg, ',');
				if (comma != NULL) {
					*comma = 0;
				}

				if (strcmp(arg, "SINGLE") == 0) {
					profile = GRK_PROFILE_BC_SINGLE;
				} else if (strcmp(arg, "MULTI") == 0) {
					profile = GRK_PROFILE_BC_MULTI;
				} else if (strcmp(arg, "MULTI_R") == 0) {
					profile = GRK_PROFILE_BC_MULTI_R;
				} else {
					spdlog::error("{}", msg);
					return 1;
				}

				if (!(mainlevel >= 0 && mainlevel <= 11)) {
					/* Voluntarily rough validation. More fine grained done in library */
					spdlog::error("Invalid mainlevel value {}.\n", mainlevel);
					return 1;
				}
				parameters->rsiz = (uint16_t) (profile | mainlevel);

				if (parameters->verbose) {
					spdlog::info("Broadcast profile activated\n"
							"Other options specified could be overridden");
				}

				parameters->framerate = framerate;
				if (framerate > 0) {
					const int limitMBitsSec[] = { 0,
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
							GRK_BROADCAST_LEVEL_11_MBITSSEC
					};
					parameters->max_cs_size =
							(uint64_t) (limitMBitsSec[mainlevel]
									* (1000.0 * 1000 / 8) / framerate);
					if (parameters->verbose) {
						spdlog::info("Setting max codestream size to {} bytes.",
								parameters->max_cs_size);
					}
				}
			}
			if (IMFArg.isSet()) {
				int mainlevel = 0;
				int sublevel = 0;
				int profile = 0;
				int framerate = 0;
				const char *msg =
						"Wrong value for -IMF. Should be "
								"<PROFILE>[,mainlevel=X][,sublevel=Y][,framerate=FPS] where <PROFILE> is one "
								"of 2K/4K/8K/2K_R/4K_R/8K_R.";
				char *arg = (char*) IMFArg.getValue().c_str();
				char *comma;

				comma = strstr(arg, ",mainlevel=");
				if (comma
						&& sscanf(comma + 1, "mainlevel=%d", &mainlevel) != 1) {
					spdlog::error("{}", msg);
					return 1;
				}

				comma = strstr(arg, ",sublevel=");
				if (comma && sscanf(comma + 1, "sublevel=%d", &sublevel) != 1) {
					spdlog::error("{}", msg);
					return 1;
				}

				comma = strstr(arg, ",framerate=");
				if (comma
						&& sscanf(comma + 1, "framerate=%d", &framerate) != 1) {
					spdlog::error("{}", msg);
					return 1;
				}

				comma = strchr(arg, ',');
				if (comma != NULL) {
					*comma = 0;
				}

				if (strcmp(arg, "2K") == 0) {
					profile = GRK_PROFILE_IMF_2K;
				} else if (strcmp(arg, "4K") == 0) {
					profile = GRK_PROFILE_IMF_4K;
				} else if (strcmp(arg, "8K") == 0) {
					profile = GRK_PROFILE_IMF_8K;
				} else if (strcmp(arg, "2K_R") == 0) {
					profile = GRK_PROFILE_IMF_2K_R;
				} else if (strcmp(arg, "4K_R") == 0) {
					profile = GRK_PROFILE_IMF_4K_R;
				} else if (strcmp(arg, "8K_R") == 0) {
					profile = GRK_PROFILE_IMF_8K_R;
				} else {
					spdlog::error("{}", msg);
					return 1;
				}

				if (!(mainlevel >= 0 && mainlevel <= 11)) {
					/* Voluntarily rough validation. More fine grained done in library */
					spdlog::error("Invalid main level {}.\n",mainlevel);
					return 1;
				}
				if (!(sublevel >= 0 && sublevel <= 9)) {
					/* Voluntarily rough validation. More fine grained done in library */
					spdlog::error("Invalid sub-level {}.\n",sublevel);
					return 1;
				}
				parameters->rsiz = (uint16_t) (profile | (sublevel << 4)
						| mainlevel);

				if (parameters->verbose) {
					spdlog::info("IMF profile activated\n"
							"Other options specified could be overridden");
				}

				parameters->framerate = framerate;
				if (framerate > 0 && sublevel > 0 && sublevel <= 9) {
					const int limitMBitsSec[] = { 0,
					GRK_IMF_SUBLEVEL_1_MBITSSEC,
					GRK_IMF_SUBLEVEL_2_MBITSSEC,
					GRK_IMF_SUBLEVEL_3_MBITSSEC,
					GRK_IMF_SUBLEVEL_4_MBITSSEC,
					GRK_IMF_SUBLEVEL_5_MBITSSEC,
					GRK_IMF_SUBLEVEL_6_MBITSSEC,
					GRK_IMF_SUBLEVEL_7_MBITSSEC,
					GRK_IMF_SUBLEVEL_8_MBITSSEC,
					GRK_IMF_SUBLEVEL_9_MBITSSEC };
					parameters->max_cs_size =
							(uint64_t) (limitMBitsSec[sublevel]
									* (1000.0 * 1000 / 8) / framerate);
					if (parameters->verbose) {
						spdlog::info("Setting max codestream size to {} bytes.",
								parameters->max_cs_size);
					}
				}
			}


			if (rsizArg.isSet()) {
				if (cinema2KArg.isSet() || cinema4KArg.isSet()) {
					warning_callback(
							"  Cinema profile set - RSIZ parameter ignored.",
							nullptr);
				} else if (IMFArg.isSet()) {
					warning_callback(
							"  IMF profile set - RSIZ parameter ignored.",
							nullptr);
				} else {
					parameters->rsiz = rsizArg.getValue();
				}
			}
		} else {
			parameters->rsiz |= GRK_JPH_RSIZ_FLAG;
		}

		if (captureResArg.isSet()) {
			if (sscanf(captureResArg.getValue().c_str(), "%lf,%lf",
					parameters->capture_resolution,
					parameters->capture_resolution + 1) != 2) {
				spdlog::error(
						"-Q 'capture resolution' argument error !! [-Q X0,Y0]");
				return 1;
			}
			parameters->write_capture_resolution = true;
		}
		if (displayResArg.isSet()) {
			if (sscanf(captureResArg.getValue().c_str(), "%lf,%lf",
					parameters->display_resolution,
					parameters->display_resolution + 1) != 2) {
				spdlog::error(
						"-D 'display resolution' argument error !! [-D X0,Y0]");
				return 1;
			}
			parameters->write_display_resolution = true;
		}

		if (mctArg.isSet()) {
			uint32_t mct_mode = mctArg.getValue();
			if (mct_mode > 2) {
				spdlog::error(
						"Incorrect MCT value {}. Must be equal to 0, 1 or 2.",
						mct_mode);
				return 1;
			}
			parameters->tcp_mct = (uint8_t) mct_mode;
		}

		if (customMCTArg.isSet()) {
			char *lFilename = (char*) customMCTArg.getValue().c_str();
			char *lMatrix = nullptr;
			char *lCurrentPtr = nullptr;
			float *lCurrentDoublePtr = nullptr;
			float *lSpace = nullptr;
			int *int_ptr = nullptr;
			int lNbComp = 0, lTotalComp, lMctComp, i2;
			size_t lStrLen, lStrFread;
			uint32_t rc = 1;

			/* Open file */
			FILE *lFile = fopen(lFilename, "r");
			if (!lFile)
				goto cleanup;

			/* Set size of file and read its content*/
			if (fseek(lFile, 0, SEEK_END))
				goto cleanup;

			lStrLen = (size_t) ftell(lFile);
			if (fseek(lFile, 0, SEEK_SET))
				goto cleanup;

			lMatrix = (char*) malloc(lStrLen + 1);
			if (!lMatrix)
				goto cleanup;
			lStrFread = fread(lMatrix, 1, lStrLen, lFile);
			fclose(lFile);
			lFile = nullptr;
			if (lStrLen != lStrFread)
				goto cleanup;

			lMatrix[lStrLen] = 0;
			lCurrentPtr = lMatrix;

			/* replace ',' by 0 */
			while (*lCurrentPtr != 0) {
				if (*lCurrentPtr == ' ') {
					*lCurrentPtr = 0;
					++lNbComp;
				}
				++lCurrentPtr;
			}
			++lNbComp;
			lCurrentPtr = lMatrix;

			lNbComp = (int) (sqrt(4 * lNbComp + 1) / 2. - 0.5);
			lMctComp = lNbComp * lNbComp;
			lTotalComp = lMctComp + lNbComp;
			lSpace = (float*) malloc((size_t) lTotalComp * sizeof(float));
			if (lSpace == nullptr) {
				free(lMatrix);
				return 1;
			}
			lCurrentDoublePtr = lSpace;
			for (i2 = 0; i2 < lMctComp; ++i2) {
				lStrLen = strlen(lCurrentPtr) + 1;
				*lCurrentDoublePtr++ = (float) atof(lCurrentPtr);
				lCurrentPtr += lStrLen;
			}

			int_ptr = (int*) lCurrentDoublePtr;
			for (i2 = 0; i2 < lNbComp; ++i2) {
				lStrLen = strlen(lCurrentPtr) + 1;
				*int_ptr++ = atoi(lCurrentPtr);
				lCurrentPtr += lStrLen;
			}

			/* TODO should not be here ! */
			grk_set_MCT(parameters, lSpace, (int*) (lSpace + lMctComp),
					(uint32_t) lNbComp);

			rc = 0;
			cleanup: if (!lFile) {
				fclose(lFile);
			}
			free(lSpace);
			free(lMatrix);
			if (rc)
				return false;

		}

		if (roiArg.isSet()) {
			if (sscanf(roiArg.getValue().c_str(), "c=%d,U=%d",
					&parameters->roi_compno, &parameters->roi_shift) != 2) {
				spdlog::error("ROI error !! [-ROI c='compno',U='shift']");
				return 1;
			}
		}

		if (tileOffsetArg.isSet()) {
			if (sscanf(tileOffsetArg.getValue().c_str(), "%d,%d",
					&parameters->cp_tx0, &parameters->cp_ty0) != 2) {
				spdlog::error("-T 'tile offset' argument error !! [-T X0,Y0]");
				return 1;
			}
		}

		if (commentArg.isSet()) {
			std::istringstream f(commentArg.getValue());
			string s;
			while (getline(f, s, '|')) {
				if (s.empty())
					continue;
				if (s.length() > GRK_MAX_COMMENT_LENGTH) {
					spdlog::warn(
							" Comment length {} is greater than maximum comment length {}. Ignoring",
							(uint32_t) s.length(), GRK_MAX_COMMENT_LENGTH);
					continue;
				}
				size_t count = parameters->cp_num_comments;
				if (count == GRK_NUM_COMMENTS_SUPPORTED) {
					spdlog::warn(
							" Grok encoder is limited to {} comments. Ignoring subsequent comments.",
							GRK_NUM_COMMENTS_SUPPORTED);
					break;
				}
				// ISO Latin comment
				parameters->cp_is_binary_comment[count] = false;
				parameters->cp_comment[count] = (char*) grk_buffer_new(
						s.length());
				memcpy(parameters->cp_comment[count], s.c_str(), s.length());
				parameters->cp_comment_len[count] = (uint16_t) s.length();
				parameters->cp_num_comments++;
			}
		}

		if (tpArg.isSet()) {
			parameters->tp_flag = tpArg.getValue();
			parameters->tp_on = 1;

		}
	} catch (ArgException &e)  // catch any exceptions
	{
		cerr << "error: " << e.error() << " for arg " << e.argId() << endl;
	}

	if (img_fol->set_imgdir) {
		if (!(parameters->infile[0] == 0)) {
			spdlog::error("options -ImgDir and -i cannot be used together !!");
			return 1;
		}
		if (!img_fol->set_out_format) {
			spdlog::error(
					"When -ImgDir is used, -OutFor <FORMAT> must be used !!");
			spdlog::error(
					"Only one format allowed! Valid formats are j2k and jp2!!");
			return 1;
		}
		if (!((parameters->outfile[0] == 0))) {
			spdlog::error("options -ImgDir and -o cannot be used together !!");
			spdlog::error("Specify OutputFormat using -OutFor<FORMAT> !!");
			return 1;
		}
	} else {
		if (parameters->cod_format == GRK_UNK_FMT) {
			if (parameters->infile[0] == 0) {
				spdlog::error("Missing input file parameter\n"
						"Example: {} -i image.pgm -o image.j2k", argv[0]);
				spdlog::error("   Help: {} -h", argv[0]);
				return 1;
			}
		}

		if (parameters->outfile[0] == 0) {
			spdlog::error("Missing output file parameter\n"
					"Example: {} -i image.pgm -o image.j2k", argv[0]);
			spdlog::error("   Help: {} -h", argv[0]);
			return 1;
		}
	}

	if ((parameters->decod_format == GRK_RAW_FMT
			&& parameters->raw_cp.width == 0)
			|| (parameters->decod_format == GRK_RAWL_FMT
					&& parameters->raw_cp.width == 0)) {
		spdlog::error("invalid raw image parameters");
		spdlog::error("Please use the Format option -F:");
		spdlog::error(
				"-F rawWidth,rawHeight,rawComp,rawBitDepth,s/u (Signed/Unsigned)");
		spdlog::error("Example: -i lena.raw -o lena.j2k -F 512,512,3,8,u");
		return 1;
	}

	if ((parameters->cp_disto_alloc || parameters->cp_fixed_quality)
			&& (!(parameters->cp_disto_alloc ^ parameters->cp_fixed_quality))) {
		spdlog::error("options -r and -q cannot be used together !!");
		return 1;
	}

	/* if no rate entered, lossless by default */
	if (parameters->tcp_numlayers == 0) {
		parameters->tcp_rates[0] = 0;
		parameters->tcp_numlayers = 1;
		parameters->cp_disto_alloc = 1;
	}

	if ((parameters->cp_tx0 > 0
			&& parameters->cp_tx0 > parameters->image_offset_x0)
			|| (parameters->cp_ty0 > 0
					&& parameters->cp_ty0 > parameters->image_offset_y0)) {
		spdlog::error(
				"Tile offset cannot be greater than image offset : TX0({})<=IMG_X0({}) TYO({})<=IMG_Y0({}) ",
				parameters->cp_tx0, parameters->image_offset_x0,
				parameters->cp_ty0, parameters->image_offset_y0);
		return 1;
	}

	for (uint32_t i = 0; i < parameters->numpocs; i++) {
		if (parameters->POC[i].prg == -1) {
			spdlog::error(
					"Unrecognized progression order in option -P (POC n {}) [LRCP, RLCP, RPCL, PCRL, CPRL] !!",
					i + 1);
		}
	}

	/* If subsampled image is provided, automatically disable MCT */
	if (((parameters->decod_format == GRK_RAW_FMT)
			|| (parameters->decod_format == GRK_RAWL_FMT))
			&& (((parameters->raw_cp.numcomps > 1)
					&& ((parameters->raw_cp.comps[1].dx > 1)
							|| (parameters->raw_cp.comps[1].dy > 1)))
					|| ((parameters->raw_cp.numcomps > 2)
							&& ((parameters->raw_cp.comps[2].dx > 1)
									|| (parameters->raw_cp.comps[2].dy > 1))))) {
		parameters->tcp_mct = 0;
	}

	if (parameters->tcp_mct == 2 && !parameters->mct_data) {
		spdlog::error(
				"Custom MCT has been set but no array-based MCT has been provided.");
		return false;
	}

	return 0;
}
struct CompressInitParams {
	CompressInitParams() :
			initialized(false) {

		plugin_path[0] = 0;

		/* Initialize indexfilename and img_fol */
		*indexfilename = 0;

		memset(&img_fol, 0, sizeof(img_fol));
		memset(&out_fol, 0, sizeof(out_fol));

	}

	~CompressInitParams() {
		for (size_t i = 0; i < parameters.cp_num_comments; ++i) {
			if (parameters.cp_comment[i])
				grk_buffer_delete((uint8_t*) parameters.cp_comment[i]);
		}
		if (parameters.raw_cp.comps)
			free(parameters.raw_cp.comps);
		if (img_fol.imgdirpath)
			free(img_fol.imgdirpath);
		if (out_fol.imgdirpath)
			free(out_fol.imgdirpath);

	}
	bool initialized;

	grk_cparameters parameters; /* compression parameters */

	char indexfilename[GRK_PATH_LEN]; /* index file name */
	char plugin_path[GRK_PATH_LEN];

	grk_img_fol img_fol;
	grk_img_fol out_fol;

};

static int plugin_main(int argc, char **argv, CompressInitParams *initParams);

// returns 0 if failed, 1 if succeeded, 
// and 2 if file is not suitable for compression
static int compress(std::string image_filename, CompressInitParams *initParams,
		uint8_t tcp_mct, uint32_t rateControlAlgorithm) {
	//clear for next file encode
	initParams->parameters.write_capture_resolution_from_file = false;
	// don't reset format if reading from STDIN
	if (initParams->parameters.infile[0])
		initParams->parameters.decod_format = GRK_UNK_FMT;

	//restore cached settings
	initParams->parameters.tcp_mct = tcp_mct;
	initParams->parameters.rateControlAlgorithm = rateControlAlgorithm;

	if (initParams->img_fol.set_imgdir) {
		if (get_next_file(image_filename, &initParams->img_fol,
				initParams->out_fol.set_imgdir ?
						&initParams->out_fol : &initParams->img_fol,
				&initParams->parameters)) {
			return 2;
		}
	}
	grk_plugin_encode_user_callback_info callbackInfo;
	memset(&callbackInfo, 0, sizeof(grk_plugin_encode_user_callback_info));
	callbackInfo.encoder_parameters = &initParams->parameters;
	callbackInfo.image = nullptr;
	callbackInfo.output_file_name = initParams->parameters.outfile;
	callbackInfo.input_file_name = initParams->parameters.infile;

	return plugin_compress_callback(&callbackInfo) ? 1 : 0;
}

/* -------------------------------------------------------------------------- */
/**
 * GRK_COMPRESS MAIN
 */
/* -------------------------------------------------------------------------- */
int main(int argc, char **argv) {
	CompressInitParams initParams;
	int success = 0;
	try {
		// try to encode with plugin
		int rc = plugin_main(argc, argv, &initParams);

		// return immediately if either 
		// initParams was not initialized (something was wrong with command line params)
		// or
		// plugin was successful
		if (!initParams.initialized)
			return 1;
		if (!rc)
			return 0;
		size_t num_compressed_files = 0;

		//cache certain settings
		auto tcp_mct = initParams.parameters.tcp_mct;
		auto rateControlAlgorithm = initParams.parameters.rateControlAlgorithm;
		auto start = std::chrono::high_resolution_clock::now();

		if (!initParams.img_fol.set_imgdir) {
			auto rc = compress("", &initParams, tcp_mct, rateControlAlgorithm);
			if (rc == 0) {
				success = 1;
				goto cleanup;
			}
			num_compressed_files++;
		} else {
			auto dir = opendir(initParams.img_fol.imgdirpath);
			if (!dir) {
				spdlog::error("Could not open Folder {}",
						initParams.img_fol.imgdirpath);
				success = 1;
				goto cleanup;
			}
			struct dirent *content = nullptr;
			while ((content = readdir(dir)) != nullptr) {
				if (strcmp(".", content->d_name) == 0
						|| strcmp("..", content->d_name) == 0)
					continue;
				auto rc = compress(content->d_name, &initParams, tcp_mct,
						rateControlAlgorithm);
				if (rc == 1)
					num_compressed_files++;
			}
			closedir(dir);
		}
		auto finish = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> elapsed = finish - start;

		if (num_compressed_files) {
			spdlog::info("encode time: {} ms",
					(elapsed.count() * 1000) / (double) num_compressed_files);
		}
	} catch (std::bad_alloc &ba) {
		spdlog::error(" Out of memory. Exiting.");
		success = 1;
		goto cleanup;
	}
	cleanup: grk_deinitialize();
	return success;

}

grk_img_fol img_fol_plugin, out_fol_plugin;

static bool plugin_compress_callback(
		grk_plugin_encode_user_callback_info *info) {
	grk_cparameters *parameters = info->encoder_parameters;
	bool bSuccess = true;
	grk_stream *stream = nullptr;
	grk_codec *codec = nullptr;
	grk_image *image = info->image;
	char outfile[3 * GRK_PATH_LEN];
	char temp_ofname[GRK_PATH_LEN];
	bool createdImage = false;
	bool inMemoryCompression = false;

	// get output file
	outfile[0] = 0;
	if (info->output_file_name && info->output_file_name[0]) {
		if (info->outputFileNameIsRelative) {
			strcpy(temp_ofname, get_file_name((char*) info->output_file_name));
			if (img_fol_plugin.set_out_format) {
				sprintf(outfile, "%s%s%s.%s",
						out_fol_plugin.imgdirpath ?
								out_fol_plugin.imgdirpath :
								img_fol_plugin.imgdirpath,
						grk::get_path_separator(), temp_ofname,
						img_fol_plugin.out_format);
			}
		} else {
			strcpy(outfile, info->output_file_name);
		}
	} else {
		bSuccess = false;
		goto cleanup;
	}

	if (!image) {
		if (parameters->decod_format == GRK_UNK_FMT) {
			int fmt = get_file_format((char*) info->input_file_name);
			if (fmt <= GRK_UNK_FMT) {
				bSuccess = false;
				goto cleanup;
			}
			parameters->decod_format = (GRK_SUPPORTED_FILE_FMT) fmt;
			if (!isDecodedFormatSupported(parameters->decod_format)) {
				bSuccess = false;
				goto cleanup;
			}

		}
		/* decode the source image */
		/* ----------------------- */

		switch (info->encoder_parameters->decod_format) {
		case GRK_PGX_FMT: {
			PGXFormat pgx;
			image = pgx.decode(info->input_file_name, info->encoder_parameters);
			if (!image) {
				spdlog::error("Unable to load pgx file");
				bSuccess = false;
				goto cleanup;
			}
		}
			break;

		case GRK_PXM_FMT: {
			PNMFormat pnm(false);
			image = pnm.decode(info->input_file_name, info->encoder_parameters);
			if (!image) {
				spdlog::error("Unable to load pnm file");
				bSuccess = false;
				goto cleanup;
			}
		}
			break;

		case GRK_BMP_FMT: {
			BMPFormat bmp;
			image = bmp.decode(info->input_file_name, info->encoder_parameters);
			if (!image) {
				spdlog::error("Unable to load bmp file");
				bSuccess = false;
				goto cleanup;
			}
		}
			break;

#ifdef GROK_HAVE_LIBTIFF
		case GRK_TIF_FMT: {
			TIFFFormat tif;
			image = tif.decode(info->input_file_name, info->encoder_parameters);
			if (!image) {
				spdlog::error("Unable to load tiff file");
				bSuccess = false;
				goto cleanup;
			}
		}
			break;
#endif /* GROK_HAVE_LIBTIFF */

		case GRK_RAW_FMT: {
			RAWFormat raw(true);
			image = raw.decode(info->input_file_name, info->encoder_parameters);
			if (!image) {
				spdlog::error("Unable to load raw file");
				bSuccess = false;
				goto cleanup;
			}
		}
			break;

		case GRK_RAWL_FMT: {
			RAWFormat raw(false);
			image = raw.decode(info->input_file_name, info->encoder_parameters);
			if (!image) {
				spdlog::error("Unable to load raw file");
				bSuccess = false;
				goto cleanup;
			}
		}
			break;

		case GRK_TGA_FMT: {
			TGAFormat tga;
			image = tga.decode(info->input_file_name, info->encoder_parameters);
			if (!image) {
				spdlog::error("Unable to load tga file");
				bSuccess = false;
				goto cleanup;
			}
		}
			break;

#ifdef GROK_HAVE_LIBPNG
		case GRK_PNG_FMT: {
			PNGFormat png;
			image = png.decode(info->input_file_name, info->encoder_parameters);
			if (!image) {
				spdlog::error("Unable to load png file");
				bSuccess = false;
				goto cleanup;
			}
		}
			break;
#endif /* GROK_HAVE_LIBPNG */

#ifdef GROK_HAVE_LIBJPEG
		case GRK_JPG_FMT: {
			JPEGFormat jpeg;
			image = jpeg.decode(info->input_file_name,
					info->encoder_parameters);
			if (!image) {
				spdlog::error("Unable to load jpeg file");
				bSuccess = false;
				goto cleanup;
			}
		}
			break;
#endif /* GROK_HAVE_LIBPNG */
		default: {
			spdlog::error("Unsupported input file format {}",
					info->encoder_parameters->decod_format);
			bSuccess = false;
			goto cleanup;
		}
			break;
		}

		/* Can happen if input file is TIFF or PNG
		 * and GROK_HAVE_LIBTIF or GROK_HAVE_LIBPNG is undefined
		 */
		if (!image) {
			spdlog::error("Unable to load file: no image generated.");
			bSuccess = false;
			goto cleanup;
		}
		createdImage = true;
	}

	if (inMemoryCompression) {
		auto fp = fopen(info->input_file_name, "rb");
		if (!fp) {
			spdlog::error("grk_compress: unable to open file {} for reading",
					info->input_file_name);
			bSuccess = false;
			goto cleanup;
		}

		auto rc = fseek(fp, 0, SEEK_END);
		if (rc == -1) {
			spdlog::error("grk_compress: unable to seek on file {}",
					info->input_file_name);
			if (fp)
				fclose(fp);
			bSuccess = false;
			goto cleanup;
		}
		auto fileLength = ftell(fp);
		if (fileLength == -1) {
			if (fp)
				fclose(fp);
			bSuccess = false;
			goto cleanup;
		}
		if (fp)
			fclose(fp);

		if (fileLength) {
			//  option to write to buffer, assuming one knows how large compressed stream will be 
			uint64_t imageSize = (((image->x1 - image->x0)
					* (image->y1 - image->y0) * image->numcomps
					* ((image->comps[0].prec + 7) / 8)) * 3) / 2;
			info->compressBufferLen =
					(size_t) fileLength > imageSize ?
							(size_t) fileLength : imageSize;
			info->compressBuffer = new uint8_t[info->compressBufferLen];
		}
	}

	// limit to 16 bit precision
	for (uint32_t i = 0; i < image->numcomps; ++i) {
		if (image->comps[i].prec > 16) {
			spdlog::error("Precision = {} not supported:",
					image->comps[i].prec);
			bSuccess = false;
			goto cleanup;
		}
	}

	/* Decide if MCT should be used */
	if (parameters->tcp_mct == 255) { /* mct mode has not been set in commandline */
		parameters->tcp_mct = (image->numcomps >= 3) ? 1 : 0;
	} else { /* mct mode has been set in commandline */
		if ((parameters->tcp_mct == 1) && (image->numcomps < 3)) {
			spdlog::error("RGB->YCC conversion cannot be used:");
			spdlog::error("Input image has less than 3 components");
			bSuccess = false;
			goto cleanup;
		}
		if ((parameters->tcp_mct == 2) && (!parameters->mct_data)) {
			spdlog::error("Custom MCT has been set but no array-based MCT");
			spdlog::error("has been provided.");
			bSuccess = false;
			goto cleanup;
		}
	}

	// set default algorithm 
	if (parameters->rateControlAlgorithm == 255) {
		parameters->rateControlAlgorithm = 0;
	}

	if (GRK_IS_IMF(parameters->rsiz) && parameters->framerate > 0) {
		const int mainlevel = GRK_GET_MAINLEVEL(parameters->rsiz);
		if (mainlevel > 0 && mainlevel <= GRK_MAINLEVEL_MAX) {
			const int limitMSamplesSec[] = { 0,
			GRK_MAINLEVEL_1_MSAMPLESEC,
			GRK_MAINLEVEL_2_MSAMPLESEC,
			GRK_MAINLEVEL_3_MSAMPLESEC,
			GRK_MAINLEVEL_4_MSAMPLESEC,
			GRK_MAINLEVEL_5_MSAMPLESEC,
			GRK_MAINLEVEL_6_MSAMPLESEC,
			GRK_MAINLEVEL_7_MSAMPLESEC,
			GRK_MAINLEVEL_8_MSAMPLESEC,
			GRK_MAINLEVEL_9_MSAMPLESEC,
			GRK_MAINLEVEL_10_MSAMPLESEC,
			GRK_MAINLEVEL_11_MSAMPLESEC };
			uint32_t avgcomponents = image->numcomps;
			if (image->numcomps == 3 && image->comps[1].dx == 2
					&& image->comps[1].dy == 2) {
				avgcomponents = 2;
			}
			double msamplespersec = (double) image->x1 * image->y1
					* avgcomponents * parameters->framerate / 1e6;
			if (msamplespersec > limitMSamplesSec[mainlevel]) {
				spdlog::warn("MSamples/sec is {}, whereas limit is {}.",
						msamplespersec, limitMSamplesSec[mainlevel]);
			}
		}
	}

	if (info->compressBuffer) {
		// let stream clean up compress buffer
		stream = grk_stream_create_mem_stream(info->compressBuffer,
				info->compressBufferLen, true, false);
	} else {
		stream = grk_stream_create_file_stream(outfile, 32 * 1024 * 1024,
				false);
	}
	if (!stream) {
		spdlog::error("failed to create stream");
		bSuccess = false;
		goto cleanup;
	}

	switch (parameters->cod_format) {
	case GRK_J2K_FMT: /* JPEG-2000 codestream */
	{
		/* Get a decoder handle */
		codec = grk_create_compress(GRK_CODEC_J2K, stream);
		break;
	}
	case GRK_JP2_FMT: /* JPEG 2000 compressed image data */
	{
		/* Get a decoder handle */
		codec = grk_create_compress(GRK_CODEC_JP2, stream);
		break;
	}
	default:
		bSuccess = false;
		goto cleanup;
	}

	/* catch events using our callbacks and give a local context */
	if (parameters->verbose) {
		grk_set_info_handler(info_callback, nullptr);
		grk_set_warning_handler(warning_callback, nullptr);
	}
	grk_set_error_handler(error_callback, nullptr);

	if (!grk_setup_encoder(codec, parameters, image)) {
		spdlog::error("failed to encode image: grk_setup_encoder");
		bSuccess = false;
		goto cleanup;
	}

	/* encode the image */
	bSuccess = grk_start_compress(codec, image);
	if (!bSuccess) {
		spdlog::error("failed to encode image: grk_start_compress");
		bSuccess = false;
		goto cleanup;
	}

	bSuccess = grk_encode_with_plugin(codec, info->tile);
	if (!bSuccess) {
		spdlog::error("failed to encode image: grk_encode");
		bSuccess = false;
		goto cleanup;
	}

	bSuccess = bSuccess && grk_end_compress(codec);
	if (!bSuccess) {
		spdlog::error("failed to encode image: grk_end_compress");
		bSuccess = false;
		goto cleanup;
	}
	if (info->compressBuffer) {
		auto fp = fopen(outfile, "wb");
		if (!fp) {
			spdlog::error("Buffer compress: failed to open file {} for writing",
					outfile);
		} else {
			auto len = grk_stream_get_write_mem_stream_length(stream);
			size_t written = fwrite(info->compressBuffer, 1, len, fp);
			if (written != len) {
				spdlog::error(
						"Buffer compress: only {} bytes written out of {} total",
						len, written);
			}
			if (fp)
				fclose(fp);
		}
	}
	cleanup: if (stream)
		grk_stream_destroy(stream);
	if (codec)
		grk_destroy_codec(codec);
	if (createdImage)
		grk_image_destroy(image);
	if (!bSuccess) {
		spdlog::error("failed to encode image");
		if (parameters->outfile[0])
			remove(actual_path(parameters->outfile));
	}
	return bSuccess;
}

static int plugin_main(int argc, char **argv, CompressInitParams *initParams) {
	if (!initParams)
		return 1;

	grk_dircnt *dirptr = nullptr;
	int32_t success = 0;
	uint32_t num_images, imageno;
	bool isBatch = false;
	uint32_t state= 0;

	/* set encoding parameters to default values */
	grk_set_default_encoder_parameters(&initParams->parameters);



	/* parse input and get user encoding parameters */
	initParams->parameters.tcp_mct = 255; /* This will be set later according to the input image or the provided option */
	initParams->parameters.rateControlAlgorithm = 255;
	if (parse_cmdline_encoder_ex(argc, argv, &initParams->parameters,
			&initParams->img_fol, &initParams->out_fol,
			initParams->indexfilename, sizeof(initParams->indexfilename),
			initParams->plugin_path) == 1) {
		success = 1;
		goto cleanup;
	}
	isBatch = initParams->img_fol.imgdirpath &&  initParams->out_fol.imgdirpath;
	state = grk_plugin_get_debug_state();


	if (!initParams->parameters.verbose)
		spdlog::set_level(spdlog::level::level_enum::err);

#ifdef GROK_HAVE_LIBTIFF
	tiffSetErrorAndWarningHandlers(initParams->parameters.verbose);
#endif

	initParams->initialized = true;

	// loads plugin but does not actually create codec
	if (!grk_initialize(initParams->plugin_path,
			initParams->parameters.numThreads)) {
		success = 1;
		goto cleanup;
	}

	img_fol_plugin = initParams->img_fol;
	out_fol_plugin = initParams->out_fol;

	// create codec
	grk_plugin_init_info initInfo;
	initInfo.deviceId = initParams->parameters.deviceId;
	initInfo.verbose = initParams->parameters.verbose;
	if (!grk_plugin_init(initInfo)) {
		success = 1;
		goto cleanup;

	}
	if ((state & GRK_PLUGIN_STATE_DEBUG)
			|| (state & GRK_PLUGIN_STATE_PRE_TR1)) {
		isBatch = 0;
	}
	if (isBatch) {
		setup_signal_handler();
		success = grk_plugin_batch_encode(initParams->img_fol.imgdirpath,
				initParams->out_fol.imgdirpath, &initParams->parameters,
				plugin_compress_callback);
		// if plugin successfully begins batch encode, then wait for batch to complete
		if (success == 0) {
			uint32_t slice = 100;	//ms
			uint32_t slicesPerSecond = 1000 / slice;
			uint32_t seconds = initParams->parameters.duration;
			if (!seconds)
				seconds = UINT_MAX;
			for (uint32_t i = 0U; i < seconds * slicesPerSecond; ++i) {
				batch_sleep(1);
				if (grk_plugin_is_batch_complete()) {
					break;
				}
			}
			grk_plugin_stop_batch_encode();
		}
	} else {
		// loop through all files
		/* Read directory if necessary */
		if (initParams->img_fol.set_imgdir) {
			num_images = get_num_images(initParams->img_fol.imgdirpath);
			if (!num_images)
				goto cleanup;
			dirptr = (grk_dircnt*) malloc(sizeof(grk_dircnt));
			if (!dirptr) {
				success = 1;
				goto cleanup;
			}
			if (dirptr) {
				dirptr->filename_buf = (char*) malloc(
						num_images * GRK_PATH_LEN * sizeof(char));
				if (!dirptr->filename_buf) {
					success = 1;
					goto cleanup;
				}
				dirptr->filename = (char**) malloc(num_images * sizeof(char*));
				if (!dirptr->filename) {
					success = 1;
					goto cleanup;
				}
				for (uint32_t i = 0; i < num_images; i++) {
					dirptr->filename[i] = dirptr->filename_buf
							+ i * GRK_PATH_LEN;
				}
			}
			if (load_images(dirptr, initParams->img_fol.imgdirpath) == 1) {
				goto cleanup;
			}
			if (num_images == 0) {
				spdlog::error("Folder is empty");
				goto cleanup;
			}
		} else {
			num_images = 1;
		}
		//cache certain settings
		auto tcp_mct = initParams->parameters.tcp_mct;
		auto rateControlAlgorithm = initParams->parameters.rateControlAlgorithm;
		/*Encoding image one by one*/
		for (imageno = 0; imageno < num_images; imageno++) {
			if (initParams->img_fol.set_imgdir) {
				if (get_next_file(dirptr->filename[imageno],
						&initParams->img_fol,
						initParams->out_fol.imgdirpath ?
								&initParams->out_fol : &initParams->img_fol,
						&initParams->parameters)) {
					continue;
				}
			}
			//restore cached settings
			initParams->parameters.tcp_mct = tcp_mct;
			initParams->parameters.rateControlAlgorithm = rateControlAlgorithm;
			success = grk_plugin_encode(&initParams->parameters,
					plugin_compress_callback);
			if (success != 0)
				break;
		}
	}

	cleanup: if (dirptr) {
		if (dirptr->filename_buf)
			free(dirptr->filename_buf);
		if (dirptr->filename)
			free(dirptr->filename);
		free(dirptr);
	}
	return success;
}
