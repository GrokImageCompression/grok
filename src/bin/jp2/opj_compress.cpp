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
#include "windirent.h"
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

#include "opj_apps_config.h"
#include "openjpeg.h"
#include "RAWFormat.h"
#include "PNMFormat.h"
#include "PGXFormat.h"
#include "TGAFormat.h"
#include "BMPFormat.h"
#include "JPEGFormat.h"
#include "TIFFFormat.h"
#include "PNGFormat.h"
#include "convert.h"
#include "format_defs.h"
#include "grok_string.h"
#include "color.h"

#include <float.h>
#include <math.h>
#include <assert.h>
#include <limits.h>
#include <cstddef>
#include <cstdlib>
#define TCLAP_NAMESTARTSTRING "-"
#include "tclap/CmdLine.h"

void exit_func() {
	grok_plugin_stop_batch_encode();
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
void sig_handler(int signum){
	(void)signum;
	exit_func();
}
#endif 

void setup_signal_handler()
{
#ifdef  _WIN32
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)sig_handler, TRUE);
#else
	struct sigaction sa;
	sa.sa_handler = &sig_handler;
	sigfillset(&sa.sa_mask);
	sigaction(SIGHUP, &sa, NULL);
#endif  
}



using namespace TCLAP;
using namespace std;


static bool plugin_compress_callback(grok_plugin_encode_user_callback_info_t* info);

static void encode_help_display(void)
{
    fprintf(stdout,"\nThis is the opj_compress utility from the Grok project.\n"
            "It compresses various image formats with the JPEG 2000 algorithm.\n"
            "It has been compiled against openjp2 library v%s.\n\n",opj_version());

    fprintf(stdout,"Default encoding options:\n");
    fprintf(stdout,"-------------------------\n");
    fprintf(stdout,"\n");
    fprintf(stdout," * Lossless\n");
    fprintf(stdout," * 1 tile\n");
    fprintf(stdout," * RGB->YCC conversion if there are 3 colour components\n");
    fprintf(stdout," * Size of precinct : 2^15 x 2^15 (i.e. 1 precinct)\n");
    fprintf(stdout," * Size of code-block : 64 x 64\n");
    fprintf(stdout," * Number of resolutions: 6\n");
    fprintf(stdout," * No SOP marker in the codestream\n");
    fprintf(stdout," * No EPH marker in the codestream\n");
    fprintf(stdout," * No sub-sampling in x or y direction\n");
    fprintf(stdout," * No mode switch activated\n");
    fprintf(stdout," * Progression order: LRCP\n");
    fprintf(stdout," * No ROI upshifted\n");
    fprintf(stdout," * No offset of the origin of the image\n");
    fprintf(stdout," * No offset of the origin of the tiles\n");
    fprintf(stdout," * Reversible DWT 5-3\n");
    fprintf(stdout,"\n");

    fprintf(stdout,"Note:\n");
    fprintf(stdout,"-----\n");
    fprintf(stdout,"\n");
    fprintf(stdout,"The markers written to the main_header are : SOC SIZ COD QCD COM.\n");
    fprintf(stdout,"COD and QCD never appear in the tile_header.\n");
    fprintf(stdout,"\n");

    fprintf(stdout,"Parameters:\n");
    fprintf(stdout,"-----------\n");
    fprintf(stdout,"\n");
    fprintf(stdout,"Required Parameters (except with -h):\n");
    fprintf(stdout,"One of the two options -ImgDir or -i must be used\n");
    fprintf(stdout,"\n");
    fprintf(stdout,"[-i|-InputFile] <file>\n");
    fprintf(stdout,"    Input file\n");
    fprintf(stdout,"    Known extensions are <PBM|PGM|PPM|PNM|PAM|PGX|PNG|BMP|TIF|RAW|RAWL|TGA>\n");
    fprintf(stdout,"    If used, '-o <file>' must be provided\n");
    fprintf(stdout,"[-o|-OutputFile] <compressed file>\n");
    fprintf(stdout,"    Output file (accepted extensions are j2k or jp2).\n");
    fprintf(stdout,"[-y|-ImgDir] <dir>\n");
    fprintf(stdout,"    Image file Directory path (example ../Images) \n");
    fprintf(stdout,"    When using this option -OutFor must be used\n");
    fprintf(stdout,"[-O|-OutFor] <J2K|J2C|JP2>\n");
    fprintf(stdout,"    Output format for compressed files.\n");
    fprintf(stdout,"    Required only if -ImgDir is used\n");
	fprintf(stdout, "[-K|-InFor] <pbm|pgm|ppm|pnm|pam|pgx|png|bmp|tif|raw|rawl|tga>\n");
	fprintf(stdout, "    Input format. Will override file tag.\n");
    fprintf(stdout,"[-F|-Raw] <width>,<height>,<ncomp>,<bitdepth>,{s,u}@<dx1>x<dy1>:...:<dxn>x<dyn>\n");
    fprintf(stdout,"    Characteristics of the raw input image\n");
    fprintf(stdout,"    If subsampling is omitted, 1x1 is assumed for all components\n");
    fprintf(stdout,"      Example: -F 512,512,3,8,u@1x1:2x2:2x2\n");
    fprintf(stdout,"               for raw 512x512 image with 4:2:0 subsampling\n");
    fprintf(stdout,"    Required only if RAW or RAWL input file is provided.\n");
    fprintf(stdout,"\n");
    fprintf(stdout,"Optional Parameters:\n");
    fprintf(stdout,"\n");
    fprintf(stdout,"[-h|-help]\n");
    fprintf(stdout,"    Display the help information.\n");
	fprintf(stdout, "[-a|-OutDir] <output directory>\n");
	fprintf(stdout, "    Output directory where compressed files are stored.\n");
    fprintf(stdout,"[-r|-CompressionRatios] <compression ratio>,<compression ratio>,...\n");
    fprintf(stdout,"    Different compression ratios for successive layers.\n");
    fprintf(stdout,"    The rate specified for each quality level is the desired\n");
    fprintf(stdout,"    compression factor.\n");
    fprintf(stdout,"    Decreasing ratios required.\n");
    fprintf(stdout,"      Example: -r 20,10,1 means \n");
    fprintf(stdout,"            quality layer 1: compress 20x, \n");
    fprintf(stdout,"            quality layer 2: compress 10x \n");
    fprintf(stdout,"            quality layer 3: compress lossless\n");
    fprintf(stdout,"    Options -r and -q cannot be used together.\n");
    fprintf(stdout,"[-q|-Quality] <psnr value>,<psnr value>,<psnr value>,...\n");
    fprintf(stdout,"    Different psnr for successive layers (-q 30,40,50).\n");
    fprintf(stdout,"    Increasing PSNR values required.\n");
    fprintf(stdout,"    Options -r and -q cannot be used together.\n");
	fprintf(stdout, "[-A|-RateControlAlgorithm] <0|1>\n");
	fprintf(stdout, "    Select algorithm used for rate control\n");
	fprintf(stdout, "    0: Bisection search for optimal threshold using all code passes in code blocks. (default) (slightly higher PSRN than algorithm 1)\n");
	fprintf(stdout, "    1: Bisection search for optimal threshold using only feasible truncation points, on convex hull.\n");
    fprintf(stdout,"[-n|-Resolutions] <number of resolutions>\n");
    fprintf(stdout,"    Number of resolutions.\n");
    fprintf(stdout,"    It corresponds to the number of DWT decompositions +1. \n");
    fprintf(stdout,"    Default: 6.\n");
    fprintf(stdout,"[-b|-CodeBlockDim] <cblk width>,<cblk height>\n");
    fprintf(stdout,"    Code-block dimensions. The dimensions must respect the constraint \n");
    fprintf(stdout,"    defined in the JPEG-2000 standard (no dimension smaller than 4 \n");
    fprintf(stdout,"    or greater than 1024, no code-block with more than 4096 coefficients).\n");
    fprintf(stdout,"    The maximum value permitted is 64x64. \n");
    fprintf(stdout,"    Default: 64x64.\n");
    fprintf(stdout,"[-c|-PrecinctDims] [<prec width>,<prec height>],[<prec width>,<prec height>],...\n");
    fprintf(stdout,"    Precinct size. Values specified must be power of 2. \n");
    fprintf(stdout,"    Multiple records may be supplied, in which case the first record refers\n");
    fprintf(stdout,"    to the highest resolution level and subsequent records to lower \n");
    fprintf(stdout,"    resolution levels. The last specified record is right-shifted for each \n");
    fprintf(stdout,"    remaining lower resolution levels.\n");
    fprintf(stdout,"    Default: 215x215 at each resolution.\n");
    fprintf(stdout,"[-t|-TileDim] <tile width>,<tile height>\n");
    fprintf(stdout,"    Tile dimensions.\n");
    fprintf(stdout,"    Default: the dimension of the whole image, thus only one tile.\n");
    fprintf(stdout,"[-p|-ProgressionOrder] <LRCP|RLCP|RPCL|PCRL|CPRL>\n");
    fprintf(stdout,"    Progression order.\n");
    fprintf(stdout,"    Default: LRCP.\n");
/*
    fprintf(stdout,"[-s|-Subsampling]  <subX,subY>\n");
    fprintf(stdout,"    Subsampling factor.\n");
    fprintf(stdout,"    Subsampling bigger than 2 can produce error\n");
    fprintf(stdout,"    Default: no subsampling.\n");
*/
    fprintf(stdout,"[-P|-POC] <progression order change>/<progression order change>/...\n");
    fprintf(stdout,"    Progression order change.\n");
    fprintf(stdout,"    The syntax of a progression order change is the following:\n");
    fprintf(stdout,"    T<tile>=<resStart>,<compStart>,<layerEnd>,<resEnd>,<compEnd>,<progOrder>\n");
    fprintf(stdout,"      Example: -POC T1=0,0,1,5,3,CPRL/T1=5,0,1,6,3,CPRL\n");
    fprintf(stdout,"[-S|-SOP]\n");
    fprintf(stdout,"    Write SOP marker before each packet.\n");
    fprintf(stdout,"[-E|-EPH]\n");
    fprintf(stdout,"    Write EPH marker after each header packet.\n");
    fprintf(stdout,"[-M|-Mode] <key value>\n");
    fprintf(stdout,"    Mode switch.\n");
    fprintf(stdout,"    [1=BYPASS(LAZY) 2=RESET 4=RESTART(TERMALL)\n");
    fprintf(stdout,"    8=VSC 16=ERTERM(SEGTERM) 32=SEGMARK(SEGSYM)]\n");
    fprintf(stdout,"    Indicate multiple modes by adding their values.\n");
    fprintf(stdout,"      Example: RESTART(4) + RESET(2) + SEGMARK(32) => -M 38\n");
    fprintf(stdout,"[-u|-TP] <R|L|C>\n");
    fprintf(stdout,"    Divide packets of every tile into tile-parts.\n");
    fprintf(stdout,"    Division is made by grouping Resolutions (R), Layers (L)\n");
    fprintf(stdout,"    or Components (C).\n");
    fprintf(stdout,"[-R|-ROI] c=<component index>,U=<upshifting value>\n");
    fprintf(stdout,"    Quantization indices upshifted for a component. \n");
    fprintf(stdout,"    [WARNING] This option does not implement the usual ROI (Region of Interest).\n");
    fprintf(stdout,"    It should be understood as a 'Component of Interest'. It offers the \n");
    fprintf(stdout,"    possibility to upshift the value of a component during quantization step.\n");
    fprintf(stdout,"    The value after c= is the component number [0, 1, 2, ...] and the value \n");
    fprintf(stdout,"    after U= is the value of upshifting. U must be in the range [0, 37].\n");
    fprintf(stdout,"[-d|-ImageOffset] <image offset X,image offset Y>\n");
    fprintf(stdout,"    Offset of the origin of the image.\n");
    fprintf(stdout,"[-T|-TileOffset] <tile offset X,tile offset Y>\n");
    fprintf(stdout,"    Offset of the origin of the tiles.\n");
    fprintf(stdout,"[-I|-Irreversible\n");
    fprintf(stdout,"    Use the irreversible DWT 9-7.\n");
    fprintf(stdout,"[-Y|-mct] <0|1|2>\n");
    fprintf(stdout,"    Explicitly specifies if a Multiple Component Transform has to be used.\n");
    fprintf(stdout,"    0: no MCT ; 1: RGB->YCC conversion ; 2: custom MCT.\n");
    fprintf(stdout,"    If custom MCT, \"-m\" option has to be used (see hereunder).\n");
    fprintf(stdout,"    By default, RGB->YCC conversion is used if there are 3 components or more,\n");
    fprintf(stdout,"    no conversion otherwise.\n");
    fprintf(stdout,"[-m|-CustomMCT <file>\n");
    fprintf(stdout,"    Use array-based MCT, values are coma separated, line by line\n");
    fprintf(stdout,"    No specific separators between lines, no space allowed between values.\n");
    fprintf(stdout,"    If this option is used, it automatically sets \"-mct\" option to 2.\n");
	fprintf(stdout, "[-Z|-RSIZ] <rsiz>\n");
	fprintf(stdout, "    Profile, main level, sub level and version.\n");
	fprintf(stdout, "	Note: this flag will be ignored if cinema profile flags are used.\n");
	fprintf(stdout,"[-w|-cinema2K] <24|48>\n");
    fprintf(stdout,"    Digital Cinema 2K profile compliant codestream.\n");
    fprintf(stdout,"	Need to specify the frames per second.\n");
    fprintf(stdout,"    Only 24 or 48 fps are currently allowed.\n");
    fprintf(stdout,"[-x|-cinema4K] <24|48>\n");
    fprintf(stdout,"    Digital Cinema 4K profile compliant codestream.\n");
	fprintf(stdout, "	Need to specify the frames per second.\n");
	fprintf(stdout, "    Only 24 or 48 fps are currently allowed.\n");
    fprintf(stdout,"[-C|-Comment] <comment>\n");
    fprintf(stdout,"    Add <comment> in the comment marker segment.\n");
	fprintf(stdout, "[-Q|-CaptureRes] <capture resolution X,capture resolution Y>\n");
	fprintf(stdout, "    Capture resolution in pixels/metre, in double precision.\n");
	fprintf(stdout, "    These values will override the resolution stored in the input image, if present \n");
	fprintf(stdout, "    unless the special values <0,0> are passed in, in which case \n");
	fprintf(stdout, "    the image resolution will be used.\n");
	fprintf(stdout, "[-D|-DisplayRes] <display resolution X,display resolution Y>\n");
	fprintf(stdout, "    Display resolution in pixels/metre, in double precision.\n");
	fprintf(stdout, "[-e|-Repetitions] <number of repetitions>\n");
	fprintf(stdout, "    Number of repetitions, for either a single image, or a folder of images. Default is 1. 0 signifies unlimited repetitions. \n");
	fprintf(stdout, "[-g|-PluginPath] <plugin path>\n");
	fprintf(stdout, "    Path to T1 plugin.\n");
	fprintf(stdout, "[-H|-NumThreads] <number of threads>\n");
	fprintf(stdout, "    Number of threads to use for T1.\n");
	fprintf(stdout, "[-G|-DeviceId] <device ID>\n");
	fprintf(stdout, "    (GPU) Specify which GPU accelerator to run codec on.\n");
	fprintf(stdout, "    A value of -1 will specify all devices.\n");
    fprintf(stdout,"\n");
}

static OPJ_PROG_ORDER give_progression(const char progression[4])
{
    if(strncmp(progression, "LRCP", 4) == 0) {
        return OPJ_LRCP;
    }
    if(strncmp(progression, "RLCP", 4) == 0) {
        return OPJ_RLCP;
    }
    if(strncmp(progression, "RPCL", 4) == 0) {
        return OPJ_RPCL;
    }
    if(strncmp(progression, "PCRL", 4) == 0) {
        return OPJ_PCRL;
    }
    if(strncmp(progression, "CPRL", 4) == 0) {
        return OPJ_CPRL;
    }

    return OPJ_PROG_UNKNOWN;
}

static unsigned int get_num_images(char *imgdirpath)
{
    DIR *dir;
    struct dirent* content;
    unsigned int num_images = 0;

    /*Reading the input images from given input directory*/

    dir= opendir(imgdirpath);
    if(!dir) {
        fprintf(stderr,"[ERROR] Could not open Folder %s\n",imgdirpath);
        return 0;
    }

    num_images=0;
    while((content=readdir(dir))!=nullptr) {
        if(strcmp(".",content->d_name)==0 || strcmp("..",content->d_name)==0 )
            continue;
        num_images++;
    }
    closedir(dir);
    return num_images;
}

static int load_images(dircnt_t *dirptr, char *imgdirpath)
{
    /*Reading the input images from given input directory*/

	DIR * dir= opendir(imgdirpath);
    if(!dir) {
        fprintf(stderr,"[ERROR] Could not open Folder %s\n",imgdirpath);
        return 1;
    } 

	struct dirent* content = nullptr;
	int i = 0;
    while((content=readdir(dir))!=nullptr) {
        if(strcmp(".",content->d_name)==0 || strcmp("..",content->d_name)==0 )
            continue;

        strcpy(dirptr->filename[i],content->d_name);
        i++;
    }
    closedir(dir);
    return 0;
}

static int get_file_format(char *filename)
{
    unsigned int i;
    static const char *extension[] = {
        "pgx", "pnm", "pgm", "ppm", "pbm", "pam", "bmp", "tif", "tiff", "jpg", "raw", "rawl", "tga", "png", "j2k", "jp2", "j2c", "jpc"
    };
    static const int format[] = {
        PGX_DFMT, PXM_DFMT, PXM_DFMT, PXM_DFMT, PXM_DFMT, PXM_DFMT, BMP_DFMT, TIF_DFMT,TIF_DFMT, JPG_DFMT, RAW_DFMT, RAWL_DFMT, TGA_DFMT, PNG_DFMT, J2K_CFMT, JP2_CFMT, J2K_CFMT, J2K_CFMT
    };
    char * ext = strrchr(filename, '.');
    if (ext == nullptr)
        return -1;
    ext++;
    for(i = 0; i < sizeof(format)/sizeof(*format); i++) {
        if(strcasecmp(ext, extension[i]) == 0) {
            return format[i];
        }
    }
    return -1;
}

static char * get_file_name(char *name)
{
    return strtok(name,".");
}

static const char* get_path_separator() {
#ifdef _WIN32
	return "\\";
#else
	return "/";
#endif
}


static char get_next_file(int imageno,
							dircnt_t *dirptr,
							img_fol_t *img_fol,
							img_fol_t *out_fol,
							opj_cparameters_t *parameters){

	std::string image_filename = dirptr->filename[imageno];
	if (parameters->verbose)
		fprintf(stdout, "File Number %d \"%s\"\n", imageno, image_filename.c_str());
	std::string infilename = img_fol->imgdirpath + std::string(get_path_separator()) + image_filename;
	if (parameters->decod_format == -1) {
		parameters->decod_format = get_file_format((char*)infilename.c_str());
		if (parameters->decod_format == -1)
			return 1;
	}
	if (grk::strcpy_s(parameters->infile, sizeof(parameters->infile), infilename.c_str()) != 0) {
		return 1;
	}
	std::string output_root_filename;
	// if we don't find a file tag, then just use the full file name
	auto pos = image_filename.find(".");
	if (pos != std::string::npos)
		output_root_filename = image_filename.substr(0, pos);
	else
		output_root_filename = image_filename;
	if (img_fol->set_out_format == 1) {
		std::string outfilename = out_fol->imgdirpath + std::string(get_path_separator()) + output_root_filename + "." + img_fol->out_format;
		if (grk::strcpy_s(parameters->outfile, sizeof(parameters->outfile), outfilename.c_str()) != 0) {
			return 1;
		}
	}
	return 0;
}

static bool isNonJPEG2000FileFormatSupported(int32_t format) {
	switch (format) {
	case PGX_DFMT:
	case PXM_DFMT:
	case BMP_DFMT:
	case TIF_DFMT:
	case RAW_DFMT:
	case RAWL_DFMT:
	case TGA_DFMT:
	case PNG_DFMT:
	case JPG_DFMT:
		break;
	default:
		return false;
	}
	return true;
}


class GrokOutput : public StdOutput
{
public:
	virtual void usage(CmdLineInterface& c)
	{
		(void)c;
		encode_help_display();
	}
};

/* ------------------------------------------------------------------------------------ */

static bool checkCinema(ValueArg<uint32_t>* arg, uint16_t profile, opj_cparameters_t *parameters) {
	bool isValid = true;
	if (arg->isSet()) {
		int fps = arg->getValue();
		if (fps == 24) {
			parameters->rsiz = profile;
			parameters->max_comp_size = OPJ_CINEMA_24_COMP;
			parameters->max_cs_size = OPJ_CINEMA_24_CS;
		}
		else if (fps == 48) {
			parameters->rsiz = profile;
			parameters->max_comp_size = OPJ_CINEMA_48_COMP;
			parameters->max_cs_size = OPJ_CINEMA_48_CS;
		}
		else {
			isValid = false;
			if (parameters->verbose)
				fprintf(stderr, "[ERROR] Incorrect digital cinema frame rate %d : must be either 24 or 48\n", fps);
		}
	}
	return isValid;
}
static int parse_cmdline_encoder_ex(int argc, 
									char **argv,
									opj_cparameters_t *parameters,
									img_fol_t *img_fol,
									img_fol_t *out_fol,
									char *indexfilename,
									size_t indexfilename_size,
									char* plugin_path) {
	(void)indexfilename;
	(void)indexfilename_size;
	try {

		// Define the command line object.
		CmdLine cmd("Command description message", ' ', opj_version());

		// set the output
		GrokOutput output;
		cmd.setOutput(&output);

		// Kernel build flags:
		// 1 indicates build binary, otherwise load binary
		// 2 indicates generate binaries
		ValueArg<uint32_t> kernelBuildOptionsArg("k", "KernelBuild",
			"Kernel build options",
			false, 0, "unsigned integer", cmd);

		ValueArg<uint32_t> repetitionsArg("e", "Repetitions",
			"Number of encode repetitions, for either a folder or a single file",
			false, 0, "unsigned integer", cmd);

		ValueArg<uint16_t> rsizArg("Z", "RSIZ",
			"RSIZ",
			false, 0, "unsigned integer", cmd);

		ValueArg<uint32_t> cinema2KArg("w", "cinema2K", 
										"Digital cinema 2K profile",
										false,24, "unsigned integer", cmd);
		ValueArg<uint32_t> cinema4KArg("x", "cinema4K",
									"Digital cinema 2K profile",
									false, 24, "unsigned integer", cmd);

		ValueArg<string> imgDirArg("y", "ImgDir",
									"Image directory",
									false, "", "string", cmd);
		ValueArg<string> outDirArg("a", "OutDir",
									"Output directory",
									false, "", "string", cmd);

		ValueArg<string> pluginPathArg("g", "PluginPath",
									"Plugin path",
									false, "", "string", cmd);
		ValueArg<uint32_t> numThreadsArg("H", "NumThreads",
									"Number of threads",
									false, 8, "unsigned integer", cmd);

		ValueArg<int32_t> deviceIdArg("G", "DeviceId",
			"Device ID",
			false, 0, "integer", cmd);

		ValueArg<string> inputFileArg("i", "InputFile",
									"Input file",
									false, "", "string", cmd);
		ValueArg<string> outputFileArg("o", "OutputFile",
									"Output file",
									false, "", "string", cmd);

		ValueArg<string> outForArg("O", "OutFor",
								"Output format",
								false, "", "string", cmd);

		ValueArg<string> inForArg("K", "InFor",
			"InputFormat format",
			false, "", "string", cmd);

		SwitchArg sopArg("S", "SOP",
						"Add SOP markers", cmd);

		SwitchArg ephArg("E", "EPH",
						"Add EPH markers", cmd);

		ValueArg<char> tpArg("u", "TP",
									"Tile part generation",
									false, 0, "char", cmd);

		ValueArg<string> tileOffsetArg("T", "TileOffset",
								"Tile offset",
								false, "", "string", cmd);


		ValueArg<string> pocArg("P", "POC",
								"Progression order changes",
								false, "", "string", cmd);

		ValueArg<string> roiArg("R", "ROI",
								"Region of interest",
								false, "", "string", cmd);

		ValueArg<uint32_t> mctArg("Y", "mct",
							"Multi component transform",
							false, 0, "unsigned integer", cmd);

		ValueArg<string> captureResArg("Q", "CaptureRes",
							"Capture resolution",
							false, "", "string", cmd);

		ValueArg<string> displayResArg("D", "DisplayRes",
							"Display resolution",
							false, "", "string", cmd);

		ValueArg<string> compressionRatiosArg("r", "CompressionRatios",
			"Layer rates expressed as compression ratios",
			false, "", "string", cmd);

		ValueArg<string> qualityArg("q", "Quality",
			"Layer rates expressed as quality",
			false, "", "string", cmd);

		ValueArg<string> rawFormatArg("F", "Raw",
									"Raw image format parameters",
									false, "", "string", cmd);

		ValueArg<string> tilesArg("t", "TileDim",
			"Tile parameters",
			false, "", "string", cmd);

		ValueArg<uint32_t> resolutionArg("n", "Resolutions",
			"Resolution",
			false, 0, "unsigned integer", cmd);

		ValueArg<string> precinctDimArg("c", "PrecinctDim",
			"Precinct dimensions",
			false, "", "string", cmd);

		ValueArg<string> codeBlockDimArg("b", "CodeBlockDim",
			"Code block dimension",
			false, "", "string", cmd);


		ValueArg<string> progressionOrderArg("p", "ProgressionOrder",
			"Progression order",
			false, "", "string", cmd);

		// this flag is currently disabled 
		ValueArg<string> subsamplingFactorArg("s", "SubsamplingFactor",
			"Subsampling factor",
			false, "", "string"/*, cmd*/);

		ValueArg<string> imageOffsetArg("d", "ImageOffset",
			"Image offset in reference grid coordinates",
			false, "", "string", cmd);

		ValueArg<uint32_t> modeArg("M", "Mode",
			"Mode",
			false, 0, "unsigned integer", cmd);

		ValueArg<string> commentArg("C", "Comment",
			"Add a comment",
			false, "", "string", cmd);

		SwitchArg irreversibleArg("I", "Irreversible",
			"Irreversible", cmd);

		ValueArg<string> customMCTArg("m", "CustomMCT",
			"MCT input file",
			false, "", "string", cmd);

		ValueArg<uint32_t> durationArg("z", "Duration",
			"Duration in seconds",
			false, 0, "unsigned integer", cmd);

		ValueArg<uint32_t> rateControlAlgoArg("A", "RateControlAlgorithm",
			"Rate control algorithm",
			false, 0, "unsigned integer", cmd);


		SwitchArg verboseArg("v", "verbose",
			"Verbose", cmd);

		cmd.parse(argc, argv);

		img_fol->set_out_format = 0;
		parameters->raw_cp.rawWidth = 0;

		if (repetitionsArg.isSet()) {
			parameters->repeats = repetitionsArg.getValue();
		}

		if (kernelBuildOptionsArg.isSet()) {
			parameters->kernelBuildOptions = kernelBuildOptionsArg.getValue();
		}

		if (rateControlAlgoArg.isSet()) {
			parameters->rateControlAlgorithm = rateControlAlgoArg.getValue();
		}

		if (numThreadsArg.isSet()) {
			parameters->numThreads = numThreadsArg.getValue();
		}

		if (deviceIdArg.isSet()) {
			parameters->deviceId = deviceIdArg.getValue();
		}


		if (durationArg.isSet()) {
			parameters->duration = durationArg.getValue();
		}

		if (inForArg.isSet()) {
			auto dummy = "dummy." + inForArg.getValue();
			char *infile = (char*)(dummy).c_str();
			parameters->decod_format = get_file_format(infile);
			if (!isNonJPEG2000FileFormatSupported(parameters->decod_format)){
				fprintf(stdout,
					"[WARNING] Ignoring unknown input file format: %s \n"
					"        Known file formats are *.pnm, *.pgm, *.ppm, *.pgx, *png, *.bmp, *.tif, *.jpg, *.raw or *.tga\n",
					infile);
			}
		}

		if (inputFileArg.isSet()) {
			char *infile = (char*)inputFileArg.getValue().c_str();
			if (parameters->decod_format == -1) {
				parameters->decod_format = get_file_format(infile);
				if (!isNonJPEG2000FileFormatSupported(parameters->decod_format)) {
					fprintf(stderr,
						"[ERROR] Unknown input file format: %s \n"
						"        Known file formats are *.pnm, *.pgm, *.ppm, *.pgx, *png, *.bmp, *.tif, *.jpg, *.raw or *.tga\n",
						infile);
					return 1;
				}
			}
			if (grk::strcpy_s(parameters->infile, sizeof(parameters->infile), infile) != 0) {
				return 1;
			}
		}

		if (outForArg.isSet()) {
			char outformat[50];
			char *of = (char*)outForArg.getValue().c_str();
			sprintf(outformat, ".%s", of);
			img_fol->set_out_format = 1;
			parameters->cod_format = get_file_format(outformat);
			switch (parameters->cod_format) {
			case J2K_CFMT:
				img_fol->out_format = "j2k";
				break;
			case JP2_CFMT:
				img_fol->out_format = "jp2";
				break;
			default:
				fprintf(stderr, "[ERROR] Unknown output format image [only j2k, j2c, jp2]!! \n");
				return 1;
			}
		}


		if (outputFileArg.isSet()) {
			char *outfile = (char*)outputFileArg.getValue().c_str();
			parameters->cod_format = get_file_format(outfile);
			switch (parameters->cod_format) {
			case J2K_CFMT:
			case JP2_CFMT:
				break;
			default:
				fprintf(stderr, "[ERROR] Unknown output format image %s [only *.j2k, *.j2c or *.jp2]!! \n", outfile);
				return 1;
			}
		if (grk::strcpy_s(parameters->outfile, sizeof(parameters->outfile), outfile) != 0) {
				return 1;
			}
		}

		if (compressionRatiosArg.isSet()) {
			char *s = (char*)compressionRatiosArg.getValue().c_str();
			parameters->tcp_numlayers = 0;
			while (sscanf(s, "%lf", &parameters->tcp_rates[parameters->tcp_numlayers]) == 1) {
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
					fprintf(stderr, "[ERROR] rates must be listed in descending order\n");
					return 1;
				}
				if (parameters->tcp_rates[i] < 1.0) {
					fprintf(stderr, "[ERROR] rates must be greater than or equal to one\n");
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
			char *s = (char*)qualityArg.getValue().c_str();;
			while (sscanf(s, "%lf", &parameters->tcp_distoratio[parameters->tcp_numlayers]) == 1) {
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
					fprintf(stderr, "[ERROR] PSNR values must be greater than or equal to zero\n");
					return 1;
				}
				if (distortion < lastDistortion && !(i == parameters->tcp_numlayers-1 && distortion == 0)) {
					fprintf(stderr, "[ERROR] PSNR values must be listed in ascending order\n");
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
			substr2 = (char*)strchr(rawFormatArg.getValue().c_str(), '@');
			if (substr2 == nullptr) {
				len = (uint32_t)rawFormatArg.getValue().length();
			}
			else {
				len = (uint32_t)(substr2 - rawFormatArg.getValue().c_str());
				substr2++; /* skip '@' character */
			}
			substr1 = (char*)malloc((len + 1) * sizeof(char));
			if (substr1 == nullptr) {
				return 1;
			}
			memcpy(substr1, rawFormatArg.getValue().c_str(), len);
			substr1[len] = '\0';
			if (sscanf(substr1, "%d,%d,%d,%d,%c", &width, &height, &ncomp, &bitdepth, &signo) == 5) {
				if (signo == 's') {
					raw_signed = true;
				}
				else if (signo == 'u') {
					raw_signed = false;
				}
				else {
					wrong = true;
				}
			}
			else {
				wrong = true;
			}
			if (!wrong) {
				raw_cparameters_t* raw_cp = &parameters->raw_cp;
				int compno;
				int lastdx = 1;
				int lastdy = 1;
				raw_cp->rawWidth = width;
				raw_cp->rawHeight = height;
				raw_cp->rawComp = ncomp;
				raw_cp->rawBitDepth = bitdepth;
				raw_cp->rawSigned = raw_signed;
				raw_cp->rawComps = (raw_comp_cparameters_t*)malloc(((uint32_t)(ncomp)) * sizeof(raw_comp_cparameters_t));
				if (raw_cp->rawComps == nullptr) {
					free(substr1);
					return 1;
				}
				for (compno = 0; compno < ncomp && !wrong; compno++) {
					if (substr2 == nullptr) {
						raw_cp->rawComps[compno].dx = lastdx;
						raw_cp->rawComps[compno].dy = lastdy;
					}
					else {
						int dx, dy;
						sep = strchr(substr2, ':');
						if (sep == nullptr) {
							if (sscanf(substr2, "%dx%d", &dx, &dy) == 2) {
								lastdx = dx;
								lastdy = dy;
								raw_cp->rawComps[compno].dx = dx;
								raw_cp->rawComps[compno].dy = dy;
								substr2 = nullptr;
							}
							else {
								wrong = true;
							}
						}
						else {
							if (sscanf(substr2, "%dx%d:%s", &dx, &dy, substr2) == 3) {
								raw_cp->rawComps[compno].dx = dx;
								raw_cp->rawComps[compno].dy = dy;
							}
							else {
								wrong = true;
							}
						}
					}
				}
			}
			free(substr1);
			if (wrong) {
				fprintf(stderr, "\n[ERROR]  invalid raw image parameters\n");
				fprintf(stderr, "Please use the Format option -F:\n");
				fprintf(stderr, "-F <width>,<height>,<ncomp>,<bitdepth>,{s,u}@<dx1>x<dy1>:...:<dxn>x<dyn>\n");
				fprintf(stderr, "If subsampling is omitted, 1x1 is assumed for all components\n");
				fprintf(stderr, "Example: -i image.raw -o image.j2k -F 512,512,3,8,u@1x1:2x2:2x2\n");
				fprintf(stderr, "         for raw 512x512 image with 4:2:0 subsampling\n");
				return 1;
			}
		}

		if (tilesArg.isSet()) {
			int32_t cp_tdx = 0, cp_tdy = 0;
			if (sscanf(tilesArg.getValue().c_str(), "%d,%d", &cp_tdx, &cp_tdy) == EOF) {
				fprintf(stderr, "[ERROR] sscanf failed for tiles argument\n");
				return 1;
			}
			// sanity check on tile dimensions
			if (cp_tdx <= 0 || cp_tdy <= 0) {
				fprintf(stderr, "[ERROR] Tile dimensions must be strictly positive\n");
				return 1;
			}
			parameters->cp_tdx = cp_tdx;
			parameters->cp_tdy = cp_tdy;
			parameters->tile_size_on = true;

		}

		if (resolutionArg.isSet()) {
			parameters->numresolution = resolutionArg.getValue();
		}

		if (precinctDimArg.isSet()){
			char sep;
			int res_spec = 0;

			char *s = (char*)precinctDimArg.getValue().c_str();
			int ret;
			do {
				sep = 0;
				ret = sscanf(s, "[%d,%d]%c", &parameters->prcw_init[res_spec],
					&parameters->prch_init[res_spec], &sep);
				if (!(ret == 2 && sep == 0) && !(ret == 3 && sep == ',')) {
					fprintf(stderr, "\n[ERROR]  could not parse precinct dimension: '%s' %x\n", s, sep);
					fprintf(stderr, "Example: -i lena.raw -o lena.j2k -c [128,128],[128,128]\n");
					return 1;
				}
				parameters->csty |= 0x01;
				res_spec++;
				s = strpbrk(s, "]") + 2;
			} while (sep == ',');
			parameters->res_spec = res_spec;
		}

		if (codeBlockDimArg.isSet()) {
			int cblockw_init = 0, cblockh_init = 0;
			if (sscanf(codeBlockDimArg.getValue().c_str(), "%d,%d", &cblockw_init, &cblockh_init) == EOF) {
				fprintf(stderr, "[ERROR] sscanf failed for code block dimension argument\n");
				return 1;
			}
			if (cblockw_init * cblockh_init > 4096 || cblockw_init > 1024
				|| cblockw_init < 4 || cblockh_init > 1024 || cblockh_init < 4) {
				fprintf(stderr,
					"[ERROR] Size of code block error (option -b)\n\nRestriction :\n"
					"    * width*height<=4096\n    * 4<=width,height<= 1024\n\n");
				return 1;
			}
			parameters->cblockw_init = cblockw_init;
			parameters->cblockh_init = cblockh_init;
		}

		if (progressionOrderArg.isSet()) {
			char progression[4];

			strncpy(progression, progressionOrderArg.getValue().c_str(), 4);
			parameters->prog_order = give_progression(progression);
			if (parameters->prog_order == -1) {
				fprintf(stderr, "[ERROR] Unrecognized progression order "
					"[LRCP, RLCP, RPCL, PCRL, CPRL] !!\n");
				return 1;
			}
		}

		if (subsamplingFactorArg.isSet()) {
			if (sscanf(subsamplingFactorArg.getValue().c_str(), "%d,%d", &parameters->subsampling_dx,
				&parameters->subsampling_dy) != 2) {
				fprintf(stderr, "[ERROR] '-s' sub-sampling argument error !  [-s dx,dy]\n");
				return 1;
			}
		}

		if (imageOffsetArg.isSet()) {
			if (sscanf(imageOffsetArg.getValue().c_str(), "%d,%d", &parameters->image_offset_x0,
				&parameters->image_offset_y0) != 2) {
				fprintf(stderr, "[ERROR] -d 'image offset' argument "
					"error !! [-d x0,y0]\n");
				return 1;
			}
		}

		if (pocArg.isSet()) {
			uint32_t numpocs = 0;		/* number of progression order change (POC) default 0 */
			opj_poc_t *POC = nullptr;	/* POC : used in case of Progression order change */

			char *s = (char*)pocArg.getValue().c_str();
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

		if (sopArg.isSet()) {
			parameters->csty |= 0x02;
		}

		if (ephArg.isSet()) {
			parameters->csty |= 0x04;
		}

		if (irreversibleArg.isSet()) {
			parameters->irreversible = 1;
		}

		if (pluginPathArg.isSet()) {
			if (plugin_path)
				strcpy(plugin_path, pluginPathArg.getValue().c_str());
		}

		img_fol->set_imgdir = 0;
		if (imgDirArg.isSet()) {
			img_fol->imgdirpath = (char*)malloc(strlen(imgDirArg.getValue().c_str()) + 1);
			strcpy(img_fol->imgdirpath, imgDirArg.getValue().c_str());
			img_fol->set_imgdir = 1;
		}
		if (out_fol) {
			out_fol->set_imgdir = 0;
			if (outDirArg.isSet()) {
				out_fol->imgdirpath = (char*)malloc(strlen(outDirArg.getValue().c_str()) + 1);
				strcpy(out_fol->imgdirpath, outDirArg.getValue().c_str());
				out_fol->set_imgdir = 1;
			}
		}
		if (cinema2KArg.isSet()) {
			if (!checkCinema(&cinema2KArg, OPJ_PROFILE_CINEMA_2K, parameters)) {
				return 1;
			}
			if (parameters->verbose) {
				fprintf(stdout, "[WARNING] CINEMA 2K profile activated\n"
					"Other options specified may be overridden\n");
			}
		}
		if (cinema4KArg.isSet()) {
			if (!checkCinema(&cinema4KArg, OPJ_PROFILE_CINEMA_4K, parameters)) {
				return 1;
			}
			if (parameters->verbose) {
				fprintf(stdout, "[WARNING] CINEMA 4K profile activated\n"
					"Other options specified may be overridden\n");
			}
		}
		if (rsizArg.isSet()) {
			if (cinema2KArg.isSet() || cinema4KArg.isSet()) {
				fprintf(stdout, "[WARNING]  Cinema profile set - RSIZ parameter ignored.\n");
			}
			else {
				parameters->rsiz = rsizArg.getValue();
			}
		}

		if (modeArg.isSet()) {
			int value = modeArg.getValue();
			for (uint32_t i = 0; i <= 5; i++) {
				int cache = value & (1 << i);
				if (cache)
					parameters->mode |= (1 << i);
			}
		}

		if (captureResArg.isSet()) {
			if (sscanf(captureResArg.getValue().c_str(), "%lf,%lf", parameters->capture_resolution,
				parameters->capture_resolution + 1) != 2) {
				fprintf(stderr, "[ERROR] -Q 'capture resolution' argument error !! [-Q X0,Y0]");
				return 1;
			}
			parameters->write_capture_resolution = true;
		}
		if (displayResArg.isSet()) {
			if (sscanf(captureResArg.getValue().c_str(), "%lf,%lf", parameters->display_resolution,
				parameters->display_resolution + 1) != 2) {
				fprintf(stderr, "[ERROR] -D 'display resolution' argument error !! [-D X0,Y0]");
				return 1;
			}
			parameters->write_display_resolution = true;
		}

		if (mctArg.isSet()) {
			int mct_mode = mctArg.getValue();
			if (mct_mode < 0 || mct_mode > 2) {
				fprintf(stderr, "[ERROR] MCT incorrect value. Current accepted values are 0, 1 or 2.\n");
				return 1;
			}
			parameters->tcp_mct = (uint8_t)mct_mode;
		}

		if (customMCTArg.isSet()) {
			char *lFilename = (char*)customMCTArg.getValue().c_str();
			char *lMatrix;
			char *lCurrentPtr;
			float *lCurrentDoublePtr;
			float *lSpace;
			int *l_int_ptr;
			int lNbComp = 0, lTotalComp, lMctComp, i2;
			size_t lStrLen, lStrFread;

			/* Open file */
			FILE * lFile = fopen(lFilename, "r");
			if (lFile == nullptr) {
				return 1;
			}

			/* Set size of file and read its content*/
			fseek(lFile, 0, SEEK_END);
			lStrLen = (size_t)ftell(lFile);
			fseek(lFile, 0, SEEK_SET);
			lMatrix = (char *)malloc(lStrLen + 1);
			if (lMatrix == nullptr) {
				fclose(lFile);
				return 1;
			}
			lStrFread = fread(lMatrix, 1, lStrLen, lFile);
			fclose(lFile);
			if (lStrLen != lStrFread) {
				free(lMatrix);
				return 1;
			}

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

			lNbComp = (int)(sqrt(4 * lNbComp + 1) / 2. - 0.5);
			lMctComp = lNbComp * lNbComp;
			lTotalComp = lMctComp + lNbComp;
			lSpace = (float *)malloc((size_t)lTotalComp * sizeof(float));
			if (lSpace == nullptr) {
				free(lMatrix);
				return 1;
			}
			lCurrentDoublePtr = lSpace;
			for (i2 = 0; i2<lMctComp; ++i2) {
				lStrLen = strlen(lCurrentPtr) + 1;
				*lCurrentDoublePtr++ = (float)atof(lCurrentPtr);
				lCurrentPtr += lStrLen;
			}

			l_int_ptr = (int*)lCurrentDoublePtr;
			for (i2 = 0; i2<lNbComp; ++i2) {
				lStrLen = strlen(lCurrentPtr) + 1;
				*l_int_ptr++ = atoi(lCurrentPtr);
				lCurrentPtr += lStrLen;
			}

			/* TODO should not be here ! */
			opj_set_MCT(parameters, lSpace, (int *)(lSpace + lMctComp), (uint32_t)lNbComp);

			/* Free memory*/
			free(lSpace);
			free(lMatrix);

		}

		if (roiArg.isSet()) {
			if (sscanf(roiArg.getValue().c_str(), "c=%d,U=%d", &parameters->roi_compno,
				&parameters->roi_shift) != 2) {
				fprintf(stderr, "[ERROR] ROI error !! [-ROI c='compno',U='shift']\n");
				return 1;
			}
		}

		if (tileOffsetArg.isSet()) {
			if (sscanf(tileOffsetArg.getValue().c_str(), "%d,%d", &parameters->cp_tx0, &parameters->cp_ty0) != 2) {
				fprintf(stderr, "[ERROR] -T 'tile offset' argument error !! [-T X0,Y0]");
				return 1;
			}
		}

		if (commentArg.isSet()) {
			parameters->cp_comment = (char*)malloc(commentArg.getValue().length() + 1);
			if (parameters->cp_comment) {
				strcpy(parameters->cp_comment, commentArg.getValue().c_str());
			}
		}

		if (tpArg.isSet()) {
			parameters->tp_flag = tpArg.getValue();
			parameters->tp_on = 1;

		}

		if (verboseArg.isSet()) {
			parameters->verbose = verboseArg.getValue();
		}

	}
	catch (ArgException &e)  // catch any exceptions
	{
		cerr << "error: " << e.error() << " for arg " << e.argId() << endl;
	}

    if(img_fol->set_imgdir == 1) {
        if(!(parameters->infile[0] == 0)) {
            fprintf(stderr, "[ERROR] options -ImgDir and -i cannot be used together !!\n");
            return 1;
        }
        if(img_fol->set_out_format == 0) {
            fprintf(stderr, "[ERROR] When -ImgDir is used, -OutFor <FORMAT> must be used !!\n");
            fprintf(stderr, "Only one format allowed! Valid formats are j2k and jp2!!\n");
            return 1;
        }
        if(!((parameters->outfile[0] == 0))) {
            fprintf(stderr, "[ERROR] options -ImgDir and -o cannot be used together !!\n");
            fprintf(stderr, "Specify OutputFormat using -OutFor<FORMAT> !!\n");
            return 1;
        }
    } else {
		if (parameters->cod_format == -1) {
			if (parameters->infile[0] == 0) {
				fprintf(stderr, "[ERROR] Missing input file parameter\n"
					"Example: %s -i image.pgm -o image.j2k\n", argv[0]);
				fprintf(stderr, "   Help: %s -h\n", argv[0]);
				return 1;
			}
		}

		if (parameters->outfile[0] == 0) {
			fprintf(stderr, "[ERROR] Missing output file parameter\n"
				"Example: %s -i image.pgm -o image.j2k\n", argv[0]);
			fprintf(stderr, "   Help: %s -h\n", argv[0]);
			return 1;
		}
    }

    if ( (parameters->decod_format == RAW_DFMT && parameters->raw_cp.rawWidth == 0)
            || (parameters->decod_format == RAWL_DFMT && parameters->raw_cp.rawWidth == 0)) {
        fprintf(stderr,"[ERROR] invalid raw image parameters\n");
        fprintf(stderr,"Please use the Format option -F:\n");
        fprintf(stderr,"-F rawWidth,rawHeight,rawComp,rawBitDepth,s/u (Signed/Unsigned)\n");
        fprintf(stderr,"Example: -i lena.raw -o lena.j2k -F 512,512,3,8,u\n");
        fprintf(stderr,"Aborting\n");
        return 1;
    }

    if ((parameters->cp_disto_alloc ||  parameters->cp_fixed_quality)
            && (!(parameters->cp_disto_alloc ^  parameters->cp_fixed_quality))) {
        fprintf(stderr, "[ERROR] options -r and -q cannot be used together !!\n");
        return 1;
    }				

    /* if no rate entered, lossless by default */
    if (parameters->tcp_numlayers == 0) {
        parameters->tcp_rates[0] = 0;	
        parameters->tcp_numlayers=1;
        parameters->cp_disto_alloc = 1;
    }

    if((parameters->cp_tx0 > 0 && parameters->cp_tx0 > parameters->image_offset_x0) ||
		(parameters->cp_ty0 > 0 && parameters->cp_ty0 > parameters->image_offset_y0)) {
        fprintf(stderr,
                "[ERROR] Tile offset dimension is unnappropriate --> TX0(%d)<=IMG_X0(%d) TYO(%d)<=IMG_Y0(%d) \n",
                parameters->cp_tx0, parameters->image_offset_x0, parameters->cp_ty0, parameters->image_offset_y0);
        return 1;
    }

    for (uint32_t i = 0; i < parameters->numpocs; i++) {
        if (parameters->POC[i].prg == -1) {
            fprintf(stderr,
                    "[ERROR] Unrecognized progression order in option -P (POC n %d) [LRCP, RLCP, RPCL, PCRL, CPRL] !!\n",
                    i + 1);
        }
    }

    /* If subsampled image is provided, automatically disable MCT */
    if ( ((parameters->decod_format == RAW_DFMT) || (parameters->decod_format == RAWL_DFMT))
            && (   ((parameters->raw_cp.rawComp > 1 ) && ((parameters->raw_cp.rawComps[1].dx > 1) || (parameters->raw_cp.rawComps[1].dy > 1)))
                   || ((parameters->raw_cp.rawComp > 2 ) && ((parameters->raw_cp.rawComps[2].dx > 1) || (parameters->raw_cp.rawComps[2].dy > 1)))
               )) {
        parameters->tcp_mct = 0;
    }

	if (parameters->tcp_mct == 2 && !parameters->mct_data) {
		fprintf(stderr, "[ERROR] Custom MCT has been set but no array-based MCT has been provided.\n");
		return false;
	}

    return 0;
}

/* -------------------------------------------------------------------------- */

/**
sample error debug callback expecting no client object
*/
static void error_callback(const char *msg, void *client_data)
{
    (void)client_data;
    fprintf(stderr, "[ERROR] %s", msg);
}
/**
sample warning debug callback expecting no client object
*/
static void warning_callback(const char *msg, void *client_data)
{
	bool verbose = true;
	if (client_data)
		verbose = *((bool*)client_data);
	if (verbose)
    fprintf(stdout, "[WARNING] %s", msg);
}
/**
sample debug callback expecting no client object
*/
static void info_callback(const char *msg, void *client_data)
{
	bool verbose = true;
	if (client_data)
		verbose = *((bool*)client_data);
	if (verbose)
    fprintf(stdout, "[INFO] %s", msg);
}

double grok_clock(void) {
#ifdef _WIN32
	/* _WIN32: use QueryPerformance (very accurate) */
    LARGE_INTEGER freq , t ;
    /* freq is the clock speed of the CPU */
    QueryPerformanceFrequency(&freq) ;
	/* cout << "freq = " << ((double) freq.QuadPart) << endl; */
    /* t is the high resolution performance counter (see MSDN) */
    QueryPerformanceCounter ( & t ) ;
    return freq.QuadPart ? ( t.QuadPart /(double) freq.QuadPart ) : 0 ;
#else
	/* Unix or Linux: use resource usage */
    struct rusage t;
    double procTime;
    /* (1) Get the rusage data structure at this moment (man getrusage) */
    getrusage(0,&t);
    /* (2) What is the elapsed time ? - CPU time = User time + System time */
	/* (2a) Get the seconds */
    procTime = (double)(t.ru_utime.tv_sec + t.ru_stime.tv_sec);
    /* (2b) More precisely! Get the microseconds part ! */
    return ( procTime + (double)(t.ru_utime.tv_usec + t.ru_stime.tv_usec) * 1e-6 ) ;
#endif
}

struct CompressInitParams {
	CompressInitParams() : initialized(false) {
		
		plugin_path[0] = 0;

		/* Initialize indexfilename and img_fol */
		*indexfilename = 0;

		memset(&img_fol, 0, sizeof(img_fol));
		memset(&out_fol, 0, sizeof(out_fol));

	}

	~CompressInitParams() {

		// clean up encode parameters
		if (parameters.cp_comment)
			free(parameters.cp_comment);
		parameters.cp_comment = nullptr ;
		if (parameters.raw_cp.rawComps)
			free(parameters.raw_cp.rawComps);
		parameters.raw_cp.rawComps = nullptr;


		if (img_fol.imgdirpath)
			free(img_fol.imgdirpath);
		if (out_fol.imgdirpath)
			free(out_fol.imgdirpath);

	}
	bool initialized;

	opj_cparameters_t parameters;	/* compression parameters */

	char indexfilename[OPJ_PATH_LEN];	/* index file name */
	char plugin_path[OPJ_PATH_LEN];

	img_fol_t img_fol;
	img_fol_t out_fol;

};

static int plugin_main(int argc, char **argv, CompressInitParams* initParams);


/* -------------------------------------------------------------------------- */
/**
 * OPJ_COMPRESS MAIN
 */
/* -------------------------------------------------------------------------- */
int main(int argc, char **argv) {
	CompressInitParams initParams;
	dircnt_t *dirptr = nullptr;
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
		uint32_t i, num_images, imageno;

		//cache certain settings
		auto tcp_mct = initParams.parameters.tcp_mct;
		auto rateControlAlgorithm = initParams.parameters.rateControlAlgorithm;

		double t = grok_clock();
		/* Read directory if necessary */
		if (initParams.img_fol.set_imgdir == 1) {
			num_images = get_num_images(initParams.img_fol.imgdirpath);
			if (num_images == 0) {
				fprintf(stderr, "Folder is empty\n");
				goto cleanup;
			}
			dirptr = (dircnt_t*)malloc(sizeof(dircnt_t));
			if (!dirptr) {
				success = 1;
				goto cleanup;
			}
			dirptr->filename_buf = (char*)malloc(num_images*OPJ_PATH_LEN * sizeof(char));
			if (!dirptr->filename_buf) {
				success = 1;
				goto cleanup;
			}
			dirptr->filename = (char**)malloc(num_images * sizeof(char*));
			if (!dirptr->filename) {
				success = 1;
				goto cleanup;
			}
			for (i = 0; i < num_images; i++) {
				dirptr->filename[i] = dirptr->filename_buf + i*OPJ_PATH_LEN;
			}
			if (load_images(dirptr, initParams.img_fol.imgdirpath) == 1) {
				goto cleanup;
			}
		}
		else {
			num_images = 1;
		}
		/*Encoding image one by one*/
		for (imageno = 0; imageno < num_images; imageno++) {
			if (initParams.parameters.verbose)
				fprintf(stdout, "\n");
			//restore cached settings
			initParams.parameters.tcp_mct = tcp_mct;
			initParams.parameters.rateControlAlgorithm = rateControlAlgorithm;
			if (initParams.img_fol.set_imgdir == 1) {
				if (get_next_file((int)imageno, dirptr, &initParams.img_fol, initParams.out_fol.set_imgdir ? &initParams.out_fol : &initParams.img_fol, &initParams.parameters)) {
					continue;
				}
			}
			grok_plugin_encode_user_callback_info_t opjInfo;
			memset(&opjInfo, 0, sizeof(grok_plugin_encode_user_callback_info_t));
			opjInfo.encoder_parameters = &initParams.parameters;
			opjInfo.image = nullptr;
			opjInfo.output_file_name = initParams.parameters.outfile;
			opjInfo.input_file_name = initParams.parameters.infile;

			if (!plugin_compress_callback(&opjInfo)) {
				if (num_images == 1) {
					success = 1;
					goto cleanup;
				}
				continue;
			}
			num_compressed_files++;
		}
		t = grok_clock() - t;
		if (initParams.parameters.verbose && num_compressed_files) {
			fprintf(stdout, "encode time: %d ms \n", (int)((t * 1000.0) / (double)num_compressed_files));
		}
	} catch (std::bad_alloc ba){
		std::cerr << "[ERROR]: Out of memory. Exiting." << std::endl;
		success = 1;
		goto cleanup;
	}
cleanup:
	if (dirptr) {
		if (dirptr->filename_buf)
			free(dirptr->filename_buf);
		if (dirptr->filename)
			free(dirptr->filename);
		free(dirptr);
	}
	opj_cleanup();
	return success;

}

img_fol_t img_fol_plugin, out_fol_plugin;

static bool plugin_compress_callback(grok_plugin_encode_user_callback_info_t* info) {
	opj_cparameters_t* parameters = info->encoder_parameters;
	bool bSuccess = true;
	opj_stream_t *l_stream = nullptr;
	opj_codec_t* l_codec = nullptr;
	opj_image_t *image = info->image;
	char  outfile[OPJ_PATH_LEN];
	char  temp_ofname[OPJ_PATH_LEN];

	uint32_t l_nb_tiles = 4;
	bool bUseTiles = false;
	bool createdImage = false;
	bool inMemoryCompression = false;

	// get output file
	outfile[0] = 0;
	if (info->output_file_name && info->output_file_name[0]) {
		if (info->outputFileNameIsRelative) {
			strcpy(temp_ofname, get_file_name((char*)info->output_file_name));
			if (img_fol_plugin.set_out_format == 1) {
				sprintf(outfile, "%s%s%s.%s",
					out_fol_plugin.imgdirpath ? out_fol_plugin.imgdirpath : img_fol_plugin.imgdirpath,
					get_path_separator(),
					temp_ofname,
					img_fol_plugin.out_format);
			}
		}
		else {
			strcpy(outfile, info->output_file_name);
		}
	}
	else {
		bSuccess = false;
		goto cleanup;
	}

	if (!image) {
		if (parameters->decod_format == -1) {
			parameters->decod_format = get_file_format((char*)info->input_file_name);
			if (!isNonJPEG2000FileFormatSupported(parameters->decod_format)) {
				bSuccess = false;
				goto cleanup;
			}

		}
		/* decode the source image */
		/* ----------------------- */

		switch (info->encoder_parameters->decod_format) {
		case PGX_DFMT:
		{
			PGXFormat pgx;
			image = pgx.decode(info->input_file_name, info->encoder_parameters);
			if (!image) {
				fprintf(stderr, "[ERROR] Unable to load pgx file\n");
				bSuccess = false;
				goto cleanup;
			}
		}
			break;

		case PXM_DFMT:
		{
			PNMFormat pnm(false);
			image = pnm.decode(info->input_file_name, info->encoder_parameters);
			if (!image) {
				fprintf(stderr, "[ERROR] Unable to load pnm file\n");
				bSuccess = false;
				goto cleanup;
			}
		}
			break;

		case BMP_DFMT:
		{
			BMPFormat bmp;
			image = bmp.decode(info->input_file_name, info->encoder_parameters);
			if (!image) {
				fprintf(stderr, "[ERROR] Unable to load bmp file\n");
				bSuccess = false;
				goto cleanup;
			}
		}
			break;

#ifdef GROK_HAVE_LIBTIFF
		case TIF_DFMT:
		{
			TIFFFormat tif;
			image = tif.decode(info->input_file_name, info->encoder_parameters);
			if (!image) {
				fprintf(stderr, "[ERROR] Unable to load tiff file\n");
				bSuccess = false;
				goto cleanup;
			}
		}
			break;
#endif /* GROK_HAVE_LIBTIFF */

		case RAW_DFMT:
		{
			RAWFormat raw(true);
			image = raw.decode(info->input_file_name, info->encoder_parameters);
			if (!image) {
				fprintf(stderr, "[ERROR] Unable to load raw file\n");
				bSuccess = false;
				goto cleanup;
			}
		}
			break;

		case RAWL_DFMT:
		{
			RAWFormat raw(false);
			image = raw.decode(info->input_file_name, info->encoder_parameters);
			if (!image) {
				fprintf(stderr, "[ERROR] Unable to load raw file\n");
				bSuccess = false;
				goto cleanup;
			}
		}
			break;

		case TGA_DFMT:
		{
			TGAFormat tga;
			image = tga.decode(info->input_file_name, info->encoder_parameters);
			if (!image) {
				fprintf(stderr, "[ERROR] Unable to load tga file\n");
				bSuccess = false;
				goto cleanup;
			}
		}
			break;

#ifdef GROK_HAVE_LIBPNG
		case PNG_DFMT:
		{
			PNGFormat png;
			image = png.decode(info->input_file_name, info->encoder_parameters);
			if (!image) {
				fprintf(stderr, "[ERROR] Unable to load png file\n");
				bSuccess = false;
				goto cleanup;
			}
		}
			break;
#endif /* GROK_HAVE_LIBPNG */

#ifdef GROK_HAVE_LIBJPEG
		case JPG_DFMT:
		{
			JPEGFormat jpeg;
			image = jpeg.decode(info->input_file_name, info->encoder_parameters);
			if (!image) {
				fprintf(stderr, "[ERROR] Unable to load jpeg file\n");
				bSuccess = false;
				goto cleanup;
			}
		}
			break;
#endif /* GROK_HAVE_LIBPNG */
		}

		/* Can happen if input file is TIFF or PNG
		* and GROK_HAVE_LIBTIF or GROK_HAVE_LIBPNG is undefined
		*/
		if (!image) {
			fprintf(stderr, "[ERROR] Unable to load file: no image generated.\n");
			bSuccess = false;
			goto cleanup;
		}
		createdImage = true;
	}

	if (inMemoryCompression) {
		auto fp = fopen(info->input_file_name, "rb");
		if (!fp) {
			fprintf(stderr, "[ERROR] opj_compress: unable to open file %s for reading", info->input_file_name);
			bSuccess = false;
			goto cleanup;
		}

		auto rc = fseek(fp, 0, SEEK_END);
		if (rc == -1) {
			fprintf(stderr, "[ERROR] opj_compress: unable to seek on file %s", info->input_file_name);
			fclose(fp);
			bSuccess = false;
			goto cleanup;
		}
		auto fileLength = ftell(fp);
		fclose(fp);

		if (fileLength) {
			//  option to write to buffer, assuming one knows how large compressed stream will be 
			uint64_t imageSize = (((image->x1 - image->x0) * (image->y1 - image->y0) * image->numcomps * ((image->comps[0].prec + 7) / 8)) * 3) / 2;
			info->compressBufferLen = fileLength > imageSize ? fileLength : imageSize;
			info->compressBuffer = new uint8_t[info->compressBufferLen];
		}
	}


	// limit to 16 bit precision
	for (uint32_t i = 0; i < image->numcomps; ++i) {
		if (image->comps[i].prec > 16) {
			fprintf(stderr, "[ERROR] Precision = %d not supported:\n", image->comps[i].prec);
			bSuccess = false;
			goto cleanup;
		}
	}

	/* Decide if MCT should be used */
	if (parameters->tcp_mct == 255) { /* mct mode has not been set in commandline */
		parameters->tcp_mct = (image->numcomps >= 3) ? 1 : 0;
	}
	else {            /* mct mode has been set in commandline */
		if ((parameters->tcp_mct == 1) && (image->numcomps < 3)) {
			fprintf(stderr, "[ERROR] RGB->YCC conversion cannot be used:\n");
			fprintf(stderr, "Input image has less than 3 components\n");
			bSuccess = false;
			goto cleanup;
		}
		if ((parameters->tcp_mct == 2) && (!parameters->mct_data)) {
			fprintf(stderr, "[ERROR] Custom MCT has been set but no array-based MCT\n");
			fprintf(stderr, "has been provided. Aborting.\n");
			bSuccess = false;
			goto cleanup;
		}
	}

	// set default algorithm 
	if (parameters->rateControlAlgorithm == 255) {
		parameters->rateControlAlgorithm = 0;
	}

	switch (parameters->cod_format) {
	case J2K_CFMT:	/* JPEG-2000 codestream */
	{
		/* Get a decoder handle */
		l_codec = opj_create_compress(OPJ_CODEC_J2K);
		break;
	}
	case JP2_CFMT:	/* JPEG 2000 compressed image data */
	{
		/* Get a decoder handle */
		l_codec = opj_create_compress(OPJ_CODEC_JP2);
		break;
	}
	default:
		bSuccess = false;
		goto cleanup;
	}

	/* catch events using our callbacks and give a local context */
	opj_set_info_handler(l_codec, info_callback, &parameters->verbose);
	opj_set_warning_handler(l_codec, warning_callback, &parameters->verbose);
	opj_set_error_handler(l_codec, error_callback, nullptr);

	if (!opj_setup_encoder(l_codec, parameters, image)) {
		fprintf(stderr, "[ERROR] failed to encode image: opj_setup_encoder\n");
		bSuccess = false;
		goto cleanup;
	}
	if (info->compressBuffer) {
		// let stream clean up compress buffer
		l_stream = opj_stream_create_buffer_stream(info->compressBuffer, 
													info->compressBufferLen, 
													true,
													false);
	}
	else {
		l_stream = opj_stream_create_default_file_stream(outfile, false);
	}
	if (!l_stream) {
		fprintf(stderr, "[ERROR] failed to create stream\n");
		bSuccess = false;
		goto cleanup;
	}

	/* encode the image */
	bSuccess = opj_start_compress(l_codec, image, l_stream);
	if (!bSuccess) {
		fprintf(stderr, "[ERROR] failed to encode image: opj_start_compress\n");
		bSuccess = false;
		goto cleanup;
	}
	if (bSuccess && bUseTiles) {
		uint8_t *l_data;
		uint64_t l_data_size = 512 * 512 * 3;
		l_data = (uint8_t*)calloc(1, l_data_size);
		assert(l_data);
		for (uint32_t i = 0; i<l_nb_tiles; ++i) {
			if (!opj_write_tile(l_codec, i, l_data, l_data_size, l_stream)) {
				fprintf(stderr, "[ERROR] test_tile_encoder: failed to write the tile %d!\n", i);
				bSuccess = false;
				goto cleanup;
			}
		}
		free(l_data);
	}
	else {
		bSuccess = bSuccess && opj_encode_with_plugin(l_codec, info->tile, l_stream);
		if (!bSuccess) {
			fprintf(stderr, "[ERROR] failed to encode image: opj_encode\n");
			bSuccess = false;
			goto cleanup;
		}
	}
	bSuccess = bSuccess && opj_end_compress(l_codec, l_stream);
	if (!bSuccess) {
		fprintf(stderr, "[ERROR] failed to encode image: opj_end_compress\n");
		bSuccess = false;
		goto cleanup;
	}
	if (info->compressBuffer) {
		auto fp = fopen(outfile, "wb");
		if (!fp) {
			fprintf(stderr, "[ERROR] Buffer compress: failed to open file %s for writing\n", outfile);
		}
		else {
			auto len = opj_stream_get_write_buffer_stream_length(l_stream);
			size_t written = fwrite(info->compressBuffer, 1,len, fp);
			if (written != len) {
				fprintf(stderr, "[ERROR] Buffer compress: only %zd bytes written out of %zd total\n", len, written);
			}
			fclose(fp);
		}
	}
	if (l_stream) {
		opj_stream_destroy(l_stream);
		l_stream = nullptr;
	}
	if (l_codec) {
		opj_destroy_codec(l_codec);
		l_codec = nullptr;
	}
	if (!bSuccess) {
		fprintf(stderr, "[ERROR] failed to encode image\n");
		remove(parameters->outfile);
		bSuccess = false;
		goto cleanup;
	}
cleanup:
	if (l_stream)
		opj_stream_destroy(l_stream);
	if (l_codec)
		opj_destroy_codec(l_codec);
	if (createdImage)
		opj_image_destroy(image);
	return bSuccess;
}

static int plugin_main(int argc, char **argv, CompressInitParams* initParams) {
	if (!initParams)
		return 1;

	/* set encoding parameters to default values */
	opj_set_default_encoder_parameters(&initParams->parameters);


	/* parse input and get user encoding parameters */
	initParams->parameters.tcp_mct = 255; /* This will be set later according to the input image or the provided option */
	initParams->parameters.rateControlAlgorithm = 255;
	if (parse_cmdline_encoder_ex(argc, 
								argv,
								&initParams->parameters,
								&initParams->img_fol,
								&initParams->out_fol,
								initParams->indexfilename,
								sizeof(initParams->indexfilename),
								initParams->plugin_path) == 1) {
		return 1;
	}

#ifndef NDEBUG
	if (initParams->parameters.verbose) {
		std::string out;
		for (int i = 0; i < argc; ++i) {
			out += std::string(" ") + argv[i];
		}
		printf("%s\n", out.c_str());
	}
#endif

#ifdef GROK_HAVE_LIBTIFF
	tiffSetErrorAndWarningHandlers(initParams->parameters.verbose);
#endif
	
	initParams->initialized = true;

	// loads plugin but does not actually create codec
	if (!opj_initialize(initParams->plugin_path)) {
		opj_cleanup();
		return 1;
	}

	img_fol_plugin = initParams->img_fol;
	out_fol_plugin = initParams->out_fol;

	// create codec
	grok_plugin_init_info_t initInfo;
	initInfo.deviceId = initParams->parameters.deviceId;
	initInfo.verbose = initParams->parameters.verbose;
	if (!grok_plugin_init(initInfo)) {
		opj_cleanup();
		return 1;

	}
	
	bool isBatch = initParams->img_fol.imgdirpath &&  initParams->out_fol.imgdirpath;
	uint32_t state = grok_plugin_get_debug_state();
	if ((state & GROK_PLUGIN_STATE_DEBUG) || (state & GROK_PLUGIN_STATE_PRE_TR1)) {
		isBatch = 0;
	}
	dircnt_t *dirptr = nullptr;
	int32_t success = 0;
	uint32_t num_images, imageno;
	if (isBatch) {
		setup_signal_handler();
		success = grok_plugin_batch_encode(initParams->img_fol.imgdirpath, initParams->out_fol.imgdirpath, &initParams->parameters, plugin_compress_callback);
		// if plugin successfully begins batch encode, then wait for batch to complete
		if (success==0) {
			uint32_t slice = 100;	//ms
			uint32_t slicesPerSecond = 1000 / slice;
			uint32_t seconds = initParams->parameters.duration;
			if (!seconds)
				seconds = UINT_MAX;
			for (uint32_t i = 0U; i < seconds*slicesPerSecond; ++i) {
				batch_sleep(1);
				if (grok_plugin_is_batch_complete()) {
					break;
				}
			}
			grok_plugin_stop_batch_encode();
		}
	}
	else 	{
		// loop through all files
		/* Read directory if necessary */
		if (initParams->img_fol.set_imgdir == 1) {
			num_images = get_num_images(initParams->img_fol.imgdirpath);
			if (!num_images)
				goto cleanup;
			dirptr = (dircnt_t*)malloc(sizeof(dircnt_t));
			if (!dirptr) {
				success = 1;
				goto cleanup;
			}
			if (dirptr) {
				dirptr->filename_buf = (char*)malloc(num_images*OPJ_PATH_LEN*sizeof(char));	
				if (!dirptr->filename_buf) {
					success = 1;
					goto cleanup;
				}
				dirptr->filename = (char**)malloc(num_images*sizeof(char*));
				if (!dirptr->filename) {
					success = 1;
					goto cleanup;
				}
				for (uint32_t i = 0; i<num_images; i++) {
					dirptr->filename[i] = dirptr->filename_buf + i*OPJ_PATH_LEN;
				}
			}
			if (load_images(dirptr, initParams->img_fol.imgdirpath) == 1) {
				goto cleanup;
			}
			if (num_images == 0) {
				fprintf(stderr, "[ERROR] Folder is empty\n");
				goto cleanup;
			}
		}
		else {
			num_images = 1;
		}
		//cache certain settings
		auto tcp_mct = initParams->parameters.tcp_mct;
		auto rateControlAlgorithm = initParams->parameters.rateControlAlgorithm;
		/*Encoding image one by one*/
		for (imageno = 0; imageno < num_images; imageno++) {
			if (initParams->img_fol.set_imgdir == 1) {
				if (get_next_file((int)imageno, dirptr,
								&initParams->img_fol, initParams->out_fol.imgdirpath ? &initParams->out_fol : &initParams->img_fol, &initParams->parameters)) {
					continue;
				}
			}
			//restore cached settings
			initParams->parameters.tcp_mct =  tcp_mct;
			initParams->parameters.rateControlAlgorithm = rateControlAlgorithm;
			success = grok_plugin_encode(&initParams->parameters, plugin_compress_callback);
			if (success != 0)
				break;
		}
	}

cleanup:
	if (dirptr) {
		if (dirptr->filename_buf)
			free(dirptr->filename_buf);
		if (dirptr->filename)
			free(dirptr->filename);
		free(dirptr);
	}
	opj_cleanup();
	return success;
}
