/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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

#include <filesystem>
#ifndef _WIN32
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
#include <climits>
#include <string>
#include <chrono>
#include <thread>

#include "grk_apps_config.h"
#include "common.h"
#include "grok.h"
#include "spdlog/spdlog.h"
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
#include "GrkDecompress.h"

namespace grk
{
void exit_func()
{
   grk_plugin_stop_batch_decompress();
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
void sig_handler([[maybe_unused]] int signum)
{
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

static void decompress_help_display(void)
{
   fprintf(stdout,
		   "grk_decompress - decompress JPEG 2000 codestream to various image formats.\n"
		   "This utility has been compiled against libgrokj2k v%s.\n\n",
		   grk_version());
   fprintf(stdout, "Supported input formats: `JP2` and `J2K\\J2C`\n");
   fprintf(stdout, "Supported input image extensions are `.jp2` and `.j2k\\.j2c`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Supported output formats are `JPEG`, `BMP`, `PNM`, `PGX`, `PNG`, `RAW` and\n");
   fprintf(stdout, "`TIFF`\n");
   fprintf(stdout, "Valid output image extensions are `jpg`, `.jpeg`, `.bmp`, `.pgm`, `.pgx`,\n");
   fprintf(stdout, "`.pnm`, `.ppm`, `.pam`, `.png`, `.raw`, `.rawl`, `.tif` and `.tiff`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "* For `PNG` the library must have `libpng` available.\n");
   fprintf(stdout, "* For `TIF\\\\TIFF` the library must have `libtiff` available.\n");
   fprintf(stdout, "* For `JPG\\\\JPEG` the library must have a `libjpeg` variant available.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " Limitations\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "* Grok supports up to and including 16 bit sample precision for decompression.\n");
   fprintf(stdout, "This is a subset of the ISO standard, which allows up to 38 bit precision.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " stdout\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "The decompresser can write output to `stdout` for the following formats:\n");
   fprintf(stdout,
		   "`BMP`,`PNG`, `JPG`, `PNM`, `RAW` and `RAWL`.  To enable writing to `stdout`,\n");
   fprintf(stdout,
		   "please ensure that the `-o` parameter is **not** present in the command line,\n");
   fprintf(stdout,
		   "and that the `-out_fmt` parameter is set to one of the supported formats listed\n");
   fprintf(stdout, "above. Note: the verbose flag `-v` will be ignored in this mode, as verbose\n");
   fprintf(stdout, "output would corrupt the output file.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " Embedded ICC Profile\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "If there is an embedded ICC profile in the input file, then the profile will be\n");
   fprintf(stdout,
		   "stored in the output file for `TIF\\TIFF`, `JPG`, `BMP` and `PNG` formats. For\n");
   fprintf(stdout,
		   "other formats, the profile will be applied to the decompressed image before it\n");
   fprintf(stdout, "is stored.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " IPTC (JP2 only)\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "If a compressed input contains `IPTC` metadata, this metadata will be stored to\n");
   fprintf(stdout, "the output file if that output file is in `TIF\\TIFF` format.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " XMP (JP2 only)\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "If a compressed input contains `XMP` metadata, this metadata will be stored to\n");
   fprintf(stdout, "the output file if that output file is in `TIF\\\\TIFF` or `PNG` format.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " Exif (JP2 only)\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "To transfer Exif and all other meta-data tags, use the command line argument\n");
   fprintf(stdout, "`-V` described below. To transfer the tags, Grok uses the\n");
   fprintf(stdout,
		   "[ExifTool](https://exiftool.org/) Perl module. ExifTool must be installed for\n");
   fprintf(stdout,
		   "this command line argument to work properly. Note: transferring Exif tags may\n");
   fprintf(stdout, "add a few hundred ms to the decompress time, depending on the system.\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "**Important note on command line argument notation below**: the outer square\n");
   fprintf(stdout, "braces appear for clarity only,and **should not** be included in the actual\n");
   fprintf(stdout, "command line argument. Square braces appearing inside the outer braces\n");
   fprintf(stdout, "**should** be included.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "   `-h,  -help`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Print a help message and exit.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "   `-version`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Print library version and exit.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "  `-v, -verbose`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Output information and warnings about decoding to console (errors are always\n");
   fprintf(stdout, "output). Console is silent by default.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "  `-i, -in_file [file]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Input file. Either this argument or the `-batch_src` argument described below is\n");
   fprintf(stdout,
		   "required. Valid input image extensions are J2K, JP2 and JPC. When using this\n");
   fprintf(stdout, "option output file must be specified using -o.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "  `-o, -out_file [file]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Output file. Required when using `-i` option. See above for supported file\n");
   fprintf(stdout,
		   "types. If a `PGX` filename is given, there will be as many output files as there\n");
   fprintf(stdout,
		   "are components: an index starting from 0 will then be appended to the output\n");
   fprintf(stdout, "filename, just before the `pgx` extension. If a `PGM` filename is given and\n");
   fprintf(stdout,
		   "there is more than one component, then only the first component will be written\n");
   fprintf(stdout, "to the file.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-y, -batch_src [directory path]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Path to the folder where the compressed images are stored. Either this argument\n");
   fprintf(stdout,
		   "or the `-i` argument described above is required. When image files are in the\n");
   fprintf(stdout,
		   "same directory as the executable, this can be indicated by a dot `.` argument.\n");
   fprintf(stdout,
		   "When using this option, the output format must be specified using `-out_fmt`.\n");
   fprintf(stdout, "Output images are saved in the same folder.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-a, -out_dir [output directory]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Output directory where compressed files are stored. Only relevant when the\n");
   fprintf(stdout,
		   "`-img_dir` flag is set. Default: same directory as specified by `-batch_src`.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-O, -out_fmt [format]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Output format used to decompress the code streams. Required when `-batch_src`\n");
   fprintf(stdout, "option is used. See above for supported formats.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-r, -reduce [reduce factor]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Reduce factor. Set the number of highest resolution levels to be discarded. The\n");
   fprintf(stdout, "image resolution is effectively divided by 2 to the power of the number of\n");
   fprintf(stdout,
		   "discarded levels. The reduce factor is limited by the smallest total number of\n");
   fprintf(stdout, "decomposition levels among tiles.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-l, -layer [layer number]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Layer number. Set the maximum number of quality layers to decode. If there are\n");
   fprintf(stdout, "fewer quality layers than the specified number, all quality layers will be\n");
   fprintf(stdout, "decoded.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-d, -region [x0,y0,x1,y1]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Decompress a region of the image. If `(X,Y)` is a location in the image, then it\n");
   fprintf(stdout, "will only be decoded\n");
   fprintf(stdout,
		   "if `x0 <= X < x1` and `y0 <= Y < y1`. By default, the entire image is decoded.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "There are two ways of specifying the decompress region:\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "1. pixel coordinates relative to image origin - region is specified in 32 bit\n");
   fprintf(stdout, "integers.\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Example: if image coordinates on canvas are `(50,50,1050,1050)` and region is\n");
   fprintf(stdout, "specified as `-d 100,100,200,200`,\n");
   fprintf(stdout, "then a region with canvas coordinates `(150,150,250,250)` is decompressed\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "2. pixel coordinates relative to image origin and scaled as floating point to\n");
   fprintf(stdout, "unit square `[0,0,1,1]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "The above example would be specified as `-d 0.1,0.1,0.2,0.2`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Note: there is one ambiguous case, namely `-d 0,0,1,1`, which could be\n");
   fprintf(stdout, "interpreted as either scaled or un-scaled.\n");
   fprintf(stdout, "We treat this case as a **scaled** pixel region.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-c, -compression [compression value]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Compress output image data. Currently, this flag is only applicable when output\n");
   fprintf(stdout, "format is set\n");
   fprintf(stdout, "to `TIF`. Possible values are {`NONE`, `LZW`,`JPEG`, `PACKBITS`.\n");
   fprintf(stdout, "`ZIP`,`LZMA`,`ZSTD`,`WEBP`}.\n");
   fprintf(stdout, "Default value is `NONE`.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-L, -compression_level [compression level]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "\"Quality\" of compression. Currently only implemented for `PNG` format.\n");
   fprintf(stdout, "For `PNG`, compression level ranges from 0 (no compression) up to 9.\n");
   fprintf(stdout, "Grok default value is 3.\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Note: PNG is always lossless, so using a different level will not affect the\n");
   fprintf(stdout, "image quality. It only changes\n");
   fprintf(stdout, "the speed vs file size tradeoff.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-t, -tile_index [tile index]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Only decode tile with specified index. Index follows the JPEG2000 convention\n");
   fprintf(stdout, "from top-left to bottom-right. By default all tiles are decoded.\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   " `-p, -precision [component 0 precision[C|S],component 1 precision[C|S],...]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Force precision (bit depth) of components. There must be at least one value\n");
   fprintf(stdout, "present, but there is no limit on the number of values.\n");
   fprintf(stdout,
		   "The last values are ignored if too many values. If there are fewer values than\n");
   fprintf(stdout, "components, the last value is used for the remaining components. If `C` is\n");
   fprintf(stdout,
		   "specified (default), values are clipped. If `S` is specified, values are scaled.\n");
   fprintf(stdout, "Specifying a `0` value indicates use of the original bit depth.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Example:\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "-p 8C,8C,8c\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Clip all components of a 16 bit RGB image to 8 bits.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-f, -force_rgb`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Force output image color space to `RGB`. For `TIF/TIFF` or `PNG` output formats,\n");
   fprintf(stdout,
		   "the ICC profile will be applied in this case - default behaviour is to stored\n");
   fprintf(stdout, "the profile in the output file, if supported.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-u, -upsample`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Sub-sampled components will be upsampled to image size.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-s, -split_pnm`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Split output components into different files when writing to `PNM`.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-X, -xml [output file name]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Store XML metadata to file, if it exists in compressed file. File name will be\n");
   fprintf(stdout, "set to `output file name + \".xml\"`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-V, -transfer_exif_tags`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Transfer all Exif tags to output file. Note: [ExifTool](https://exiftool.org/)\n");
   fprintf(stdout, "must be installed for this command line\n");
   fprintf(stdout, "argument to work correctly.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-W, -logfile [output file name]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Log to file. File name will be set to `output file name`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-H, -num_threads [number of threads]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Number of threads used for T1 compression. Default is total number of logical\n");
   fprintf(stdout, "cores.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "  `-e, -repetitions [number of repetitions]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Number of repetitions, for either a single image, or a folder of images. Default\n");
   fprintf(stdout, "is 1. 0 signifies unlimited repetitions.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-g, -plugin_path [plugin path]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Path to Grok plugin, which handles T1 decompression.\n");
   fprintf(stdout, "Default search path for plugin is in same folder as `grk_decompress` binary\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-G, -device_id [device ID]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "For Grok plugin running on multi-GPU system. Specifies which single GPU\n");
   fprintf(stdout, "accelerator to run codec on.\n");
   fprintf(stdout,
		   "If the flag is set to -1, all GPUs are used in round-robin scheduling. If set to\n");
   fprintf(stdout, "-2, then plugin is disabled and\n");
   fprintf(stdout, "compression is done on the CPU. Default value: 0.\n");
}

void GrkDecompress::printTiming(uint32_t num_images, std::chrono::duration<double> elapsed)
{
   if(!num_images)
	  return;
   std::string temp = (num_images > 1) ? "ms/image" : "ms";
   spdlog::info("decompress time: {} {}", (elapsed.count() * 1000) / (double)num_images, temp);
}

bool GrkDecompress::parsePrecision(const char* option, grk_decompress_parameters* parameters)
{
   const char* remaining = option;
   bool result = true;

   /* reset */
   if(parameters->precision)
   {
	  free(parameters->precision);
	  parameters->precision = nullptr;
   }
   parameters->num_precision = 0U;

   for(;;)
   {
	  int prec;
	  char mode;
	  char comma;
	  int count;

	  count = sscanf(remaining, "%d%c%c", &prec, &mode, &comma);
	  if(count == 1)
	  {
		 mode = 'C';
		 count++;
	  }
	  if((count == 2) || (mode == ','))
	  {
		 if(mode == ',')
			mode = 'C';
		 comma = ',';
		 count = 3;
	  }
	  if(count == 3)
	  {
		 if((prec < 1) || (prec > 32))
		 {
			spdlog::error("Invalid precision {} in precision option {}", prec, option);
			result = false;
			break;
		 }
		 if((mode != 'C') && (mode != 'S'))
		 {
			spdlog::error("Invalid precision mode %c in precision option {}", mode, option);
			result = false;
			break;
		 }
		 if(comma != ',')
		 {
			spdlog::error("Invalid character %c in precision option {}", comma, option);
			result = false;
			break;
		 }

		 if(parameters->precision == nullptr)
		 {
			/* first one */
			parameters->precision = (grk_precision*)malloc(sizeof(grk_precision));
			if(parameters->precision == nullptr)
			{
			   spdlog::error("Could not allocate memory for precision option");
			   result = false;
			   break;
			}
		 }
		 else
		 {
			uint32_t new_size = parameters->num_precision + 1U;
			grk_precision* new_prec;

			if(new_size == 0U)
			{
			   spdlog::error("Could not allocate memory for precision option");
			   result = false;
			   break;
			}

			new_prec =
				(grk_precision*)realloc(parameters->precision, new_size * sizeof(grk_precision));
			if(new_prec == nullptr)
			{
			   spdlog::error("Could not allocate memory for precision option");
			   result = false;
			   break;
			}
			parameters->precision = new_prec;
		 }

		 parameters->precision[parameters->num_precision].prec = (uint8_t)prec;
		 switch(mode)
		 {
			case 'C':
			   parameters->precision[parameters->num_precision].mode = GRK_PREC_MODE_CLIP;
			   break;
			case 'S':
			   parameters->precision[parameters->num_precision].mode = GRK_PREC_MODE_SCALE;
			   break;
			default:
			   break;
		 }
		 parameters->num_precision++;

		 remaining = strchr(remaining, ',');
		 if(remaining == nullptr)
		 {
			break;
		 }
		 remaining += 1;
	  }
	  else
	  {
		 spdlog::error("Could not parse precision option {}", option);
		 result = false;
		 break;
	  }
   }

   return result;
}

char GrkDecompress::nextFile(const std::string& inputFile, grk_img_fol* inputFolder,
							 grk_img_fol* outFolder, grk_decompress_parameters* parameters)
{
   spdlog::info("File: \"{}\"", inputFile.c_str());
   std::string infilename = inputFolder->imgdirpath + std::string(pathSeparator()) + inputFile;
   if(!grk_decompress_detect_format(infilename.c_str(), &parameters->decod_format) ||
	  parameters->decod_format == GRK_CODEC_UNK)
	  return 1;
   if(grk::strcpy_s(parameters->infile, sizeof(parameters->infile), infilename.c_str()) != 0)
	  return 1;

   auto temp_ofname = inputFile;
   auto pos = inputFile.rfind(".");
   if(pos != std::string::npos)
	  temp_ofname = inputFile.substr(0, pos);
   if(inputFolder->set_out_format)
   {
	  std::string outfilename = outFolder->imgdirpath + std::string(pathSeparator()) + temp_ofname +
								"." + inputFolder->out_format;
	  if(grk::strcpy_s(parameters->outfile, sizeof(parameters->outfile), outfilename.c_str()) != 0)
		 return 1;
   }

   return 0;
}

class GrokDecompressOutput : public TCLAP::StdOutput
{
 public:
   virtual void usage([[maybe_unused]] TCLAP::CmdLineInterface& c)
   {
	  decompress_help_display();
   }
};

/**
 * Convert compression string to compression code. (use TIFF codes)
 */
uint32_t GrkDecompress::getCompressionCode(const std::string& compressionString)
{
   if(compressionString == "NONE")
	  return 0;
   else if(compressionString == "LZW")
	  return 5;
   else if(compressionString == "JPEG")
	  return 7;
   else if(compressionString == "PACKBITS")
	  return 32773;
   else if(compressionString == "ZIP")
	  return 8;
   else if(compressionString == "LZMA")
	  return 34925;
   else if(compressionString == "ZSTD")
	  return 50000;
   else if(compressionString == "WEBP")
	  return 50001;
   else
	  return UINT_MAX;
}

GrkRC GrkDecompress::parseCommandLine(int argc, char** argv, DecompressInitParams* initParams)
{
   grk_decompress_parameters* parameters = &initParams->parameters;
   grk_img_fol* inputFolder = &initParams->inputFolder;
   grk_img_fol* outFolder = &initParams->outFolder;
   char* pluginPath = initParams->pluginPath;
   try
   {
	  TCLAP::CmdLine cmd("grk_decompress command line", ' ', grk_version());
	  cmd.setExceptionHandling(false);

	  // set the output
	  GrokDecompressOutput output;
	  cmd.setOutput(&output);

	  TCLAP::ValueArg<std::string> outDirArg("a", "out_dir", "Output Directory", false, "",
											 "string", cmd);
	  TCLAP::ValueArg<std::string> compressionArg("c", "compression", "compression Type", false, "",
												  "string", cmd);
	  TCLAP::ValueArg<std::string> decodeRegionArg("d", "region", "Decompress Region", false, "",
												   "string", cmd);
	  TCLAP::ValueArg<uint32_t> repetitionsArg(
		  "e", "repetitions",
		  "Number of compress repetitions, for either a folder or a single file", false, 0,
		  "unsigned integer", cmd);
	  TCLAP::SwitchArg forceRgbArg("f", "force_rgb", "Force RGB", cmd);
	  TCLAP::ValueArg<std::string> pluginPathArg("g", "plugin_path", "Plugin path", false, "",
												 "string", cmd);
	  TCLAP::ValueArg<int32_t> deviceIdArg("G", "device_id", "Device ID", false, 0, "integer", cmd);
	  TCLAP::ValueArg<uint32_t> numThreadsArg("H", "num_threads", "Number of threads", false, 0,
											  "unsigned integer", cmd);
	  TCLAP::ValueArg<std::string> inputFileArg("i", "in_file", "Input file", false, "", "string",
												cmd);
	  TCLAP::ValueArg<std::string> licenseArg("j", "license", "License", false, "", "string", cmd);
	  TCLAP::ValueArg<std::string> serverArg("J", "server", "Server", false, "", "string", cmd);
	  // Kernel build flags:
	  // 1 indicates build binary, otherwise load binary
	  // 2 indicates generate binaries
	  TCLAP::ValueArg<uint32_t> kernelBuildOptionsArg("k", "kernel_build", "Kernel build options",
													  false, 0, "unsigned integer", cmd);
	  TCLAP::ValueArg<uint16_t> layerArg("l", "layer", "layer", false, 0, "unsigned integer", cmd);
	  TCLAP::ValueArg<uint32_t> compressionLevelArg("L", "compression_level", "compression Level",
													false, UINT_MAX, "unsigned integer", cmd);
	  TCLAP::ValueArg<uint32_t> randomAccessArg("m", "random_access",
												"Toggle support for random access"
												" into code stream",
												false, 0, "unsigned integer", cmd);
	  TCLAP::ValueArg<std::string> outputFileArg("o", "out_file", "Output file", false, "",
												 "string", cmd);
	  TCLAP::ValueArg<std::string> outForArg("O", "out_fmt", "Output Format", false, "", "string",
											 cmd);
	  TCLAP::ValueArg<std::string> precisionArg("p", "precision", "Force precision", false, "",
												"string", cmd);
	  TCLAP::ValueArg<uint32_t> reduceArg("r", "reduce", "reduce resolutions", false, 0,
										  "unsigned integer", cmd);
	  TCLAP::SwitchArg splitPnmArg("s", "split_pnm", "Split PNM", cmd);
	  TCLAP::ValueArg<uint32_t> tileArg("t", "tile_info", "Input tile index", false, 0,
										"unsigned integer", cmd);
	  TCLAP::SwitchArg upsampleArg("u", "upsample", "Upsample", cmd);
	  TCLAP::SwitchArg verboseArg("v", "verbose", "Verbose", cmd);
	  TCLAP::SwitchArg transferExifTagsArg("V", "transfer_exif_tags", "Transfer Exif tags", cmd);
	  TCLAP::ValueArg<std::string> logfileArg("W", "logfile", "Log file", false, "", "string", cmd);
	  TCLAP::SwitchArg xmlArg("X", "xml", "xml metadata", cmd);
	  TCLAP::ValueArg<std::string> inDirArg("y", "batch_src", "Image Directory", false, "",
											"string", cmd);
	  TCLAP::ValueArg<uint32_t> durationArg("z", "Duration", "Duration in seconds", false, 0,
											"unsigned integer", cmd);

	  cmd.parse(argc, argv);
	  if(verboseArg.isSet())
		 parameters->verbose_ = true;
	  else
		 spdlog::set_level(spdlog::level::level_enum::err);
	  grk_set_msg_handlers(parameters->verbose_ ? infoCallback : nullptr, nullptr,
						   parameters->verbose_ ? warningCallback : nullptr, nullptr, errorCallback,
						   nullptr);
	  bool useStdio = inputFileArg.isSet() && outForArg.isSet() && !outputFileArg.isSet();
	  // disable verbose mode so we don't write info or warnings to stdout
	  if(useStdio)
		 parameters->verbose_ = false;
	  if(!parameters->verbose_)
		 spdlog::set_level(spdlog::level::level_enum::err);

	  if(logfileArg.isSet())
	  {
		 auto file_logger = spdlog::basic_logger_mt("grk_decompress", logfileArg.getValue());
		 spdlog::set_default_logger(file_logger);
	  }

	  initParams->transfer_exif_tags = transferExifTagsArg.isSet();
#ifndef GROK_HAVE_EXIFTOOL
	  if(initParams->transfer_exif_tags)
	  {
		 spdlog::warn("Transfer of EXIF tags not supported. Transfer can be achieved by "
					  "directly calling");
		 spdlog::warn("exiftool after decompression as follows: ");
		 spdlog::warn("exiftool -TagsFromFile $SOURCE_FILE -all:all>all:all $DEST_FILE");
		 initParams->transfer_exif_tags = false;
	  }
#endif
	  parameters->io_xml = xmlArg.isSet();
	  parameters->force_rgb = forceRgbArg.isSet();
	  if(upsampleArg.isSet())
	  {
		 if(reduceArg.isSet())
			spdlog::warn("Cannot upsample when reduce argument set. Ignoring");
		 else
			parameters->upsample = true;
	  }
	  parameters->split_pnm = splitPnmArg.isSet();
	  if(compressionArg.isSet())
	  {
		 uint32_t comp = getCompressionCode(compressionArg.getValue());
		 if(comp == UINT_MAX)
			spdlog::warn("Unrecognized compression {}. Ignoring", compressionArg.getValue());
		 else
			parameters->compression = comp;
	  }
	  if(compressionLevelArg.isSet())
		 parameters->compression_level = compressionLevelArg.getValue();
	  // process
	  if(inputFileArg.isSet())
	  {
		 const char* infile = inputFileArg.getValue().c_str();
		 // for debugging purposes, set to false
		 bool checkFile = true;

		 if(checkFile)
		 {
			if(!grk_decompress_detect_format(infile, &parameters->decod_format))
			{
			   spdlog::error("Unable to open file {} for decoding.", infile);
			   return GrkRCParseArgsFailed;
			}
			switch(parameters->decod_format)
			{
			   case GRK_CODEC_J2K:
			   case GRK_CODEC_JP2:
				  break;
			   default:
				  spdlog::error("Unknown input file format: {} \n"
								"        Known file formats are *.j2k, *.jp2 or *.jpc",
								infile);
				  return GrkRCParseArgsFailed;
			}
		 }
		 else
		 {
			parameters->decod_format = GRK_CODEC_J2K;
		 }
		 if(grk::strcpy_s(parameters->infile, sizeof(parameters->infile), infile) != 0)
		 {
			spdlog::error("Path is too long");
			return GrkRCParseArgsFailed;
		 }
	  }
	  if(outForArg.isSet())
	  {
		 std::string outformat = std::string(".") + outForArg.getValue();
		 inputFolder->set_out_format = true;
		 parameters->cod_format = (GRK_SUPPORTED_FILE_FMT)grk_get_file_format(outformat.c_str());
		 switch(parameters->cod_format)
		 {
			case GRK_FMT_PGX:
			   inputFolder->out_format = "pgx";
			   break;
			case GRK_FMT_PXM:
			   inputFolder->out_format = "ppm";
			   break;
			case GRK_FMT_BMP:
			   inputFolder->out_format = "bmp";
			   break;
			case GRK_FMT_JPG:
			   inputFolder->out_format = "jpg";
			   break;
			case GRK_FMT_TIF:
			   inputFolder->out_format = "tif";
			   break;
			case GRK_FMT_RAW:
			   inputFolder->out_format = "raw";
			   break;
			case GRK_FMT_RAWL:
			   inputFolder->out_format = "rawl";
			   break;
			case GRK_FMT_PNG:
			   inputFolder->out_format = "png";
			   break;
			default:
			   spdlog::error("Unknown output format image {} [only *.png, *.pnm, *.pgm, "
							 "*.ppm, *.pgx, *.bmp, *.tif, *.jpg, *.jpeg, *.raw or *.rawl]",
							 outformat);
			   return GrkRCParseArgsFailed;
		 }
	  }
	  if(outputFileArg.isSet())
	  {
		 const char* outfile = outputFileArg.getValue().c_str();
		 parameters->cod_format = (GRK_SUPPORTED_FILE_FMT)grk_get_file_format(outfile);
		 switch(parameters->cod_format)
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
			   spdlog::error(
				   "Unknown output format image {} [only *.png, *.pnm, *.pgm, *.ppm, *.pgx, "
				   "*.bmp, *.tif, *.tiff, *jpg, *jpeg, *.raw or *rawl]",
				   outfile);
			   return GrkRCParseArgsFailed;
		 }
		 if(grk::strcpy_s(parameters->outfile, sizeof(parameters->outfile), outfile) != 0)
		 {
			spdlog::error("Path is too long");
			return GrkRCParseArgsFailed;
		 }
	  }
	  else
	  {
		 if(!inDirArg.isSet())
		 {
			bool fail = true;
			bool unsupportedStdout =
				outForArg.isSet() &&
				!grk::supportedStdioFormat((GRK_SUPPORTED_FILE_FMT)parameters->cod_format, false);
			if(unsupportedStdout)
			   spdlog::error("Output format does not support decompress to stdout");
			else if(!outForArg.isSet())
			   spdlog::error("Missing output file");
			else
			   fail = false;
			if(fail)
			   return GrkRCParseArgsFailed;
		 }
	  }
	  if(outDirArg.isSet())
	  {
		 if(!validateDirectory(outDirArg.getValue()))
			return GrkRCFail;
		 outFolder->imgdirpath = (char*)malloc(strlen(outDirArg.getValue().c_str()) + 1);
		 strcpy(outFolder->imgdirpath, outDirArg.getValue().c_str());
		 outFolder->set_imgdir = true;
	  }

	  if(inDirArg.isSet())
	  {
		 if(!validateDirectory(inDirArg.getValue()))
			return GrkRCFail;
		 inputFolder->imgdirpath = (char*)malloc(strlen(inDirArg.getValue().c_str()) + 1);
		 strcpy(inputFolder->imgdirpath, inDirArg.getValue().c_str());
		 inputFolder->set_imgdir = true;
	  }

	  if(reduceArg.isSet())
	  {
		 if(reduceArg.getValue() >= GRK_MAXRLVLS)
			spdlog::warn("Resolution level reduction %u must be strictly less than the "
						 "maximum number of resolutions %u. Ignoring",
						 reduceArg.getValue(), GRK_MAXRLVLS);
		 else
			parameters->core.reduce = (uint8_t)reduceArg.getValue();
	  }
	  if(layerArg.isSet())
		 parameters->core.layers_to_decompress_ = layerArg.getValue();
	  if(randomAccessArg.isSet())
		 parameters->core.random_access_flags_ = randomAccessArg.getValue();
	  parameters->single_tile_decompress = tileArg.isSet();
	  if(tileArg.isSet())
		 parameters->tile_index = (uint16_t)tileArg.getValue();
	  if(precisionArg.isSet() && !parsePrecision(precisionArg.getValue().c_str(), parameters))
		 return GrkRCParseArgsFailed;
	  if(numThreadsArg.isSet())
		 parameters->num_threads = numThreadsArg.getValue();
	  if(decodeRegionArg.isSet())
	  {
		 size_t size_optarg = (size_t)strlen(decodeRegionArg.getValue().c_str()) + 1U;
		 char* ROI_values = new char[size_optarg];
		 ROI_values[0] = '\0';
		 memcpy(ROI_values, decodeRegionArg.getValue().c_str(), size_optarg);
		 /*printf("ROI_values = %s [%u / %u]\n", ROI_values, strlen(ROI_values), size_optarg );
		  */
		 bool rc = parseWindowBounds(ROI_values, &parameters->dw_x0, &parameters->dw_y0,
									 &parameters->dw_x1, &parameters->dw_y1);
		 delete[] ROI_values;
		 if(!rc)
			return GrkRCParseArgsFailed;
	  }

	  if(pluginPathArg.isSet() && pluginPath)
		 strcpy(pluginPath, pluginPathArg.getValue().c_str());
	  if(repetitionsArg.isSet())
		 parameters->repeats = repetitionsArg.getValue();
	  if(kernelBuildOptionsArg.isSet())
		 parameters->kernel_build_options = kernelBuildOptionsArg.getValue();
	  if(deviceIdArg.isSet())
		 parameters->device_id = deviceIdArg.getValue();
	  if(durationArg.isSet())
		 parameters->duration = durationArg.getValue();
   }
   catch(const TCLAP::ArgException& e) // catch any exceptions
   {
	  std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
	  return GrkRCParseArgsFailed;
   }
   catch(const TCLAP::ExitException& e) // catch any exceptions
   {
	  return GrkRCUsage;
   }

   /* check for possible errors */
   if(inputFolder->set_imgdir)
   {
	  if(!(parameters->infile[0] == 0))
	  {
		 spdlog::error("options -batch_src and -i cannot be used together.");
		 return GrkRCParseArgsFailed;
	  }
	  if(!inputFolder->set_out_format)
	  {
		 spdlog::error("When -batch_src is used, -out_fmt <FORMAT> must be used.");
		 spdlog::error("Only one format allowed.\n"
					   "Valid format are PGM, PPM, PNM, PGX, BMP, TIF and RAW.");
		 return GrkRCParseArgsFailed;
	  }
	  if(!((parameters->outfile[0] == 0)))
	  {
		 spdlog::error("options -batch_src and -o cannot be used together.");
		 return GrkRCParseArgsFailed;
	  }
   }
   else
   {
	  if(parameters->decod_format == GRK_CODEC_UNK)
	  {
		 if((parameters->infile[0] == 0) || (parameters->outfile[0] == 0))
		 {
			spdlog::error("Required parameters are missing\n"
						  "Example: {} -i image.j2k -o image.pgm",
						  argv[0]);
			spdlog::error("   Help: {} -h", argv[0]);
			return GrkRCParseArgsFailed;
		 }
	  }
   }
   return GrkRCSuccess;
}
void GrkDecompress::setDefaultParams(grk_decompress_parameters* parameters)
{
   if(parameters)
   {
	  grk_decompress_set_default_params(parameters);
	  parameters->device_id = 0;
	  parameters->repeats = 1;
	  parameters->compression_level = GRK_DECOMPRESS_COMPRESSION_LEVEL_DEFAULT;
   }
}

void GrkDecompress::destoryParams(grk_decompress_parameters* parameters)
{
   if(parameters)
   {
	  free(parameters->precision);
	  parameters->precision = nullptr;
   }
}

static int decompress_callback(grk_plugin_decompress_callback_info* info);

// returns 0 for failure, 1 for success, and 2 if file is not suitable for decoding
int GrkDecompress::decompress(const std::string& fileName, DecompressInitParams* initParams)
{
   if(initParams->inputFolder.set_imgdir)
   {
	  if(nextFile(fileName, &initParams->inputFolder,
				  initParams->outFolder.set_imgdir ? &initParams->outFolder
												   : &initParams->inputFolder,
				  &initParams->parameters))
	  {
		 return 2;
	  }
   }
   grk_plugin_decompress_callback_info info;
   memset(&info, 0, sizeof(grk_plugin_decompress_callback_info));
   info.decod_format = GRK_CODEC_UNK;
   info.decompress_flags = GRK_DECODE_ALL;
   info.decompressor_parameters = &initParams->parameters;
   info.user_data = this;
   info.cod_format =
	   info.cod_format != GRK_FMT_UNK ? info.cod_format : info.decompressor_parameters->cod_format;
   info.header_info.decompress_fmt = info.cod_format;
   info.header_info.force_rgb = info.decompressor_parameters->force_rgb;
   info.header_info.upsample = info.decompressor_parameters->upsample;
   info.header_info.precision = info.decompressor_parameters->precision;
   info.header_info.num_precision = info.decompressor_parameters->num_precision;
   info.header_info.split_by_component = info.decompressor_parameters->split_pnm;
   info.header_info.single_tile_decompress = info.decompressor_parameters->single_tile_decompress;
   if(preProcess(&info))
   {
	  grk_object_unref(info.codec);
	  return 0;
   }
   if(postProcess(&info))
   {
	  grk_object_unref(info.codec);
	  return 0;
   }
#ifdef GROK_HAVE_EXIFTOOL
   if(initParams->transfer_exif_tags && initParams->parameters.decod_format == GRK_CODEC_JP2)
	  transfer_exif_tags(initParams->parameters.infile, initParams->parameters.outfile);
#endif
   grk_object_unref(info.codec);
   info.codec = nullptr;
   return 1;
}

GrkRC GrkDecompress::pluginMain(int argc, char** argv, DecompressInitParams* initParams)
{
   grk_dircnt* dirptr = nullptr;
   GrkRC rc = GrkRCFail;
   uint32_t numDecompressed = 0;
   bool isBatch = false;
   std::chrono::time_point<std::chrono::high_resolution_clock> start;
   setDefaultParams(&initParams->parameters);
   GrkRC parseReturn = parseCommandLine(argc, argv, initParams);
   if(parseReturn != GrkRCSuccess)
	  return parseReturn;
#ifdef GROK_HAVE_LIBTIFF
   tiffSetErrorAndWarningHandlers(initParams->parameters.verbose_);
#endif
#ifdef GROK_HAVE_LIBPNG
   pngSetVerboseFlag(initParams->parameters.verbose_);
#endif
   initParams->initialized = true;
   // loads plugin but does not actually create codec
   grk_initialize(initParams->pluginPath, initParams->parameters.num_threads,
				  initParams->parameters.verbose_);

   // create codec
   grk_plugin_init_info initInfo;
   initInfo.device_id = initParams->parameters.device_id;
   initInfo.verbose = initParams->parameters.verbose_;
   if(!grk_plugin_init(initInfo))
	  goto cleanup;
   initParams->parameters.user_data = this;
   isBatch = initParams->inputFolder.imgdirpath && initParams->outFolder.imgdirpath;
   if((grk_plugin_get_debug_state() & GRK_PLUGIN_STATE_DEBUG))
	  isBatch = false;
   if(isBatch)
   {
	  // initialize batch
	  setUpSignalHandler();
	  int ret = grk_plugin_init_batch_decompress(initParams->inputFolder.imgdirpath,
												 initParams->outFolder.imgdirpath,
												 &initParams->parameters, decompress_callback);
	  // start batch
	  if(ret)
		 ret = grk_plugin_batch_decompress();
	  // if plugin successfully begins batch compress, then wait for batch to complete
	  if(ret == 0)
	  {
		 grk_plugin_wait_for_batch_complete();
		 grk_plugin_stop_batch_decompress();
		 rc = GrkRCSuccess;
	  }
	  else
	  {
		 goto cleanup;
	  }
   }
   else
   {
	  start = std::chrono::high_resolution_clock::now();
	  if(!initParams->inputFolder.set_imgdir)
	  {
		 if(grk_plugin_decompress(&initParams->parameters, decompress_callback))
			goto cleanup;
	  }
	  else
	  {
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
			if(grk_plugin_decompress(&initParams->parameters, decompress_callback))
			   goto cleanup;
			numDecompressed++;
		 }
	  }
	  printTiming(numDecompressed, std::chrono::high_resolution_clock::now() - start);
   }
   rc = GrkRCSuccess;
cleanup:
   if(dirptr)
   {
	  free(dirptr->filename_buf);
	  free(dirptr->filename);
	  free(dirptr);
   }
   return rc;
}

int decompress_callback(grk_plugin_decompress_callback_info* info)
{
   int rc = -1;
   // GRK_DECODE_T1 flag specifies full decompress on CPU, so
   // we don't need to initialize the decompressor in this case
   if(info->decompress_flags & GRK_DECODE_T1)
   {
	  info->init_decompressors_func = nullptr;
   }
   if(info->decompress_flags & GRK_PLUGIN_DECODE_CLEAN)
   {
	  grk_object_unref(info->codec);
	  info->codec = nullptr;
	  if(info->image && !info->plugin_owns_image)
		 info->image = nullptr;
	  rc = 0;
   }
   auto decompressor = (GrkDecompress*)info->user_data;
   if(info->decompress_flags & (GRK_DECODE_HEADER | GRK_DECODE_T1 | GRK_DECODE_T2))
   {
	  rc = decompressor->preProcess(info);
	  if(rc)
		 return rc;
   }
   if(info->decompress_flags & GRK_DECODE_POST_T1)
	  rc = decompressor->postProcess(info);
   return rc;
}

static void cleanUpFile(const char* outfile)
{
   if(!outfile)
	  return;

   bool allocated = false;
   char* p = actual_path(outfile, &allocated);
   if(!p)
	  return;
   int ret = (remove)(p);
   if(ret)
	  spdlog::warn("Error code {} when removing file {}; actual file path {}", ret, outfile, p);
   if(allocated)
	  free(p);
}

static void grkSerializeRegisterClientCallback(grk_io_init io_init,
											   grk_io_callback reclaim_callback, void* io_user_data,
											   void* reclaim_user_data)
{
   if(!io_user_data || !reclaim_user_data)
	  return;
   auto imageFormat = (IImageFormat*)io_user_data;

   imageFormat->registerGrkReclaimCallback(io_init, reclaim_callback, reclaim_user_data);
}

static bool grkSerializeBufferCallback(uint32_t threadId, grk_io_buf buffer, void* user_data)
{
   if(!user_data)
	  return false;
   auto imageFormat = (IImageFormat*)user_data;

   return imageFormat->encodePixels(threadId, buffer);
}

bool GrkDecompress::encodeHeader(grk_plugin_decompress_callback_info* info)
{
   if(!storeToDisk)
	  return true;
   if(!encodeInit(info))
	  return false;
   if(!imageFormat->encodeHeader())
   {
	  spdlog::error("Encode header failed.");
	  return false;
   }

   return true;
}

bool GrkDecompress::encodeInit(grk_plugin_decompress_callback_info* info)
{
   if(!storeToDisk)
	  return true;
   auto parameters = info->decompressor_parameters;
   const char* outfile = info->decompressor_parameters->outfile[0]
							 ? info->decompressor_parameters->outfile
							 : info->output_file_name;
   auto cod_format = info->cod_format != GRK_FMT_UNK ? info->cod_format : parameters->cod_format;
   auto outfileStr = outfile ? std::string(outfile) : "";
   uint32_t compression_level = 0;
   if(cod_format == GRK_FMT_TIF)
	  compression_level = parameters->compression;
   else if(cod_format == GRK_FMT_JPG || cod_format == GRK_FMT_PNG)
	  compression_level = parameters->compression_level;
   if(!imageFormat->encodeInit(info->image, outfileStr, compression_level,
							   info->decompressor_parameters->num_threads
								   ? info->decompressor_parameters->num_threads
								   : std::thread::hardware_concurrency()))
   {
	  spdlog::error("Outfile {} not generated", outfileStr);
	  return false;
   }

   return true;
}

// return: 0 for success, non-zero for failure
int GrkDecompress::preProcess(grk_plugin_decompress_callback_info* info)
{
   if(!info)
	  return 1;
   bool failed = true;
   // bool useMemoryBuffer = false;
   auto parameters = info->decompressor_parameters;
   if(!parameters)
	  return 1;
   auto infile = info->input_file_name ? info->input_file_name : parameters->infile;
   const char* outfile = info->decompressor_parameters->outfile[0]
							 ? info->decompressor_parameters->outfile
							 : info->output_file_name;
   auto cod_format = info->cod_format != GRK_FMT_UNK ? info->cod_format : parameters->cod_format;
   switch(cod_format)
   {
	  case GRK_FMT_PXM:
		 imageFormat = new PNMFormat(parameters->split_pnm);
		 break;
	  case GRK_FMT_PGX:
		 imageFormat = new PGXFormat();
		 break;
	  case GRK_FMT_BMP:
		 imageFormat = new BMPFormat();
		 break;
	  case GRK_FMT_TIF:
#ifdef GROK_HAVE_LIBTIFF
		 imageFormat = new TIFFFormat();
#else
		 spdlog::error("libtiff is missing");
		 goto cleanup;
#endif
		 break;
	  case GRK_FMT_RAW:
		 imageFormat = new RAWFormat(true);
		 break;
	  case GRK_FMT_RAWL:
		 imageFormat = new RAWFormat(false);
		 break;
	  case GRK_FMT_JPG:
#ifdef GROK_HAVE_LIBJPEG
		 imageFormat = new JPEGFormat();
#else
		 spdlog::error("libjpeg is missing");
		 goto cleanup;
#endif
		 break;
	  case GRK_FMT_PNG:
#ifdef GROK_HAVE_LIBPNG
		 imageFormat = new PNGFormat();
#else
		 spdlog::error("libpng is missing");
		 goto cleanup;
#endif
		 break;
	  default:
		 spdlog::error("Unsupported output format {}", convertFileFmtToString(info->cod_format));
		 goto cleanup;
		 break;
   }
   parameters->core.io_buffer_callback = grkSerializeBufferCallback;
   parameters->core.io_user_data = imageFormat;
   parameters->core.io_register_client_callback = grkSerializeRegisterClientCallback;

   // 1. initialize
   if(!info->codec)
   {
	  grk_stream_params stream_params;
	  memset(&stream_params, 0, sizeof(stream_params));
	  stream_params.file = infile;
	  info->codec = grk_decompress_init(&stream_params, &parameters->core);
	  if(!info->codec)
	  {
		 spdlog::error("grk_decompress: failed to set up the decompressor");
		 goto cleanup;
	  }
   }
   // 2. read header
   if(info->decompress_flags & GRK_DECODE_HEADER)
   {
	  // Read the main header of the code stream (j2k) and also JP2 boxes (jp2)
	  if(!grk_decompress_read_header(info->codec, &info->header_info))
	  {
		 spdlog::error("grk_decompress: failed to read the header");
		 goto cleanup;
	  }
	  info->image = grk_decompress_get_composited_image(info->codec);
	  auto img = info->image;

	  const float val[4] = {
		  info->decompressor_parameters->dw_x0, info->decompressor_parameters->dw_y0,
		  info->decompressor_parameters->dw_x1, info->decompressor_parameters->dw_y1};
	  bool allLessThanOne = true;
	  for(uint8_t i = 0; i < 4; ++i)
	  {
		 if(val[i] > 1.0f)
			allLessThanOne = false;
	  }
	  if(allLessThanOne)
	  {
		 info->decompressor_parameters->dw_x0 = (float)floor(val[0] * double(img->x1 - img->x0));
		 info->decompressor_parameters->dw_y0 = (float)floor(val[1] * double(img->y1 - img->y0));
		 info->decompressor_parameters->dw_x1 = (float)ceil(val[2] * double(img->x1 - img->x0));
		 info->decompressor_parameters->dw_y1 = (float)ceil(val[3] * double(img->y1 - img->y0));
	  }

	  // do not allow odd top left region coordinates for SYCC
	  if(info->image->color_space == GRK_CLRSPC_SYCC)
	  {
		 bool adjustX = (info->decompressor_parameters->dw_x0 != (float)info->full_image_x0) &&
						((uint32_t)info->decompressor_parameters->dw_x0 & 1);
		 bool adjustY = (info->decompressor_parameters->dw_y0 != (float)info->full_image_y0) &&
						((uint32_t)info->decompressor_parameters->dw_y0 & 1);
		 if(adjustX || adjustY)
		 {
			spdlog::error("grk_decompress: Top left-hand region coordinates that do not coincide\n"
						  "with respective top left-hand image coordinates must be even");
			goto cleanup;
		 }
	  }

	  // store xml to file
	  if(info->header_info.xml_data && info->header_info.xml_data_len && parameters->io_xml)
	  {
		 std::string xmlFile = std::string(parameters->outfile) + ".xml";
		 auto fp = fopen(xmlFile.c_str(), "wb");
		 if(!fp)
		 {
			spdlog::error("grk_decompress: unable to open file {} for writing xml to",
						  xmlFile.c_str());
			goto cleanup;
		 }
		 if(fwrite(info->header_info.xml_data, 1, info->header_info.xml_data_len, fp) !=
			info->header_info.xml_data_len)
		 {
			spdlog::error("grk_decompress: unable to write all xml data to file {}",
						  xmlFile.c_str());
			fclose(fp);
			goto cleanup;
		 }
		 if(!grk::safe_fclose(fp))
		 {
			spdlog::error("grk_decompress: error closing file {}", infile);
			goto cleanup;
		 }
	  }
	  if(info->init_decompressors_func)
		 return info->init_decompressors_func(&info->header_info, info->image);
   }
   if(info->image)
   {
	  info->full_image_x0 = info->image->x0;
	  info->full_image_y0 = info->image->y0;
   }
   // header-only decompress
   if(info->decompress_flags == GRK_DECODE_HEADER)
	  goto cleanup;
   // 3. decompress
   if(info->tile)
	  info->tile->decompress_flags = info->decompress_flags;
   // limit to 16 bit precision
   for(uint32_t i = 0; i < info->image->numcomps; ++i)
   {
	  if(info->image->comps[i].prec > GRK_MAX_SUPPORTED_IMAGE_PRECISION)
	  {
		 spdlog::error("grk_decompress: precision = {} not supported:", info->image->comps[i].prec);
		 goto cleanup;
	  }
   }
   if(!grk_decompress_set_window(info->codec, parameters->dw_x0, parameters->dw_y0,
								 parameters->dw_x1, parameters->dw_y1))
   {
	  spdlog::error("grk_decompress: failed to set the decompressed area");
	  goto cleanup;
   }
   if(!encodeInit(info))
	  return false;

   // decompress all tiles
   if(!parameters->single_tile_decompress)
   {
	  if(!(grk_decompress(info->codec, info->tile)))
		 goto cleanup;
   }
   // or, decompress one particular tile
   else
   {
	  if(!grk_decompress_tile(info->codec, parameters->tile_index))
	  {
		 spdlog::error("grk_decompress: failed to decompress tile");
		 goto cleanup;
	  }
   }
   if(!encodeHeader(info))
	  goto cleanup;
   failed = false;
cleanup:
   if(failed)
   {
	  cleanUpFile(outfile);
	  info->image = nullptr;
	  delete imageFormat;
	  imageFormat = nullptr;
   }

   return failed ? 1 : 0;
}

/*
 Post-process decompressed image and store in selected image format
 */
int GrkDecompress::postProcess(grk_plugin_decompress_callback_info* info)
{
   if(!info)
	  return -1;
   auto fmt = imageFormat;
   bool failed = true;
   bool imageNeedsDestroy = false;
   auto image = info->image;
   const char* infile = info->decompressor_parameters->infile[0]
							? info->decompressor_parameters->infile
							: info->input_file_name;
   const char* outfile = info->decompressor_parameters->outfile[0]
							 ? info->decompressor_parameters->outfile
							 : info->output_file_name;
   if(image->meta)
   {
	  if(image->meta->xmp_buf)
	  {
		 bool canStoreXMP = (info->decompressor_parameters->cod_format == GRK_FMT_TIF ||
							 info->decompressor_parameters->cod_format == GRK_FMT_PNG);
		 if(!canStoreXMP)
		 {
			spdlog::warn(" Input file `{}` contains XMP meta-data,\nbut the file format for "
						 "output file `{}` does not support storage of this data.",
						 infile, outfile);
		 }
	  }
	  if(image->meta->iptc_buf)
	  {
		 bool canStoreIPTC_IIM = (info->decompressor_parameters->cod_format == GRK_FMT_TIF);
		 if(!canStoreIPTC_IIM)
		 {
			spdlog::warn(
				" Input file `{}` contains legacy IPTC-IIM meta-data,\nbut the file format "
				"for output file `{}` does not support storage of this data.",
				infile, outfile);
		 }
	  }
   }
   if(storeToDisk)
   {
	  auto outfileStr = outfile ? std::string(outfile) : "";
	  if(!fmt->encodePixels())
	  {
		 spdlog::error("Outfile {} not generated", outfileStr);
		 goto cleanup;
	  }
	  if(!fmt->encodeFinish())
	  {
		 spdlog::error("Outfile {} not generated", outfileStr);
		 goto cleanup;
	  }
   }
   failed = false;
cleanup:
   grk_object_unref(info->codec);
   info->codec = nullptr;
   if(image && imageNeedsDestroy)
   {
	  grk_object_unref(&image->obj);
	  info->image = nullptr;
   }
   delete imageFormat;
   imageFormat = nullptr;
   if(failed)
	  cleanUpFile(outfile);

   return failed ? 1 : 0;
}
int GrkDecompress::main(int argc, char** argv)
{
   int rc = EXIT_SUCCESS;
   uint32_t numDecompressed = 0;
   DecompressInitParams initParams;
   try
   {
	  // try to decompress with plugin
	  GrkRC plugin_rc = pluginMain(argc, argv, &initParams);

	  // return immediately if either
	  // initParams was not initialized (something was wrong with command line params)
	  // or
	  // plugin was successful
	  if(plugin_rc == GrkRCSuccess || plugin_rc == GrkRCUsage)
	  {
		 rc = EXIT_SUCCESS;
		 goto cleanup;
	  }
	  if(!initParams.initialized)
	  {
		 rc = EXIT_FAILURE;
		 goto cleanup;
	  }
	  auto start = std::chrono::high_resolution_clock::now();
	  for(uint32_t i = 0; i < initParams.parameters.repeats; ++i)
	  {
		 std::string filename;
		 if(!initParams.inputFolder.set_imgdir)
		 {
			if(decompress(filename, &initParams) == 1)
			{
			   numDecompressed++;
			}
			else
			{
			   rc = EXIT_FAILURE;
			   goto cleanup;
			}
		 }
		 else
		 {
			for(const auto& entry :
				std::filesystem::directory_iterator(initParams.inputFolder.imgdirpath))
			{
			   if(entry.is_regular_file() &&
				  decompress(entry.path().filename().string(), &initParams) == 1)
				  numDecompressed++;
			}
		 }
	  }
	  printTiming(numDecompressed, std::chrono::high_resolution_clock::now() - start);
   }
   catch([[maybe_unused]] const std::bad_alloc& ba)
   {
	  spdlog::error("Out of memory. Exiting.");
	  rc = 1;
	  goto cleanup;
   }
cleanup:
   destoryParams(&initParams.parameters);
   grk_deinitialize();
   return rc;
}
GrkDecompress::GrkDecompress() : storeToDisk(true), imageFormat(nullptr) {}
GrkDecompress::~GrkDecompress(void)
{
   delete imageFormat;
   imageFormat = nullptr;
}

} // namespace grk
