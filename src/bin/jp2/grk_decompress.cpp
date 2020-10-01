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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_apps_config.h"

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
#include <sys/stat.h>
#endif /* _WIN32 */

#include "grk_decompress.h"

#include "common.h"
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

#ifdef GROK_HAVE_LIBLCMS
#include <lcms2.h>
#endif
#include "color.h"
#include "grk_string.h"
#include <climits>
#include <string>
#define TCLAP_NAMESTARTSTRING "-"
#include "tclap/CmdLine.h"
#include <chrono>
#include "spdlog/sinks/basic_file_sink.h"

using namespace TCLAP;

namespace grk {

void exit_func() {
	grk_plugin_stop_batch_decompress();
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


static void error_callback(const char *msg, void *client_data) {
	(void) client_data;
	spdlog::default_logger()->error(msg);
}
static void warning_callback(const char *msg, void *client_data) {
	(void) client_data;
	spdlog::default_logger()->warn(msg);
}
static void info_callback(const char *msg, void *client_data) {
	(void) client_data;
	spdlog::default_logger()->info(msg);
}


static void decode_help_display(void) {
	fprintf(stdout,
			"grk_decompress - decompress JPEG 2000 codestream to various image formats.\n"
					"This utility has been compiled against libgrokj2k v%s.\n\n",
			grk_version());

	fprintf(stdout,
			"-----------\n"
			"Parameters:\n"
			"-----------\n"
			"\n"
			"  [-y | -ImgDir] <directory> \n"
			"	Compressed image file directory\n"
			"  [-O | -OutFor] <PBM|PGM|PPM|PNM|PAM|PGX|PNG|BMP|TIF|RAW|RAWL>\n"
			"    REQUIRED only if [ImgDir] option is used\n"
			"	Output format for decompressed images.\n");
	fprintf(stdout, "  [-i | -InputFile] <compressed file>\n"
			"    REQUIRED only if [ImgDir] option is not specified\n"
			"    Currently accepts J2K and JP2 files. The file type\n"
			"    is identified by parsing the beginning of the file.\n");
	fprintf(stdout,
			"  [-o | -OutputFile] <decompressed file>\n"
			"    REQUIRED\n"
			"    Currently accepts formats specified above (see OutFor option)\n"
			"    Binary data is written to the file (not ascii). If a PGX\n"
			"    filename is given, there will be as many output files as there are\n"
			"    components: an index starting from 0 will then be appended to the\n"
			"    output filename, just before the \"pgx\" extension. If a PGM filename\n"
			"    is given and there are more than one component, only the first component\n"
			"    will be written to the file.\n");
	fprintf(stdout, "  [-a | -OutDir] <output directory>\n"
			"    Output directory where decompressed files will be stored.\n");
	fprintf(stdout, "  [-g | -PluginPath] <plugin path>\n"
			"    Path to T1 plugin.\n");
	fprintf(stdout, "  [-H | -num_threads] <number of threads>\n"
			"    Number of threads used by libgrokj2k library.\n");
	fprintf(stdout,	"  [-c|-Compression] <compression method>\n"
					"	Compress output image data. Currently, this option is only applicable when\n"
					"	output format is set to TIF. Possible values are:\n"
					"	{NONE, LZW,JPEG, PACKBITS. ZIP,LZMA,ZSTD,WEBP}. Default value is NONE.\n");
	fprintf(stdout,
			"   [L|-CompressionLevel] <compression level>\n"
			"    \"Quality\" of compression. Currently only implemented for PNG format.\n"
			"	Default value is set to 9 (Z_BEST_COMPRESSION).\n"
			"	Other options are 0 (Z_NO_COMPRESSION) and 1 (Z_BEST_SPEED)\n");
	fprintf(stdout, "  [-t | -TileIndex] <tile index>\n"
			"    Index of tile to be decoded\n");
	fprintf(stdout,
			"  [-d | -DecodeRegion] <x0,y0,x1,y1>\n"
			"    Top left-hand corner and bottom right-hand corner of region to be decoded.\n");
	fprintf(stdout,
			"  [-r | -Reduce] <reduce factor>\n"
			"    Set the number of highest resolution levels to be discarded. The\n"
			"    image resolution is effectively divided by 2 to the power of the\n"
			"    number of discarded levels. The reduce factor is limited by the\n"
			"    smallest total number of decomposition levels among tiles.\n"
			"  [-l | -Layer] <number of quality layers to decompress>\n"
			"    Set the maximum number of quality layers to decompress. If there are\n"
			"    fewer quality layers than the specified number, all the quality layers\n"
			"    are decoded.\n");
	fprintf(stdout,
			"  [-p | -Precision] <comp 0 precision>[C|S][,<comp 1 precision>[C|S][,...]]\n"
			"    OPTIONAL\n"
			"    Force the precision (bit depth) of components.\n");
	fprintf(stdout,
			"There shall be at least 1 value. There is no limit to the number of values\n"
			"(comma separated, values whose count exceeds component count will be ignored).\n"
			"    If there are fewer values than components, the last value is used for remaining components.\n"
			"    If 'C' is specified (default), values are clipped.\n"
			"    If 'S' is specified, values are scaled.\n"
			"    A 0 value can be specified (meaning original bit depth).\n");
	fprintf(stdout,
			"  [-f | -force-rgb]\n"
			"    Force output image colorspace to RGB\n"
			"  [-u | -upsample]\n"
			"    components will be upsampled to image size\n"
			"  [-s | -split-pnm]\n"
			"    Split output components to different files when writing to PNM\n");
	fprintf(stdout,
			"  [-X | -XML] <xml file name> \n"
			"    Store XML metadata to file. File name will be set to \"xml file name\" + \".xml\"\n");
	fprintf(stdout,
			"  [-W | -logfile] <log file name>\n"
			"    log to file. File name will be set to \"log file name\"\n");
	fprintf(stdout, "\n");
}

bool GrkDecompress::parse_precision(const char *option,
		grk_decompress_parameters *parameters) {
	const char *remaining = option;
	bool result = true;

	/* reset */
	if (parameters->precision) {
		free(parameters->precision);
		parameters->precision = nullptr;
	}
	parameters->nb_precision = 0U;

	for (;;) {
		int prec;
		char mode;
		char comma;
		int count;

		count = sscanf(remaining, "%d%c%c", &prec, &mode, &comma);
		if (count == 1) {
			mode = 'C';
			count++;
		}
		if ((count == 2) || (mode == ',')) {
			if (mode == ',') {
				mode = 'C';
			}
			comma = ',';
			count = 3;
		}
		if (count == 3) {
			if ((prec < 1) || (prec > 32)) {
				spdlog::error("Invalid precision {} in precision option {}",
						prec, option);
				result = false;
				break;
			}
			if ((mode != 'C') && (mode != 'S')) {
				spdlog::error(
						"Invalid precision mode %c in precision option {}",
						mode, option);
				result = false;
				break;
			}
			if (comma != ',') {
				spdlog::error("Invalid character %c in precision option {}",
						comma, option);
				result = false;
				break;
			}

			if (parameters->precision == nullptr) {
				/* first one */
				parameters->precision = (grk_precision*) malloc(
						sizeof(grk_precision));
				if (parameters->precision == nullptr) {
					spdlog::error(
							"Could not allocate memory for precision option");
					result = false;
					break;
				}
			} else {
				uint32_t new_size = parameters->nb_precision + 1U;
				grk_precision *new_prec;

				if (new_size == 0U) {
					spdlog::error(
							"Could not allocate memory for precision option");
					result = false;
					break;
				}

				new_prec = (grk_precision*) realloc(parameters->precision,
						new_size * sizeof(grk_precision));
				if (new_prec == nullptr) {
					spdlog::error(
							"Could not allocate memory for precision option");
					result = false;
					break;
				}
				parameters->precision = new_prec;
			}

			parameters->precision[parameters->nb_precision].prec =
					(uint32_t) prec;
			switch (mode) {
			case 'C':
				parameters->precision[parameters->nb_precision].mode =
						GRK_PREC_MODE_CLIP;
				break;
			case 'S':
				parameters->precision[parameters->nb_precision].mode =
						GRK_PREC_MODE_SCALE;
				break;
			default:
				break;
			}
			parameters->nb_precision++;

			remaining = strchr(remaining, ',');
			if (remaining == nullptr) {
				break;
			}
			remaining += 1;
		} else {
			spdlog::error("Could not parse precision option {}", option);
			result = false;
			break;
		}
	}

	return result;
}

int GrkDecompress::load_images(grk_dircnt *dirptr, char *imgdirpath) {
	DIR *dir;
	struct dirent *content;
	int i = 0;

	/*Reading the input images from given input directory*/

	dir = opendir(imgdirpath);
	if (!dir) {
		spdlog::error("Could not open Folder {}", imgdirpath);
		return 1;
	}

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

char GrkDecompress::get_next_file(std::string image_filename, grk_img_fol *img_fol,
		grk_img_fol *out_fol, grk_decompress_parameters *parameters) {
	spdlog::info("File: \"{}\"", image_filename.c_str());
	std::string infilename = img_fol->imgdirpath
			+ std::string(get_path_separator()) + image_filename;
	if (!grk::jpeg2000_file_format(infilename.c_str(),
			(GRK_SUPPORTED_FILE_FMT*) &parameters->decod_format)
			|| parameters->decod_format == GRK_UNK_FMT)
		return 1;
	if (grk::strcpy_s(parameters->infile, sizeof(parameters->infile),
			infilename.c_str()) != 0) {
		return 1;
	}
	auto pos = image_filename.find(".");
	if (pos == std::string::npos)
		return 1;
	std::string temp_ofname = image_filename.substr(0, pos);
	if (img_fol->set_out_format) {
		std::string outfilename = out_fol->imgdirpath
				+ std::string(get_path_separator()) + temp_ofname + "."
				+ img_fol->out_format;
		if (grk::strcpy_s(parameters->outfile, sizeof(parameters->outfile),
				outfilename.c_str()) != 0) {
			return 1;
		}
	}
	return 0;
}

/* -------------------------------------------------------------------------- */

class GrokOutput: public StdOutput {
public:
	virtual void usage(CmdLineInterface &c) {
		(void) c;
		decode_help_display();
	}
};

/**
 * Convert compression string to compression code. (use TIFF codes)
 */
uint32_t GrkDecompress::getCompressionCode(const std::string &compressionString){
	if (compressionString == "NONE")
		return 0;
	else if (compressionString == "LZW")
		return 5;
	else if (compressionString == "JPEG")
		return 7;
	else if (compressionString == "PACKBITS")
		return 32773;
	else if (compressionString == "ZIP")
		return 8;
	else if (compressionString == "LZMA")
		return 34925;
	else if (compressionString == "ZSTD")
		return 50000;
	else if (compressionString == "WEBP")
		return 50001;
	else
		return UINT_MAX;
}

/* -------------------------------------------------------------------------- */
/**
 * Parse the command line
 */
/* -------------------------------------------------------------------------- */
int GrkDecompress::parse_cmdline_decoder(int argc, char **argv,
		grk_decompress_parameters *parameters, grk_img_fol *img_fol,
		grk_img_fol *out_fol, char *plugin_path) {
	try {
		CmdLine cmd("grk_decompress command line", ' ', grk_version());

		// set the output
		GrokOutput output;
		cmd.setOutput(&output);

		ValueArg<std::string> logfileArg("W", "logfile", "Log file",
				false, "", "string", cmd);

		ValueArg<std::string> imgDirArg("y", "ImgDir", "Image Directory", false, "",
				"string", cmd);
		ValueArg<std::string> outDirArg("a", "OutDir", "Output Directory", false, "",
				"string", cmd);
		ValueArg<std::string> outForArg("O", "OutFor", "Output Format", false, "",
				"string", cmd);

		SwitchArg forceRgbArg("f", "force-rgb", "Force RGB", cmd);
		SwitchArg upsampleArg("u", "upsample", "Upsample", cmd);
		SwitchArg splitPnmArg("s", "split-pnm", "Split PNM", cmd);
		ValueArg<std::string> pluginPathArg("g", "PluginPath", "Plugin path", false,
				"", "string", cmd);
		ValueArg<uint32_t> numThreadsArg("H", "num_threads",
				"Number of threads", false, 0, "unsigned integer", cmd);
		ValueArg<std::string> inputFileArg("i", "InputFile", "Input file", false, "",
				"string", cmd);
		ValueArg<std::string> outputFileArg("o", "OutputFile", "Output file", false,
				"", "string", cmd);
		ValueArg<uint32_t> reduceArg("r", "Reduce", "Reduce resolutions", false,
				0, "unsigned integer", cmd);
		ValueArg<uint16_t> layerArg("l", "Layer", "Layer", false, 0,
				"unsigned integer", cmd);
		ValueArg<uint32_t> tileArg("t", "TileIndex", "Input tile index", false,
				0, "unsigned integer", cmd);
		ValueArg<std::string> precisionArg("p", "Precision", "Force precision",
				false, "", "string", cmd);
		ValueArg<std::string> decodeRegionArg("d", "DecodeRegion", "Decode Region",
				false, "", "string", cmd);
		ValueArg<std::string> compressionArg("c", "Compression",
				"Compression Type", false, "", "string", cmd);
		ValueArg<uint32_t> compressionLevelArg("L", "CompressionLevel",
				"Compression Level", false, UINT_MAX, "unsigned integer", cmd);
		ValueArg<uint32_t> durationArg("z", "Duration", "Duration in seconds",
				false, 0, "unsigned integer", cmd);

		ValueArg<int32_t> deviceIdArg("G", "DeviceId", "Device ID", false, 0,
				"integer", cmd);

		SwitchArg xmlArg("X", "XML", "XML metadata", cmd);

		// Kernel build flags:
		// 1 indicates build binary, otherwise load binary
		// 2 indicates generate binaries
		ValueArg<uint32_t> kernelBuildOptionsArg("k", "KernelBuild",
				"Kernel build options", false, 0, "unsigned integer", cmd);

		ValueArg<uint32_t> repetitionsArg("e", "Repetitions",
				"Number of compress repetitions, for either a folder or a single file",
				false, 0, "unsigned integer", cmd);

		SwitchArg verboseArg("v", "verbose", "Verbose", cmd);
		cmd.parse(argc, argv);

		parameters->verbose = verboseArg.isSet();
		if (!parameters->verbose)
			spdlog::set_level(spdlog::level::level_enum::err);

		if (logfileArg.isSet()){
		    auto file_logger = spdlog::basic_logger_mt("grk_decompress", logfileArg.getValue());
		    spdlog::set_default_logger(file_logger);
		}

		parameters->serialize_xml = xmlArg.isSet();
		parameters->force_rgb = forceRgbArg.isSet();
		if (upsampleArg.isSet()) {
			if (reduceArg.isSet())
				spdlog::warn(
						"Cannot upsample when reduce argument set. Ignoring");
			else
				parameters->upsample = true;
		}
		parameters->split_pnm = splitPnmArg.isSet();
		if (compressionArg.isSet()) {
			uint32_t comp = getCompressionCode(compressionArg.getValue());
			if (comp == UINT_MAX)
				spdlog::warn("Unrecognized compression {}. Ignoring", compressionArg.getValue());
			else
				parameters->compression = comp;
		}
		if (compressionLevelArg.isSet()) {
			parameters->compressionLevel = compressionLevelArg.getValue();
		}
		// process
		if (inputFileArg.isSet()) {
			const char *infile = inputFileArg.getValue().c_str();
			// for debugging purposes, set to false
			bool checkFile = true;

			if (checkFile) {
				if (!jpeg2000_file_format(infile,
						(GRK_SUPPORTED_FILE_FMT*) &parameters->decod_format)) {
					spdlog::error("Unable to open file {} for decoding.",
							infile);
					return 1;
				}
				switch (parameters->decod_format) {
				case GRK_J2K_FMT:
					break;
				case GRK_JP2_FMT:
					break;
				default:
					spdlog::error(
							"Unknown input file format: {} \n"
									"        Known file formats are *.j2k, *.jp2 or *.jpc",
							infile);
					return 1;
				}
			} else {
				parameters->decod_format = GRK_J2K_FMT;
			}
			if (grk::strcpy_s(parameters->infile, sizeof(parameters->infile),
					infile) != 0) {
				spdlog::error("Path is too long");
				return 1;
			}
		}

		// disable verbose mode when writing to stdout
		if (parameters->verbose && outForArg.isSet() && !outputFileArg.isSet()
				&& !outDirArg.isSet()) {
			spdlog::warn(
					" Verbose mode is automatically disabled when decompressing to stdout");
			parameters->verbose = false;
		}

		if (outForArg.isSet()) {
			char outformat[50];
			const char *of = outForArg.getValue().c_str();
			sprintf(outformat, ".%s", of);
			img_fol->set_out_format = true;
			parameters->cod_format = (GRK_SUPPORTED_FILE_FMT)get_file_format(outformat);
			switch (parameters->cod_format) {
			case GRK_PGX_FMT:
				img_fol->out_format = "pgx";
				break;
			case GRK_PXM_FMT:
				img_fol->out_format = "ppm";
				break;
			case GRK_BMP_FMT:
				img_fol->out_format = "bmp";
				break;
			case GRK_JPG_FMT:
				img_fol->out_format = "jpg";
				break;
			case GRK_TIF_FMT:
				img_fol->out_format = "tif";
				break;
			case GRK_RAW_FMT:
				img_fol->out_format = "raw";
				break;
			case GRK_RAWL_FMT:
				img_fol->out_format = "rawl";
				break;
			case GRK_PNG_FMT:
				img_fol->out_format = "png";
				break;
			default:
				spdlog::error(
						"Unknown output format image {} [only *.png, *.pnm, *.pgm, *.ppm, *.pgx, *.bmp, *.tif, *.jpg, *.jpeg, *.raw or *.rawl]",
						outformat);
				return 1;
			}
		}

		if (outputFileArg.isSet()) {
			const char *outfile = outputFileArg.getValue().c_str();
			parameters->cod_format =
					(GRK_SUPPORTED_FILE_FMT)get_file_format(outfile);
			switch (parameters->cod_format) {
			case GRK_PGX_FMT:
			case GRK_PXM_FMT:
			case GRK_BMP_FMT:
			case GRK_TIF_FMT:
			case GRK_RAW_FMT:
			case GRK_RAWL_FMT:
			case GRK_PNG_FMT:
			case GRK_JPG_FMT:
				break;
			default:
				spdlog::error(
						"Unknown output format image {} [only *.png, *.pnm, *.pgm, *.ppm, *.pgx, *.bmp, *.tif, *.tiff, *jpg, *jpeg, *.raw or *rawl]",
						outfile);
				return 1;
			}
			if (grk::strcpy_s(parameters->outfile, sizeof(parameters->outfile),
					outfile) != 0) {
				spdlog::error("Path is too long");
				return 1;
			}
		} else {
			// check for possible output to STDOUT
			if (!imgDirArg.isSet()) {
				bool toStdout =
						outForArg.isSet()
								&& grk::supportedStdioFormat(
										(GRK_SUPPORTED_FILE_FMT) parameters->cod_format);
				if (!toStdout) {
					spdlog::error("Missing output file");
					return 1;
				}
			}

		}
		if (outDirArg.isSet()) {
			if (out_fol) {
				out_fol->imgdirpath = (char*) malloc(
						strlen(outDirArg.getValue().c_str()) + 1);
				strcpy(out_fol->imgdirpath, outDirArg.getValue().c_str());
				out_fol->set_imgdir = true;
			}
		}

		if (imgDirArg.isSet()) {
			img_fol->imgdirpath = (char*) malloc(
					strlen(imgDirArg.getValue().c_str()) + 1);
			strcpy(img_fol->imgdirpath, imgDirArg.getValue().c_str());
			img_fol->set_imgdir = true;
		}

		if (reduceArg.isSet()) {
			parameters->core.cp_reduce = reduceArg.getValue();
		}
		if (layerArg.isSet()) {
			parameters->core.cp_layer = layerArg.getValue();
		}
		if (tileArg.isSet()) {
			parameters->tile_index = (uint16_t) tileArg.getValue();
			parameters->nb_tile_to_decode = 1;
		}
		if (precisionArg.isSet()) {
			if (!parse_precision(precisionArg.getValue().c_str(), parameters))
				return 1;
		}
		if (numThreadsArg.isSet()) {
			parameters->numThreads = numThreadsArg.getValue();
		}

		if (decodeRegionArg.isSet()) {
			size_t size_optarg = (size_t) strlen(
					decodeRegionArg.getValue().c_str()) + 1U;
			char *ROI_values = (char*) malloc(size_optarg);
			if (ROI_values == nullptr) {
				spdlog::error("Couldn't allocate memory");
				return 1;
			}
			ROI_values[0] = '\0';
			memcpy(ROI_values, decodeRegionArg.getValue().c_str(), size_optarg);
			/*printf("ROI_values = %s [%d / %d]\n", ROI_values, strlen(ROI_values), size_optarg ); */
			int rc = parse_DA_values(ROI_values,
									&parameters->DA_x0,
									&parameters->DA_y0,
									&parameters->DA_x1,
									&parameters->DA_y1);
			free(ROI_values);
			if (rc)
				return 1;
		}

		if (pluginPathArg.isSet()) {
			if (plugin_path)
				strcpy(plugin_path, pluginPathArg.getValue().c_str());
		}

		if (repetitionsArg.isSet()) {
			parameters->repeats = repetitionsArg.getValue();
		}

		if (kernelBuildOptionsArg.isSet()) {
			parameters->kernelBuildOptions = kernelBuildOptionsArg.getValue();
		}

		if (deviceIdArg.isSet()) {
			parameters->deviceId = deviceIdArg.getValue();
		}

		if (durationArg.isSet()) {
			parameters->duration = durationArg.getValue();
		}

	} catch (ArgException &e)  // catch any exceptions
	{
		std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
		return 1;
	}
#if 0
    case 'h': 			/* display an help description */
        decode_help_display();
        return 1;
#endif

	/* check for possible errors */
	if (img_fol->set_imgdir) {
		if (!(parameters->infile[0] == 0)) {
			spdlog::error("options -ImgDir and -i cannot be used together.");
			return 1;
		}
		if (!img_fol->set_out_format) {
			spdlog::error(
					"When -ImgDir is used, -OutFor <FORMAT> must be used.");
			spdlog::error(
					"Only one format allowed.\n"
							"Valid format are PGM, PPM, PNM, PGX, BMP, TIF and RAW.");
			return 1;
		}
		if (!((parameters->outfile[0] == 0))) {
			spdlog::error("options -ImgDir and -o cannot be used together.");
			return 1;
		}
	} else {
		if (parameters->decod_format == GRK_UNK_FMT) {
			if ((parameters->infile[0] == 0) || (parameters->outfile[0] == 0)) {
				spdlog::error("Required parameters are missing\n"
						"Example: {} -i image.j2k -o image.pgm", argv[0]);
				spdlog::error("   Help: {} -h", argv[0]);
				return 1;
			}
		}
	}
	return 0;
}
void GrkDecompress::set_default_parameters(grk_decompress_parameters *parameters) {
	if (parameters) {
		memset(parameters, 0, sizeof(grk_decompress_parameters));

		/* default decoding parameters (command line specific) */
		parameters->decod_format = GRK_UNK_FMT;
		parameters->cod_format = GRK_UNK_FMT;

		/* default decoding parameters (core) */
		grk_set_default_decompress_params(&(parameters->core));
		parameters->deviceId = 0;
		parameters->repeats = 1;
		parameters->compressionLevel = GRK_DECOMPRESS_COMPRESSION_LEVEL_DEFAULT;
	}

}

void GrkDecompress::destroy_parameters(grk_decompress_parameters *parameters) {
	if (parameters) {
		if (parameters->precision) {
			free(parameters->precision);
			parameters->precision = nullptr;
		}
	}
}

#ifdef GROK_HAVE_LIBLCMS
void MycmsLogErrorHandlerFunction(cmsContext ContextID,
		cmsUInt32Number ErrorCode, const char *Text) {
	(void) ContextID;
	(void) ErrorCode;
	spdlog::warn(" LCMS error: {}", Text);
}
#endif

static int decode_callback(grk_plugin_decode_callback_info *info);

// returns 0 for failure, 1 for success, and 2 if file is not suitable for decoding
int GrkDecompress::decompress(const char *fileName, DecompressInitParams *initParams) {
	if (initParams->img_fol.set_imgdir) {
		if (get_next_file(fileName, &initParams->img_fol,
				initParams->out_fol.set_imgdir ?
						&initParams->out_fol : &initParams->img_fol,
				&initParams->parameters)) {
			return 2;
		}
	}

	grk_plugin_decode_callback_info info;
	memset(&info, 0, sizeof(grk_plugin_decode_callback_info));
	info.decod_format = GRK_UNK_FMT;
	info.cod_format = GRK_UNK_FMT;
	info.decode_flags = GRK_DECODE_ALL;
	info.decoder_parameters = &initParams->parameters;
	info.user_data = this;

	if (pre_decode(&info)) {
		return 0;
	}
	if (post_decode(&info)) {
		return 0;
	}
	return 1;
}


int GrkDecompress::plugin_main(int argc, char **argv, DecompressInitParams *initParams) {
	uint32_t num_images = 0, imageno = 0;
	grk_dircnt *dirptr = nullptr;
	int32_t success = 0;
	uint32_t num_decompressed_images = 0;
	bool isBatch = false;
	std::chrono::time_point<std::chrono::high_resolution_clock> start, finish;
	std::chrono::duration<double> elapsed;

#ifdef GROK_HAVE_LIBLCMS
	cmsSetLogErrorHandler(MycmsLogErrorHandlerFunction);
#endif

	/* set decoding parameters to default values */
	set_default_parameters(&initParams->parameters);

	/* parse input and get user encoding parameters */
	if (parse_cmdline_decoder(argc, argv, &initParams->parameters,
			&initParams->img_fol, &initParams->out_fol, initParams->plugin_path)
			== 1) {
		return EXIT_FAILURE;
	}


#ifdef GROK_HAVE_LIBTIFF
	tiffSetErrorAndWarningHandlers(initParams->parameters.verbose);
#endif
#ifdef GROK_HAVE_LIBPNG
	pngSetVerboseFlag(initParams->parameters.verbose);
#endif
	initParams->initialized = true;

	// loads plugin but does not actually create codec
	if (!grk_initialize(initParams->plugin_path,
			initParams->parameters.numThreads)) {
		success = 1;
		goto cleanup;
	}
	// create codec
	grk_plugin_init_info initInfo;
	initInfo.deviceId = initParams->parameters.deviceId;
	initInfo.verbose = initParams->parameters.verbose;
	if (!grk_plugin_init(initInfo)) {
		success = 1;
		goto cleanup;
	}

	isBatch = initParams->img_fol.imgdirpath && initParams->out_fol.imgdirpath;
	if ((grk_plugin_get_debug_state() & GRK_PLUGIN_STATE_DEBUG)) {
		isBatch = false;
	}
	if (isBatch) {
		//initialize batch
		setup_signal_handler();
		success = grk_plugin_init_batch_decompress(initParams->img_fol.imgdirpath,
				initParams->out_fol.imgdirpath, &initParams->parameters,
				decode_callback);
		//start batch
		if (success)
			success = grk_plugin_batch_decompress();
		// if plugin successfully begins batch compress, then wait for batch to complete
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
			grk_plugin_stop_batch_decompress();
		}
	} else {
		/* Initialize reading of directory */
		if (initParams->img_fol.set_imgdir) {
			num_images = get_num_images(initParams->img_fol.imgdirpath);
			if (num_images == 0) {
				spdlog::error("Folder is empty");
				success = 1;
				goto cleanup;
			}
			dirptr = (grk_dircnt*) malloc(sizeof(grk_dircnt));
			if (dirptr) {
				dirptr->filename_buf = (char*) malloc(
						(size_t) num_images * GRK_PATH_LEN); /* Stores at max 10 image file names*/
				if (!dirptr->filename_buf) {
					success = 1;
					goto cleanup;
				}
				dirptr->filename = (char**) malloc((size_t)num_images * sizeof(char*));
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
				success = 1;
				goto cleanup;
			}
		} else {
			num_images = 1;
		}
	}

	start = std::chrono::high_resolution_clock::now();

	/*Decoding image one by one*/
	for (imageno = 0; imageno < num_images; imageno++) {
		if (initParams->img_fol.set_imgdir) {
			if (get_next_file(dirptr->filename[imageno], &initParams->img_fol,
					initParams->out_fol.set_imgdir ?
							&initParams->out_fol : &initParams->img_fol,
					&initParams->parameters)) {
				continue;
			}
		}

		//1. try to decompress using plugin
		success = grk_plugin_decompress(&initParams->parameters, decode_callback);
		if (success != 0)
			goto cleanup;
		num_decompressed_images++;

	}
	finish = std::chrono::high_resolution_clock::now();
	elapsed = finish - start;
	if (num_decompressed_images && success == 0) {
		spdlog::info("decompress time: {} ms/image",
				(elapsed.count() * 1000) / (double) num_decompressed_images);
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


int decode_callback(grk_plugin_decode_callback_info *info) {
	int rc = -1;
	// GRK_DECODE_T1 flag specifies full decompress on CPU, so
	// we don't need to initialize the decoder in this case
	if (info->decode_flags & GRK_DECODE_T1) {
		info->init_decoders_func = nullptr;
	}
	if (info->decode_flags & GRK_PLUGIN_DECODE_CLEAN) {
		if (info->l_stream)
			grk_stream_destroy(info->l_stream);
		info->l_stream = nullptr;
		if (info->l_codec)
			grk_destroy_codec(info->l_codec);
		info->l_codec = nullptr;
		if (info->image && !info->plugin_owns_image) {
			grk_image_destroy(info->image);
			info->image = nullptr;
		}
		rc = 0;
	}
	auto decompressor = (GrkDecompress*)info->user_data;
	if (info->decode_flags & (GRK_DECODE_HEADER |
									GRK_DECODE_T1 |
									GRK_DECODE_T2)) {
		rc = decompressor->pre_decode(info);
		if (rc)
			return rc;
	}
	if (info->decode_flags & GRK_DECODE_POST_T1) {
		rc = decompressor->post_decode(info);
	}
	return rc;
}

enum grk_stream_type {
	GRK_FILE_STREAM, GRK_MAPPED_FILE_STREAM
};

// return: 0 for success, non-zero for failure
int GrkDecompress::pre_decode(grk_plugin_decode_callback_info *info) {
	if (!info)
		return 1;
	bool failed = true;
	bool useMemoryBuffer = false;
	auto parameters = info->decoder_parameters;
	if (!parameters)
		return 1;
	auto infile =
			info->input_file_name ? info->input_file_name : parameters->infile;
	int decod_format =
			info->decod_format != GRK_UNK_FMT ?
					info->decod_format : parameters->decod_format;

	GRK_SUPPORTED_FILE_FMT cod_format = (GRK_SUPPORTED_FILE_FMT) (
			info->cod_format != GRK_UNK_FMT ?
					info->cod_format : parameters->cod_format);

	switch (cod_format) {
			case GRK_PXM_FMT:
				imageFormat = new PNMFormat(parameters->split_pnm);
				break;
			case GRK_PGX_FMT:
				imageFormat = new PGXFormat();
				break;
			case GRK_BMP_FMT:
				imageFormat = new BMPFormat();
				break;
	#ifdef GROK_HAVE_LIBTIFF
			case GRK_TIF_FMT:
				imageFormat = new TIFFFormat();
				break;
	#endif
			case GRK_RAW_FMT:
				imageFormat = new RAWFormat(true);
				break;
			case GRK_RAWL_FMT:
				imageFormat = new RAWFormat(false);
				break;
	#ifdef GROK_HAVE_LIBJPEG
			case GRK_JPG_FMT:
				imageFormat = new JPEGFormat();
				break;
	#endif

	#ifdef GROK_HAVE_LIBPNG
			case GRK_PNG_FMT:
				imageFormat = new PNGFormat();
				break;
	#endif
			default:
				spdlog::error("Unsupported output format {}",cod_format);
				goto cleanup;
				break;
	}


	//1. initialize
	if (!info->l_stream) {
		if (useMemoryBuffer) {
			// Reading value from file
			auto in = fopen(infile, "r");
			if (in) {
				fseek(in, 0L, SEEK_END);
				long int sz = ftell(in);
				if (sz == -1){
					spdlog::error("grk_decompress: ftell error from file {}", sz, infile);
					goto cleanup;
				}
				rewind(in);
				auto memoryBuffer = new uint8_t[(size_t)sz];
				size_t ret = fread(memoryBuffer, 1, (size_t)sz, in);
				if (ret != (size_t)sz){
					spdlog::error("grk_decompress: error reading {} bytes from file {}", sz, infile);
					goto cleanup;
				}
				int rc = fclose(in);
				if (rc){
					spdlog::error("grk_decompress: error closing file {}", infile);
					goto cleanup;
				}
				if (ret == (size_t)sz)
					info->l_stream = grk_stream_create_mem_stream(memoryBuffer,
							(size_t)sz, true, true);
				else {
					spdlog::error("grk_decompress: failed to create memory stream for file {}", infile);
					goto cleanup;
				}
			} else {
				goto cleanup;
			}
		} else {
			info->l_stream = grk_stream_create_mapped_file_stream(infile, true);
		}
	}
	if (!info->l_stream) {
		spdlog::error("grk_decompress: failed to create a stream from file {}", infile);
		goto cleanup;
	}

	if (!info->l_codec) {
		switch (decod_format) {
		case GRK_J2K_FMT: { /* JPEG 2000 code stream */
			info->l_codec = grk_create_decompress(GRK_CODEC_J2K,
					info->l_stream);
			break;
		}
		case GRK_JP2_FMT: { /* JPEG 2000 compressed image data */
			info->l_codec = grk_create_decompress(GRK_CODEC_JP2,
					info->l_stream);
			break;
		}
		default:
			spdlog::error("grk_decompress: unknown decode format {}", decod_format);
			goto cleanup;
		}
		/* catch events using our callbacks and give a local context */
		if (parameters->verbose) {
			grk_set_info_handler(info_callback, nullptr);
			grk_set_warning_handler(warning_callback, nullptr);
		}
		grk_set_error_handler(error_callback, nullptr);

		if (!grk_init_decompress(info->l_codec, &(parameters->core))) {
			spdlog::error("grk_decompress: failed to set up the decoder");
			goto cleanup;
		}
	}

	// 2. read header
	if (info->decode_flags & GRK_DECODE_HEADER) {
		// Read the main header of the code stream (j2k) and also JP2 boxes (jp2)
		if (!grk_read_header(info->l_codec, &info->header_info, &info->image)) {
			spdlog::error("grk_decompress: failed to read the header");
			goto cleanup;
		}

		// do not allow odd top left region coordinates for SYCC
		if (info->header_info.enumcs == GRK_ENUM_CLRSPC_SYCC){
			bool adjustX = (info->decoder_parameters->DA_x0 != info->full_image_x0) &&
					(info->decoder_parameters->DA_x0 & 1);
			bool adjustY = (info->decoder_parameters->DA_y0 != info->full_image_y0) &&
						(info->decoder_parameters->DA_y0 & 1);
			if (adjustX || adjustY){
				spdlog::error("grk_decompress: Top left-hand region coordinates that do not coincide\n"
						"with respective top left-hand image coordinates must be even");
				goto cleanup;
			}
		}

		// store XML to file
		if (info->header_info.xml_data && info->header_info.xml_data_len
				&& parameters->serialize_xml) {
			std::string xmlFile = std::string(parameters->outfile) + ".xml";
			auto fp = fopen(xmlFile.c_str(), "wb");
			if (!fp) {
				spdlog::error(
						"grk_decompress: unable to open file {} for writing xml to",
						xmlFile.c_str());
				goto cleanup;
			}
			if (fwrite(info->header_info.xml_data, 1,
							info->header_info.xml_data_len, fp)
							!= info->header_info.xml_data_len) {
				spdlog::error(
						"grk_decompress: unable to write all xml data to file {}",
						xmlFile.c_str());
				fclose(fp);
				goto cleanup;
			}
			if (!grk::safe_fclose(fp)) {
				spdlog::error("grk_decompress: error closing file {}",infile);
				goto cleanup;
			}
		}
		if (info->init_decoders_func)
			return info->init_decoders_func(&info->header_info, info->image);
	}

	if (info->image){
		info->full_image_x0 = info->image->x0;
		info->full_image_y0 = info->image->y0;
	}

	// header-only decompress
	if (info->decode_flags == GRK_DECODE_HEADER)
		goto cleanup;


	//3. decompress
	if (info->tile)
		info->tile->decode_flags = info->decode_flags;

	// limit to 16 bit precision
	for (uint32_t i = 0; i < info->image->numcomps; ++i) {
		if (info->image->comps[i].prec > 16) {
			spdlog::error("grk_decompress: Precision = {} not supported:",
					info->image->comps[i].prec);
			goto cleanup;
		}
	}

	if (!grk_set_decompress_area(info->l_codec, info->image, parameters->DA_x0,
			parameters->DA_y0, parameters->DA_x1, parameters->DA_y1)) {
		spdlog::error("grk_decompress: failed to set the decoded area");
		goto cleanup;
	}

	// decompress all tiles
	if (!parameters->nb_tile_to_decode) {
		if (!(grk_decompress(info->l_codec, info->tile, info->image)
				&& grk_end_decompress(info->l_codec))) {
			spdlog::error("grk_decompress: failed to decompress image.");
			goto cleanup;
		}
	}
	// or, decompress one particular tile
	else {
		if (!grk_decompress_tile(info->l_codec, info->image,
				parameters->tile_index)) {
			spdlog::error("grk_decompress: failed to decompress tile");
			goto cleanup;
		}
		//spdlog::info("Tile {} was decoded.", parameters->tile_index);
	}
	failed = false;
cleanup:
	grk_stream_destroy(info->l_stream);
	info->l_stream = nullptr;
	grk_destroy_codec(info->l_codec);
	info->l_codec = nullptr;
	if (failed) {
		grk_image_destroy(info->image);
		info->image = nullptr;
		delete imageFormat;
		imageFormat = nullptr;
	}

	return failed ? 1 : 0;
}

/*
 Post-process decompressed image and store in selected image format
 */
int GrkDecompress::post_decode(grk_plugin_decode_callback_info *info) {
	if (!info)
		return -1;
	bool oddFirstX = info->full_image_x0 & 1;
	bool oddFirstY = info->full_image_y0 & 1;

	bool regionDecode = info->decoder_parameters->DA_x1 > info->decoder_parameters->DA_x0 &&
							info->decoder_parameters->DA_y1 > info->decoder_parameters->DA_y0;

	if (regionDecode) {
		if (info->decoder_parameters->DA_x0 != info->image->x0 )
			oddFirstX = false;
		if (info->decoder_parameters->DA_y0 != info->image->y0 )
			oddFirstY = false;
	}

	auto fmt = imageFormat;

	bool failed = true;
	bool canStoreICC = false;
	bool isTiff = info->decoder_parameters->cod_format == GRK_TIF_FMT;
	grk_decompress_parameters *parameters = info->decoder_parameters;
	auto image = info->image;
	bool canStoreCIE = isTiff && image->color_space == GRK_CLRSPC_DEFAULT_CIE;
	bool isCIE = image->color_space == GRK_CLRSPC_DEFAULT_CIE
			|| image->color_space == GRK_CLRSPC_CUSTOM_CIE;
	const char *infile =
			info->decoder_parameters->infile[0] ?
					info->decoder_parameters->infile : info->input_file_name;
	const char *outfile =
			info->decoder_parameters->outfile[0] ?
					info->decoder_parameters->outfile : info->output_file_name;

	GRK_SUPPORTED_FILE_FMT cod_format = (GRK_SUPPORTED_FILE_FMT) (
			info->cod_format != GRK_UNK_FMT ?
					info->cod_format : parameters->cod_format);

	if (image->color_space != GRK_CLRSPC_SYCC && image->numcomps == 3
			&& image->comps[0].dx == image->comps[0].dy
			&& image->comps[1].dx != 1)
		image->color_space = GRK_CLRSPC_SYCC;
	else if (image->numcomps <= 2)
		image->color_space = GRK_CLRSPC_GRAY;
	if (image->color_space == GRK_CLRSPC_SYCC ||
			image->color_space == GRK_CLRSPC_EYCC){
		if (image->numcomps != 3){
			spdlog::error("grk_decompress: YCC: number of components {} "
					"not equal to 3 ", image->numcomps);
			goto cleanup;
		}
	}
	if (image->color_space == GRK_CLRSPC_SYCC) {
		if (!isTiff || info->decoder_parameters->force_rgb) {
			if (!color_sycc_to_rgb(image, oddFirstX, oddFirstY))
				spdlog::warn("grk_decompress: sYCC to RGB colour conversion failed");
		}
	} else if (image->color_space == GRK_CLRSPC_EYCC) {
		if (!color_esycc_to_rgb(image))
			spdlog::warn("grk_decompress: eYCC to RGB colour conversion failed");
	} else if (image->color_space == GRK_CLRSPC_CMYK) {
		if (!isTiff || info->decoder_parameters->force_rgb) {
			if (!color_cmyk_to_rgb(image))
				spdlog::warn("grk_decompress: CMYK to RGB colour conversion failed");
		}
	}
	if (image->xmp_buf) {
		bool canStoreXMP = (info->decoder_parameters->cod_format == GRK_TIF_FMT
				|| info->decoder_parameters->cod_format == GRK_PNG_FMT);
		if (!canStoreXMP) {
			spdlog::warn(
					" Input file {} contains XMP meta-data,\nbut the file format for output file {} does not support storage of this data.",
					infile, outfile);
		}
	}
	if (image->iptc_buf) {
		bool canStoreIPTC_IIM = (info->decoder_parameters->cod_format
				== GRK_TIF_FMT);
		if (!canStoreIPTC_IIM) {
			spdlog::warn(
					" Input file {} contains legacy IPTC-IIM meta-data,\nbut the file format for output file {} does not support storage of this data.",
					infile, outfile);
		}
	}
	if (image->icc_profile_buf) {
		if (isCIE) {
			if (!canStoreCIE || info->decoder_parameters->force_rgb) {
#if defined(GROK_HAVE_LIBLCMS)
				if (!info->decoder_parameters->force_rgb)
					spdlog::warn(
							" Input file {} is in CIE colour space,\n"
							"but the codec is unable to store this information in the "
							"output file {}.\n"
							"The output image will therefore be converted to sRGB before saving.",
							infile, outfile);
				if (color_cielab_to_rgb(image)){
					delete[] image->icc_profile_buf;
					image->icc_profile_buf = nullptr;
					image->icc_profile_len = 0;
				} else {
					spdlog::warn("Unable to convert L*a*b image to sRGB");
				}
#else
			spdlog::warn(" Input file is stored in CIELab colour space,"
					" but lcms library is not linked, so codec can't convert L*a*b to sRGB");
#endif
			}
		} else {
			// A TIFF,PNG or JPEG image can store the ICC profile,
			// so no need to apply it in this case,
			// (unless we are forcing to RGB).
			// Otherwise, we apply the profile
			canStoreICC = (info->decoder_parameters->cod_format == GRK_TIF_FMT
					|| info->decoder_parameters->cod_format == GRK_PNG_FMT
					|| info->decoder_parameters->cod_format == GRK_JPG_FMT
					|| info->decoder_parameters->cod_format == GRK_BMP_FMT);
			if (info->decoder_parameters->force_rgb || !canStoreICC) {
#if defined(GROK_HAVE_LIBLCMS)
				if (!info->decoder_parameters->force_rgb)
					spdlog::warn(
							" Input file {} contains a color profile,\n"
							"but the codec is unable to store this profile"
							" in the output file {}.\n"
							"The profile will therefore be applied to the output"
							" image before saving.",
							infile, outfile);
				color_apply_icc_profile(image,
						info->decoder_parameters->force_rgb);
				delete[] image->icc_profile_buf;
				image->icc_profile_buf = nullptr;
				image->icc_profile_len = 0;
#endif
			}
		}
	}

	if (parameters->precision != nullptr) {
		uint32_t compno;
		for (compno = 0; compno < image->numcomps; ++compno) {
			uint32_t precisionno = compno;
			if (precisionno >= parameters->nb_precision)
				precisionno = parameters->nb_precision - 1U;
			uint32_t prec = parameters->precision[precisionno].prec;
			if (prec == 0)
				prec = image->comps[compno].prec;

			switch (parameters->precision[precisionno].mode) {
			case GRK_PREC_MODE_CLIP:
				clip_component(&(image->comps[compno]), prec);
				break;
			case GRK_PREC_MODE_SCALE:
				scale_component(&(image->comps[compno]), prec);
				break;
			default:
				break;
			}
		}
	}
	if (parameters->upsample) {
		image = upsample_image_components(image);
		if (image == nullptr) {
			spdlog::error(
					"grk_decompress: failed to upsample image components.");
			goto cleanup;
		}
	}

	if (parameters->force_rgb) {
		switch (image->color_space) {
		case GRK_CLRSPC_SRGB:
			break;
		case GRK_CLRSPC_GRAY:
			image = convert_gray_to_rgb(image);
			break;
		default:
			spdlog::error(
					"grk_decompress: don't know how to convert image to RGB colorspace.");
			grk_image_destroy(image);
			image = nullptr;
			goto cleanup;
		}
		if (image == nullptr) {
			spdlog::error("grk_decompress: failed to convert to RGB image.");
			goto cleanup;
		}
	}


	if (GrkDecompress::store_file_to_disk) {
		std::string outfileStr = outfile ? std::string(outfile) : "";
		uint32_t compressionParam = 0;
		if (cod_format == GRK_TIF_FMT)
			compressionParam = parameters->compression;
		else if (cod_format == GRK_JPG_FMT ||  cod_format == GRK_PNG_FMT)
			compressionParam = parameters->compressionLevel;
		if (!fmt->encodeHeader(image, outfileStr, compressionParam)) {
			spdlog::error("Outfile {} not generated", outfileStr);
			goto cleanup;
		}
		if (!fmt->encodeStrip(image->comps[0].h)) {
			spdlog::error("Outfile {} not generated", outfileStr);
			goto cleanup;
		}
		if (!fmt->encodeFinish()) {
			spdlog::error("Outfile {} not generated", outfileStr);
			goto cleanup;
		}

	}
	failed = false;
	cleanup:
	grk_stream_destroy(info->l_stream);
	info->l_stream = nullptr;
	grk_destroy_codec(info->l_codec);
	info->l_codec = nullptr;
	if (image && !info->plugin_owns_image) {
		grk_image_destroy(image);
		info->image = nullptr;
	}
	delete imageFormat;
	imageFormat = nullptr;
	if (failed) {
		if (outfile){
			bool allocated = false;
			char* p = actual_path(parameters->outfile, &allocated);
			(void)remove(p);
			if (allocated)
				free(p);
		}
	}

	return failed ? 1 : 0;
}

int GrkDecompress::main(int argc, char **argv) {
	int rc = EXIT_SUCCESS;
	uint32_t num_decompressed_images = 0;
	DecompressInitParams initParams;
	try {
		// try to decompress with plugin
		int plugin_rc = plugin_main(argc, argv, &initParams);

		// return immediately if either
		// initParams was not initialized (something was wrong with command line params)
		// or
		// plugin was successful
		if (!initParams.initialized) {
			rc = EXIT_FAILURE;
			goto cleanup;
		}
		if (plugin_rc == EXIT_SUCCESS) {
			rc = EXIT_SUCCESS;
			goto cleanup;
		}
		auto start = std::chrono::high_resolution_clock::now();
		for (uint32_t i = 0; i < initParams.parameters.repeats; ++i) {
			if (!initParams.img_fol.set_imgdir) {
				if (!decompress("", &initParams)) {
					rc = EXIT_FAILURE;
					goto cleanup;
				}
				num_decompressed_images++;
			} else {
				auto dir = opendir(initParams.img_fol.imgdirpath);
				if (!dir) {
					spdlog::error("Could not open Folder {}",
							initParams.img_fol.imgdirpath);
					rc = EXIT_FAILURE;
					goto cleanup;
				}
				struct dirent *content = nullptr;
				while ((content = readdir(dir)) != nullptr) {
					if (strcmp(".", content->d_name) == 0
							|| strcmp("..", content->d_name) == 0)
						continue;
					if (decompress(content->d_name, &initParams) == 1)
						num_decompressed_images++;
				}
				closedir(dir);
			}
		}
		auto finish = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> elapsed = finish - start;
		if (num_decompressed_images) {
			spdlog::info("decompress time: {} ms/image",
					(elapsed.count() * 1000)
							/ (double) num_decompressed_images);
		}
	} catch (std::bad_alloc &ba) {
		spdlog::error("Out of memory. Exiting.");
		rc = 1;
		goto cleanup;
	}
cleanup:
	destroy_parameters(&initParams.parameters);
	grk_deinitialize();
	return rc;
}

GrkDecompress::GrkDecompress() : store_file_to_disk(true),
								 imageFormat(nullptr)

{
}

GrkDecompress::~GrkDecompress(void)
{
	delete imageFormat;
	imageFormat = nullptr;
}

}

int main(int argc, char **argv) {
   grk::GrkDecompress decomp;
   return decomp.main(argc,argv);

}



