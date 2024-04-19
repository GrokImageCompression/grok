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
using namespace grk;
#include "grk_apps_config.h"
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

namespace grk
{

static uint64_t pluginCompressCallback(grk_plugin_compress_user_callback_info* info);

grk_img_fol img_fol_plugin, out_fol_plugin;

static void compress_help_display(void)
{
   fprintf(stdout,
		   "grk_compress compresses various image formats into the JPEG 2000 format.\n"
		   "It has been compiled against libgrokj2k v%s.\n\n",
		   grk_version());
   fprintf(stdout,
		   "* Supported input formats:  `JPEG`, `BMP`, `PNM`, `PGX`, `PNG`, `RAW`, `RAWL`\n");
   fprintf(stdout, "and `TIFF`\n");
   fprintf(stdout,
		   "* Supported input image extensions:  `jpg`, `.jpeg`, `.bmp`, `.pgm`, `.pgx`,\n");
   fprintf(stdout, "`.pnm`, `.ppm`, `.pam`, `.png`, `.raw`, `.rawl`, `.tif` and `.tiff`\n");
   fprintf(stdout, "* Supported output formats: `JP2` and `J2K`/`J2C`\n");
   fprintf(stdout, "* Supported output image extensions: `.jp2` and `.j2k`/`.j2c`\n");
   fprintf(stdout, "* For `PNG` the library must have `libpng` available.\n");
   fprintf(stdout, "* For `TIF/TIFF` the library must have `libtiff` available.\n");
   fprintf(stdout, "* For `JPG/JPEG` the library must have a `libjpeg` variant available.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " limitations\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "* `grk_compress` supports up to and including 16 bit sample precision for input\n");
   fprintf(stdout, "images. This is a subset of the ISO standard, which allows up to 38 bit\n");
   fprintf(stdout, "precision.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " stdin\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Input from `stdin` is supported for the following formats: `PNG`, `JPG`, `RAW`\n");
   fprintf(stdout, "and `RAWL`.  To read from `stdin`,\n");
   fprintf(stdout,
		   "make sure that the `-i` parameter is **not** present, and that the `-in_fmt`\n");
   fprintf(stdout, "parameter is set to one of the supported formats listed above.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " Embedded ICC Profile (JP2 Only)\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "If there is an embedded ICC profile in the input file, then the profile will be\n");
   fprintf(stdout, "stored in the compressed file.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " IPTC (JP2 Only)\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "If an input `TIF/TIFF` file contains `IPTC` metadata, this metadata will be\n");
   fprintf(stdout, "stored in the compressed file.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " XMP (JP2 Only)\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "If an input `TIF/TIFF` or `PNG` file contains `XMP` metadata, this metadata will\n");
   fprintf(stdout, "be stored in the compressed file.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " Exif (JP2 only)\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "To transfer Exif and all other meta-data tags, use the command line argument\n");
   fprintf(stdout, "`-V` described below. To transfer the tags, Grok uses the wonderful\n");
   fprintf(stdout,
		   "[ExifTool](https://exiftool.org/) Perl module. ExifTool must be installed for\n");
   fprintf(stdout, "this command line argument to work properly.\n");
   fprintf(stdout,
		   "Note: transferring Exif tags may add a few hundred ms to the decompress time,\n");
   fprintf(stdout, "depending on the system.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " When only the input and output files are specified, the following default\n");
   fprintf(stdout, "option values are used:\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "* lossless compression\n");
   fprintf(stdout, "* reversible DWT 5-3\n");
   fprintf(stdout, "* single quality layer\n");
   fprintf(stdout, "* single tile\n");
   fprintf(stdout, "* precinct size : 2^15 x 2^15 (i.e. only 1 precinct)\n");
   fprintf(stdout, "* code block dimensions : 64 x 64\n");
   fprintf(stdout, "* number of resolutions (i.e. DWT decomposition levels + 1) : 6\n");
   fprintf(stdout, "* no SOP markers\n");
   fprintf(stdout, "* no EPH markers\n");
   fprintf(stdout, "* default encode mode\n");
   fprintf(stdout, "* progression order : `LRCP`\n");
   fprintf(stdout, "* no ROI up-shifted\n");
   fprintf(stdout, "* no image origin offset\n");
   fprintf(stdout, "* no tile origin offset\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "**Important note on command line argument notation below**: the outer square\n");
   fprintf(stdout, "braces appear for clarity only,\n");
   fprintf(stdout, "and **should not** be included in the actual command line argument. Square\n");
   fprintf(stdout, "braces appearing\n");
   fprintf(stdout, "inside the outer braces **should** be included.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " Options\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-h, -help`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Print a help message and exit.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-version`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Print library version and exit.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-v, -verbose`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Output information and warnings about encoding to console (errors are always\n");
   fprintf(stdout, "output). Default is false i.e. console is silent by default.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-i, -in_file [file]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Input file. Either this argument or the `-batch_src` argument described below is\n");
   fprintf(stdout, "required.  See above for supported input formats.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "* `PNG` requires `libpng` while `TIF/TIFF` requires `libtiff`\n");
   fprintf(stdout,
		   "* `JPG` requires `libjpeg` (or `libjpeg-turbo`), and only 8 bit precision is\n");
   fprintf(stdout, "supported\n");
   fprintf(stdout,
		   "*  For `BMP` format, the coder accepts 24 bits color images and 8 bits (RLE or\n");
   fprintf(stdout, "no-RLE) images\n");
   fprintf(stdout, "*  `TIF` files can have up to 16 bits per component.\n");
   fprintf(stdout,
		   "*  For `RAW` or `RAWL` (`RAW` `L`ittle endian) images, the `-F` parameter must\n");
   fprintf(stdout, "be used (see below). In the case of raw images with a component depth value\n");
   fprintf(stdout,
		   "between 9 and 16 bits, each component's data must be stored on two bytes (`RAW`\n");
   fprintf(stdout,
		   "format assumes big endian-ness, `RAWL` assumes little endian-ness) When using\n");
   fprintf(stdout, "this option, the output file must be specified using `-o`.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-o, -out_file [file]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Output file. Required when using `-i` option. Valid output image extensions are\n");
   fprintf(stdout, "`J2K`, `JP2` and `J2C`.\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   " `-y, -batch_src [Source image directory OR comma separated list of compression "
		   "settings for shared memory interface]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Path to the folder where the images to be compressed are stored. Either this\n");
   fprintf(stdout,
		   "argument or the `-i` argument described above is required. When image files are\n");
   fprintf(stdout, "in the same directory as the executable, this can be indicated by a dot `.`\n");
   fprintf(stdout,
		   "argument. When using this option, output format must be specified using `-O`.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-a, -out_dir [output directory]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Output directory where compressed files are stored. Only relevant when the\n");
   fprintf(stdout, "`-batch_src` flag is set. Default: same directory as specified by `-y`.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-O, -out_fmt [J2K|J2C|JP2]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Output format used to compress the images read from the directory specified with\n");
   fprintf(
	   stdout,
	   "`-batch_src`. Required when `-batch_src` option is used. Supported formats are `J2K`,\n");
   fprintf(stdout, "`J2C`, and `JP2`.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-K, -in_fmt [pbm|pgm|ppm|pnm|pam|pgx|png|bmp|tif|raw|rawl|jpg]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Input format. Will override file tag.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-F, -raw [width,height,number of components,bit\n");
   fprintf(stdout, "depth,[s,u]@<dx1>x<dy1>:...:<dxn>x<dyn>]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Raw input image characteristics. Required only if RAW or RAWL (RAW little\n");
   fprintf(stdout, "endian) input file is provided. Note: If sub-sampling is omitted, `1x1` is\n");
   fprintf(stdout, "assumed for all components.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Example of a raw `512x512` unsigned image with `4:2:0` sub-sampling\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "-F 512,512,3,8,u@1x1:2x2:2x2\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-r, -compression_ratios [<compression ratio>,<compression ratio>,...]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Note: not supported for Part 15 (HTJ2K) compression\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Compression ratio values (double precision, greater than or equal to one). Each\n");
   fprintf(stdout,
		   "value is a factor of compression, thus 20 means 20 times compressed. Each value\n");
   fprintf(stdout,
		   "represents a quality layer. The order used to define the different levels of\n");
   fprintf(stdout,
		   "compression is important and must be from left to right in descending order. A\n");
   fprintf(stdout, "final lossless quality layer (including all remaining code passes) will be\n");
   fprintf(stdout, "signified by the value 1. Default: 1 single lossless quality layer.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-q, -quality [quality in dB,quality in dB,...]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Note: not supported for Part 15 (HTJ2K) compression\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Quality values (double precision, greater than or equal to zero). Each value is\n");
   fprintf(stdout,
		   "a PSNR measure, given in dB, representing a quality layer. The order used to\n");
   fprintf(stdout,
		   "define the different PSNR values is important and must be from left to right in\n");
   fprintf(stdout, "ascending order. A value of 0 signifies a final lossless quality layer\n");
   fprintf(stdout,
		   "(including all remaining code passes) Default: 1 single lossless quality layer.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-n, -num_resolutions [number of resolutions]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Number of resolutions. It corresponds to the `number of DWT decompositions +1`.\n");
   fprintf(stdout, "Default: 6.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-b, -code_block_dims [code block width,code block height]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Code-block size. The dimension must respect the constraint defined in the\n");
   fprintf(stdout,
		   "JPEG-2000 standard (no dimension smaller than 4 or greater than 1024, no code-\n");
   fprintf(stdout,
		   "block with more than 4096 coefficients). The maximum value authorized is 64x64.\n");
   fprintf(stdout, "Default: 64x64.\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   " `-c, -precinct_dims [  [prec width,prec height],[prec width,prec height],... ]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Precinct dimensions. Dimensions specified must be powers of 2. Multiple records\n");
   fprintf(stdout, "may be specified, in which case the first record refers to the highest\n");
   fprintf(stdout,
		   "resolution level and subsequent records refer to lower resolution levels. The\n");
   fprintf(stdout,
		   "last specified record's dimensions are progressively right-shifted (halved in\n");
   fprintf(stdout,
		   "size) for each remaining lower resolution level. Default: `2^15x2^15` at each\n");
   fprintf(stdout,
		   "resolution i.e. precincts are not used. Note: the inner square brackets must\n");
   fprintf(stdout, "actually be present.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Example for image with 6 resolutions :\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "`-c [256,256],[256,256],[256,256],[256,256],[256,256],[256,256]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-t, -tile_dims [tile width,tile height]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Tile size. Default: the dimension of the whole image, thus only one tile.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-L, -PLT`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Use PLT markers. Default: off\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-X, -TLM`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Use TLM markers. Default: off\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-I, -irreversible`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Irreversible compression (ICT + DWT 9-7). This option enables the Irreversible\n");
   fprintf(stdout,
		   "Color Transformation (ICT) in place of the Reversible Color Transformation (RCT)\n");
   fprintf(stdout, "and the irreversible DWT 9-7 in place of the 5-3 filter. Default: off.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-p, -progression_order` [progression order]\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Progression order. The five progression orders are : `LRCP`, `RLCP`, `RPCL`,\n");
   fprintf(stdout, "`PCRL` and `CPRL`. Default: `LRCP`.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-Z, -rsiz [rsiz]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Profile, main level, sub level and version. Note: this flag will be ignored if\n");
   fprintf(stdout, "cinema profile flags are used.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-N, -guard_bits [number of guard bits]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Number of guard bits to use in block coder. Must be between 0 and 7.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-w, -cinema2K [24|48]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "2K digital cinema profile. This option generates a codes stream compliant with\n");
   fprintf(stdout,
		   "the DCI specifications for 2K resolution content. The value given is the frame\n");
   fprintf(stdout,
		   "rate, which can be either 24 or 48 fps. The main specifications of the JPEG 2000\n");
   fprintf(stdout, "Profile-3 (2K Digital Cinema Profile) are:\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "* Image size = 2048 x 1080 (at least one of the dimensions should match 2048 x\n");
   fprintf(stdout, "1080)\n");
   fprintf(stdout, "* Single tile\n");
   fprintf(stdout, "* Wavelet transform levels = Maximum of 5\n");
   fprintf(stdout, "* Wavelet filter = 9-7 filter\n");
   fprintf(stdout, "* Codeblock size = 32 x 32\n");
   fprintf(stdout,
		   "* Precinct size = 128 x 128 (Lowest frequency sub-band), 256 x 256 (other sub-\n");
   fprintf(stdout, "bands)\n");
   fprintf(stdout,
		   "* Maximum Bit rate for entire frame = 1302083 bytes for 24 fps, 651041 bytes for\n");
   fprintf(stdout, "48fps\n");
   fprintf(stdout,
		   "* Maximum Bit rate for each color component= 1041666 bytes for 24 fps, 520833\n");
   fprintf(stdout, "bytes for 48fps\n");
   fprintf(stdout,
		   "* Tile parts = 3; Each tile part contains data necessary to decompress one 2K\n");
   fprintf(stdout, "color component\n");
   fprintf(stdout, "* 12 bits per component.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-x, -cinema4k`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "4K digital cinema profile. This option generates a code stream compliant with\n");
   fprintf(stdout,
		   "the DCI specifications for 4K resolution content. The value given is the frame\n");
   fprintf(stdout,
		   "rate, which can be either 24 or 48 fps. The main specifications of the JPEG 2000\n");
   fprintf(stdout, "Profile-4 (4K Digital Cinema Profile) are:\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "* Image size = 4096 x 2160 (at least one of the dimensions must match 4096 x\n");
   fprintf(stdout, "2160)\n");
   fprintf(stdout, "* Single tile * Wavelet transform levels = Maximum of 6 and minimum of 1\n");
   fprintf(stdout, "* Wavelet filter = 9-7 filter\n");
   fprintf(stdout, "* Codeblock size = 32 x 32\n");
   fprintf(stdout,
		   "* Precinct size = 128 x 128 (Lowest frequency sub-band), 256 x 256 (other sub-\n");
   fprintf(stdout, "bands)\n");
   fprintf(stdout, "* Maximum Bit rate for entire frame = 1302083 bytes for 24 fps\n");
   fprintf(stdout, "* Maximum Bit rate for each color component= 1041666 bytes for 24 fps\n");
   fprintf(stdout, "* Tile parts = 6; Each of first 3 tile parts contains data necessary to\n");
   fprintf(stdout,
		   "decompress one 2K color component, and each of last 3 tile parts contains data\n");
   fprintf(stdout, "necessary to decompress one 4K color component.\n");
   fprintf(stdout, "* 12 bits per component\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-U, -broadcast [PROFILE [,mainlevel=X][,framerate=FPS] ]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Broadcast compliant code stream\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "* `PROFILE` must be one of { `SINGLE`, `MULTI`, `MULTI_R`}\n");
   fprintf(stdout, "* X must be between 0 and 11\n");
   fprintf(stdout,
		   "* frame rate may be specified to enhance checks and set maximum bit rate when Y\n");
   fprintf(stdout, "> 0.\n");
   fprintf(stdout, "If specified, it must be positive.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-z, -IMF [PROFILE [,mainlevel=X][,sublevel=Y][,framerate=FPS]] ]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Interoperable Master Format (IMF) compliant codestream.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "* `PROFILE` must be one of { `2K`, `4K`, `8K`, `2K_R`, `4K_R`, `8K_R`}\n");
   fprintf(stdout, "* X must be between 0 and 11\n");
   fprintf(stdout, "* Y must be between 0 and 9\n");
   fprintf(stdout,
		   "* frame rate may be specified to enhance checks and set maximum bit rate when Y\n");
   fprintf(stdout, "> 0. If specified, it must be positive.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-P, -POC [T<tile number 0>=resolution number start>,component number\n");
   fprintf(stdout,
		   "start,layer number end,resolution number end,component number end,progression\n");
   fprintf(stdout, "order/T<tile number 1>= ...]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Progression order change. This specifies a list of progression orders and their\n");
   fprintf(stdout, "bounds if a progression order change is desired.\n");
   fprintf(stdout, "Note: there must be at least two progression orders specified.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Example:\n");
   fprintf(stdout, "` -POC T0=0,0,1,3,2,CPRL/T0=0,0,1,6,3,CPRL`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-S, -SOP`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "SOP marker is added before each packet. Default: no SOP.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-E, -EPH`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "EPH marker is added after each packet header. Default: no EPH.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-M, -mode [value]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Non-default encode modes. There are 7 modes available.\n");
   fprintf(stdout, "The first six are:\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "* BYPASS(LAZY) [1]\n");
   fprintf(stdout, "* RESET [2]\n");
   fprintf(stdout, "* RESTART(TERMALL) [4]\n");
   fprintf(stdout, "* VSC [8]\n");
   fprintf(stdout, "* ERTERM(SEGTERM) [16]\n");
   fprintf(stdout, "* SEGMARK(SEGSYM) [32]\n");
   fprintf(stdout, "* HT [64]\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "and they can be combined together. If more than one mode is used, the values\n");
   fprintf(stdout, "between the brackets `[]` must be added together. Default: no mode.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Example : RESTART(4) + RESET(2) + SEGMARK(32) => -M 38\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Mode HT [64], for High Throughput encoding, *cannot* be combined with any of the\n");
   fprintf(stdout, "other flags.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-u, -tile_parts [R|L|C]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Divide packets of every tile into tile-parts. The division is made by grouping\n");
   fprintf(stdout,
		   "Resolutions (R), Layers (L) or Components (C). The type of division is specified\n");
   fprintf(stdout, "by setting the single letter `R`, `L`, or `C` as the value for this flag.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-R, -ROI [c=component index,U=upshifting value]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Quantization indices upshifted for a component.\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Warning: This option does not implement the usual ROI (Region of Interest). It\n");
   fprintf(stdout,
		   "should be understood as a \"Component of Interest\". It offers the possibility\n");
   fprintf(stdout,
		   "to upshift the value of a component during quantization step. The value after\n");
   fprintf(stdout,
		   "`c=` is the component number `[0, 1, 2, ...]` and the value after `U=` is the\n");
   fprintf(stdout, "value of upshifting. U must be in the range `[0, 37]`.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-d, -image_offset [x offset,y offset]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Offset of the image origin. The division in tile could be modified as the anchor\n");
   fprintf(stdout,
		   "point for tiling will be different than the image origin. Keep in mind that the\n");
   fprintf(stdout,
		   "offset of the image can not be higher than the tile dimension if the tile option\n");
   fprintf(stdout,
		   "is used. The two values are respectively for `X` and `Y` axis offset. Default:\n");
   fprintf(stdout, "no offset.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-T, -tile_offset [x offset,y offset]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Offset of the tile origin. The two values are respectively for X and Y axis\n");
   fprintf(stdout, "offset. The tile anchor point can not be inside the image area. Default: no\n");
   fprintf(stdout, "offset.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-Y, -MCT [0|1|2]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Specify explicitly if a Multiple Component Transform has to be used.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "* 0: no MCT\n");
   fprintf(stdout, "* 1: RGB->YCC conversion\n");
   fprintf(stdout, "* 2: custom MCT.\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "For custom MCT, `-m` option has to be used (see below). By default, `RGB`->`YCC`\n");
   fprintf(stdout, "conversion is used if there are three components or more, otherwise no\n");
   fprintf(stdout, "conversion.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-m, -custom_mct [file]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Use custom array-based MCT of 32 bit signed values, comma separated, line-by-\n");
   fprintf(stdout,
		   "line no specific separators between lines, no space allowed between values. If\n");
   fprintf(stdout, "this option is used, it automatically sets `[-Y|-mct]` option equal to 2.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-V, -transfer_exif_tags`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Transfer all Exif tags to output file.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Notes:\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "1. [ExifTool](https://exiftool.org/) must be installed for this command line\n");
   fprintf(stdout, "argument\n");
   fprintf(stdout, "to function correctly.\n");
   fprintf(stdout,
		   "2. Only supported on Linux. On other platforms, `exiftool` can be used directly\n");
   fprintf(stdout, "after compression to transfer\n");
   fprintf(stdout, "tags:\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "`exiftool -TagsFromFile src.tif \"-all:all>all:all\" dest.jp2`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-Q, -capture_res [capture resolution X,capture resolution Y]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Capture resolution in pixels/metre, in double precision.\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "* If the input image has a resolution stored in its header, then this resolution\n");
   fprintf(stdout, "will be set as the capture resolution, by default.\n");
   fprintf(stdout, "* If the `-Q` command line parameter is set, then it will override the\n");
   fprintf(stdout, "resolution stored in the input image, if present\n");
   fprintf(stdout,
		   "* The special values `[0,0]` for `-Q` will force the encoder to **not** store\n");
   fprintf(stdout, "capture resolution, even if present in input image.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-D, -display_res [display resolution X,display resolution Y]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Display resolution in pixels/metre, in double precision.\n");
   fprintf(stdout,
		   "The special values `[0,0]` for `-D` will force the encoder to set the display\n");
   fprintf(stdout, "resolution equal to the capture resolution.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-C, -comment [comment]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Add `<comment>` in comment marker segment(s). Multiple comments (up to a total\n");
   fprintf(stdout,
		   "of 256) can be specified, separated by the `|` character. For example:   `-C\n");
   fprintf(stdout, "\"This is my first comment|This is my second` will store `This is my first\n");
   fprintf(stdout, "comment` in the first comment marker segment, and `This is my second` in a\n");
   fprintf(stdout, "second comment marker.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-f, -apply_icc`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Apply ICC profile before compression, if present.\n");
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
   fprintf(stdout, " `-J, -duration [duration]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Duration in seconds for a batch compress job. `grk_compress` will exit when\n");
   fprintf(stdout, "duration has been reached.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-e, -repetitions [number of repetitions]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout,
		   "Number of repetitions, for either a single image, or a folder of images. Default\n");
   fprintf(stdout, "value is `1`. Unlimited repetitions are specified by a value of `0`.\n");
   fprintf(stdout, "\n");
   fprintf(stdout, " `-g, -plugin_path [plugin path]`\n");
   fprintf(stdout, "\n");
   fprintf(stdout, "Path to Grok plugin, which handles T1 compression.\n");
   fprintf(stdout, "Default search path for plugin is in same folder as `grk_compress` binary\n");
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
CompressInitParams::CompressInitParams()
	: initialized(false), transferExifTags(false), in_image(nullptr), stream_(nullptr)
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
   std::string infilename = inputFolder->imgdirpath + std::string(grk::pathSeparator()) + inputFile;
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
   auto pos = inputFile.rfind(".");
   if(pos != std::string::npos)
	  outputRootFile = inputFile.substr(0, pos);
   else
	  outputRootFile = inputFile;
   if(inputFolder->set_out_format)
   {
	  std::string outfilename = outFolder->imgdirpath + std::string(grk::pathSeparator()) +
								outputRootFile + "." + inputFolder->out_format;
	  if(grk::strcpy_s(parameters->outfile, sizeof(parameters->outfile), outfilename.c_str()) != 0)
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

class GrokCompressOutput : public TCLAP::StdOutput
{
 public:
   virtual void usage([[maybe_unused]] TCLAP::CmdLineInterface& c)
   {
	  compress_help_display();
   }
};
static void parse_cs(const std::string& str, std::vector<std::string>& result)
{
   std::stringstream ss(str);
   while(ss.good())
   {
	  std::string substr;
	  std::getline(ss, substr, ',');
	  result.push_back(substr);
   }
}

static bool validateCinema(TCLAP::ValueArg<std::string>* arg, uint16_t profile,
						   grk_cparameters* parameters)
{
   if(arg->isSet())
   {
	  auto val = arg->getValue();
	  std::vector<std::string> args;
	  parse_cs(val, args);
	  uint16_t fps = (uint16_t)std::stoi(args[0]);
	  int bandwidth = 0;
	  if(args.size() > 1)
		 bandwidth = std::stoi(args[1]) / ((int)fps * 8);
	  parameters->rsiz = profile;
	  parameters->framerate = fps;
	  if(fps == 24)
	  {
		 if(bandwidth > 0)
		 {
			parameters->max_cs_size = (uint64_t)bandwidth;
			parameters->max_comp_size = uint64_t(double(bandwidth) / 1.25);
		 }
		 else
		 {
			parameters->max_comp_size = GRK_CINEMA_24_COMP;
			parameters->max_cs_size = GRK_CINEMA_24_CS;
		 }
	  }
	  else if(fps == 48)
	  {
		 if(bandwidth > 0)
		 {
			parameters->max_cs_size = (uint64_t)bandwidth;
			parameters->max_comp_size = uint64_t(double(bandwidth) / 1.25);
		 }
		 else
		 {
			parameters->max_comp_size = GRK_CINEMA_48_COMP;
			parameters->max_cs_size = GRK_CINEMA_48_CS;
		 }
	  }
	  else
	  {
		 bandwidth = GRK_CINEMA_DCI_MAX_BANDWIDTH;
		 if(args.size() > 1)
			bandwidth = std::stoi(args[1]);
		 bandwidth /= (int)fps * 8;
		 parameters->max_cs_size = (uint64_t)bandwidth;
		 parameters->max_comp_size = uint64_t(double(bandwidth) / 1.25);
	  }
	  parameters->numgbits = (profile == GRK_PROFILE_CINEMA_2K) ? 1 : 2;
   }
   return true;
}

int GrkCompress::main(int argc, char** argv, grk_image* in_image, grk_stream_params* stream)
{
   CompressInitParams initParams;
   initParams.in_image = in_image;
   initParams.stream_ = stream;
   int success = 0;
   try
   {
	  // try to compress with plugin
	  GrkRC plugin_rc = pluginMain(argc, argv, &initParams);

	  // return immediately if either
	  // initParams was not initialized (something was wrong with command line params)
	  // or
	  // plugin was successful
	  if(plugin_rc == GrkRCSuccess || plugin_rc == GrkRCUsage)
	  {
		 success = EXIT_SUCCESS;
		 goto cleanup;
	  }
	  if(!initParams.initialized)
	  {
		 success = EXIT_FAILURE;
		 goto cleanup;
	  }

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
			spdlog::info("Compressed file {}", initParams.parameters.outfile);
			numCompressedFiles++;
		 }
		 else
		 {
			for(const auto& entry :
				std::filesystem::directory_iterator(initParams.inputFolder.imgdirpath))
			{
			   initParams.parameters = parametersCache;
			   if(compress(entry.path().filename().string(), &initParams) == 1)
			   {
				  spdlog::info("Compressed file {}", initParams.parameters.outfile);
				  numCompressedFiles++;
			   }
			}
		 }
	  }
	  auto finish = std::chrono::high_resolution_clock::now();
	  std::chrono::duration<double> elapsed = finish - start;
	  if(numCompressedFiles)
	  {
		 spdlog::info("compress time: {} {}", (elapsed.count() * 1000) / (double)numCompressedFiles,
					  numCompressedFiles > 1 ? "ms/image" : "ms");
	  }
   }
   catch(const std::bad_alloc& ba)
   {
	  spdlog::error(" Out of memory. Exiting.");
	  success = 1;
	  goto cleanup;
   }
cleanup:
   grk_deinitialize();

   return success;
}

int GrkCompress::pluginBatchCompress(CompressInitParams* initParams)
{
   setUpSignalHandler();
   grk_plugin_compress_batch_info info;
   memset(&info, 0, sizeof(info));
   info.input_dir = initParams->inputFolder.imgdirpath;
   info.output_dir = initParams->outFolder.imgdirpath;
   info.compress_parameters = &initParams->parameters;
   info.callback = pluginCompressCallback;
   int success = grk_plugin_batch_compress(info);
   // if plugin successfully begins batch compress, then wait for batch to complete
   if(success == 0)
   {
	  grk_plugin_wait_for_batch_complete();
	  grk_plugin_stop_batch_compress();
   }

   return success;
}

GrkRC GrkCompress::pluginMain(int argc, char** argv, CompressInitParams* initParams)
{
   if(!initParams)
	  return GrkRCFail;
   /* set compressing parameters to default values */
   grk_compress_set_default_params(&initParams->parameters);
   /* parse input and get user compressing parameters */
   initParams->parameters.mct =
	   255; /* This will be set later according to the input image or the provided option */
   initParams->parameters.rateControlAlgorithm = GRK_RATE_CONTROL_PCRD_OPT;
   GrkRC parseRc = parseCommandLine(argc, argv, initParams);
   if(parseRc != GrkRCSuccess)
	  return parseRc;

#ifdef GROK_HAVE_LIBTIFF
   tiffSetErrorAndWarningHandlers(initParams->parameters.verbose);
#endif
   initParams->initialized = true;
   // load plugin but do not actually create codec
   grk_initialize(initParams->pluginPath, initParams->parameters.numThreads,
				  initParams->parameters.verbose);
   img_fol_plugin = initParams->inputFolder;
   out_fol_plugin = initParams->outFolder;

   // create codec
   grk_plugin_init_info initInfo;
   memset(&initInfo, 0, sizeof(initInfo));
   initInfo.deviceId = initParams->parameters.deviceId;
   initInfo.verbose = initParams->parameters.verbose;
   initInfo.license = initParams->license_.c_str();
   initInfo.server = initParams->server_.c_str();
   if(!grk_plugin_init(initInfo))
	  return GrkRCFail;

   // 1. batch encode
   uint32_t state = grk_plugin_get_debug_state();
   bool isBatch =
	   initParams->inputFolder.imgdirpath &&
	   (initParams->outFolder.imgdirpath || initParams->parameters.sharedMemoryInterface);
   if(isBatch && !((state & GRK_PLUGIN_STATE_DEBUG) || (state & GRK_PLUGIN_STATE_PRE_TR1)))
	  return pluginBatchCompress(initParams) ? GrkRCFail : GrkRCSuccess;

   // 2. single image encode
   if(!initParams->inputFolder.set_imgdir)
	  return grk_plugin_compress(&initParams->parameters, pluginCompressCallback) ? GrkRCSuccess
																				  : GrkRCFail;

   // 3. directory encode
   //  cache certain settings
   auto mct = initParams->parameters.mct;
   auto rateControlAlgorithm = initParams->parameters.rateControlAlgorithm;
   GrkRC rc = GrkRCFail;
   for(const auto& entry : std::filesystem::directory_iterator(initParams->inputFolder.imgdirpath))
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
	  if(grk_plugin_compress(&initParams->parameters, pluginCompressCallback))
		 break;
   }
   rc = GrkRCSuccess;

   return rc;
}

static void setHT(grk_cparameters* parameters,
		TCLAP::ValueArg<std::string> &compressionRatiosArg, TCLAP::ValueArg<std::string> &qualityArg) {
   parameters->cblk_sty = GRK_CBLKSTY_HT;
   parameters->numgbits = 1;
   if(compressionRatiosArg.isSet() || qualityArg.isSet())
   {
	  spdlog::warn("HTJ2K compression using rate distortion or quality"
				   " is not currently supported.");
   }
}

GrkRC GrkCompress::parseCommandLine(int argc, char** argv, CompressInitParams* initParams)
{
   grk_cparameters* parameters = &initParams->parameters;
   grk_img_fol* inputFolder = &initParams->inputFolder;
   grk_img_fol* outFolder = &initParams->outFolder;
   char* pluginPath = initParams->pluginPath;

   try
   {
	  TCLAP::CmdLine cmd("grk_compress command line", ' ', grk_version());

	  // set the output
	  GrokCompressOutput output;
	  cmd.setOutput(&output);
	  cmd.setExceptionHandling(false);

	  TCLAP::ValueArg<std::string> outDirArg("a", "out_dir", "Output directory", false, "",
											 "string", cmd);
	  TCLAP::ValueArg<std::string> codeBlockDimArg("b", "code_block_dims", "Code block dimensions",
												   false, "", "string", cmd);
	  TCLAP::ValueArg<std::string> precinctDimArg("c", "precinct_dims", "Precinct dimensions",
												  false, "", "string", cmd);
	  TCLAP::ValueArg<std::string> commentArg("C", "comment", "Add a comment", false, "", "string",
											  cmd);
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
	  TCLAP::SwitchArg applyICCArg("f", "apply_icc", "Apply ICC profile before compression", cmd);
	  TCLAP::ValueArg<std::string> rawFormatArg("F", "raw", "raw image format parameters", false,
												"", "string", cmd);
	  TCLAP::ValueArg<std::string> pluginPathArg("g", "plugin_path", "Plugin path", false, "",
												 "string", cmd);
	  TCLAP::ValueArg<int32_t> deviceIdArg("G", "device_id", "Device ID", false, 0, "integer", cmd);
	  TCLAP::ValueArg<uint32_t> numThreadsArg("H", "num_threads", "Number of threads", false, 0,
											  "unsigned integer", cmd);
	  TCLAP::ValueArg<std::string> inputFileArg("i", "in_file", "Input file", false, "", "string",
												cmd);
	  TCLAP::SwitchArg irreversibleArg("I", "irreversible", "Irreversible", cmd);
	  TCLAP::ValueArg<std::string> licenseArg("j", "license", "License", false, "", "string", cmd);
	  TCLAP::ValueArg<std::string> serverArg("J", "server", "Server", false, "", "string", cmd);
	  // Kernel build flags:
	  // 1 indicates build binary, otherwise load binary
	  // 2 indicates generate binaries
	  TCLAP::ValueArg<uint32_t> kernelBuildOptionsArg("k", "kernel_build", "Kernel build options",
													  false, 0, "unsigned integer", cmd);
	  TCLAP::ValueArg<std::string> inForArg("K", "in_fmt", "InputFormat format", false, "",
											"string", cmd);
	  TCLAP::ValueArg<uint32_t> durationArg("l", "duration", "Duration in seconds", false, 0,
											"unsigned integer", cmd);
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
	  TCLAP::ValueArg<std::string> tilesArg("t", "tile_dims", "Tile dimensions", false, "",
											"string", cmd);
	  TCLAP::ValueArg<std::string> tileOffsetArg("T", "tile_offset", "Tile offset", false, "",
												 "string", cmd);
	  TCLAP::ValueArg<uint8_t> tpArg("u", "tile_parts", "Tile part generation", false, 0, "uint8_t",
									 cmd);
	  TCLAP::ValueArg<std::string> BroadcastArg("U", "broadcast", "Broadcast profile", false, "",
												"string", cmd);
	  TCLAP::SwitchArg verboseArg("v", "verbose", "Verbose", cmd);
	  TCLAP::SwitchArg transferExifTagsArg("V", "transfer_exif_tags", "Transfer Exif tags", cmd);
	  TCLAP::ValueArg<std::string> cinema2KArg("w", "cinema2K", "Digital cinema 2K profile", false,
											   "24", "string", cmd);
	  TCLAP::ValueArg<std::string> logfileArg("W", "logfile", "Log file", false, "", "string", cmd);
	  TCLAP::ValueArg<std::string> cinema4KArg("x", "cinema4K", "Digital cinema 4K profile", false,
											   "24", "string", cmd);
	  TCLAP::SwitchArg tlmArg("X", "TLM", "TLM marker", cmd);
	  TCLAP::ValueArg<std::string> batchSrcArg("y", "batch_src",
											   "Source image directory OR comma separated list of "
											   "compression settings for shared memory interface",
											   false, "", "string", cmd);
	  TCLAP::ValueArg<uint32_t> mctArg("Y", "MCT", "Multi component transform", false, 0,
									   "unsigned integer", cmd);
	  TCLAP::ValueArg<std::string> IMFArg("z", "IMF", "IMF profile", false, "", "string", cmd);
	  TCLAP::ValueArg<uint16_t> rsizArg("Z", "rsiz", "rsiz", false, 0, "unsigned integer", cmd);
	  cmd.parse(argc, argv);
	  if(logfileArg.isSet())
	  {
		 auto file_logger = spdlog::basic_logger_mt("grk_compress", logfileArg.getValue());
		 spdlog::set_default_logger(file_logger);
	  }
	  if(verboseArg.isSet())
		 parameters->verbose = true;
	  else
		 spdlog::set_level(spdlog::level::level_enum::err);
	  grk_set_msg_handlers(parameters->verbose ? infoCallback : nullptr, nullptr,
						   parameters->verbose ? warningCallback : nullptr, nullptr, errorCallback,
						   nullptr);

	  bool isHT = false;
	  if(resolutionArg.isSet())
		 parameters->numresolution = (uint8_t)resolutionArg.getValue();
	  else if(cinema4KArg.isSet())
		 parameters->numresolution = GRK_CINEMA_4K_DEFAULT_NUM_RESOLUTIONS;

#ifndef GRK_BUILD_DCI
	  initParams->transferExifTags = transferExifTagsArg.isSet();
#ifndef GROK_HAVE_EXIFTOOL
	  if(initParams->transferExifTags)
	  {
		 spdlog::warn("Transfer of EXIF tags not supported. Transfer can be achieved by "
					  "directly calling");
		 spdlog::warn("exiftool after compression as follows: ");
		 spdlog::warn("exiftool -TagsFromFile $SOURCE_FILE -all:all>all:all $DEST_FILE");
		 initParams->transferExifTags = false;
	  }
#endif
	  inputFolder->set_out_format = false;
	  parameters->raw_cp.width = 0;
	  if(applyICCArg.isSet())
		 parameters->apply_icc_ = true;
	  if(pltArg.isSet())
		 parameters->writePLT = true;
	  if(tlmArg.isSet())
		 parameters->writeTLM = true;
	  if(repetitionsArg.isSet())
		 parameters->repeats = repetitionsArg.getValue();
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
			   return GrkRCFail;
			}
		 }
		 if(grk::strcpy_s(parameters->infile, sizeof(parameters->infile), infile) != 0)
			return GrkRCFail;
	  }
	  else
	  {
		 // check for possible input from STDIN
		 if(!batchSrcArg.isSet() && !initParams->in_image)
		 {
			bool fail = true;
			bool unsupportedStdout =
				inForArg.isSet() &&
				!grk::supportedStdioFormat((GRK_SUPPORTED_FILE_FMT)parameters->decod_format, false);
			if(unsupportedStdout)
			   spdlog::error("Output format does not support decompress to stdout");
			else if(!inForArg.isSet())
			   spdlog::error("Missing input file");
			else
			   fail = false;
			if(fail)
			   return GrkRCFail;
		 }
	  }
	  if(outputFileArg.isSet())
	  {
		 char* outfile = (char*)outputFileArg.getValue().c_str();
		 parameters->cod_format = grk_get_file_format(outfile, isHT);
		 switch(parameters->cod_format)
		 {
			case GRK_FMT_J2K:
			case GRK_FMT_JP2:
			   break;
			default:
			   spdlog::error("Unknown output format image {} [only *.j2k, *.j2c, *.jp2, *.jpc, *.jph or *.jhc] supported ",
							 outfile);
			   return GrkRCFail;
		 }
		 if (isHT)
			 setHT(parameters,compressionRatiosArg, qualityArg);
		 if(grk::strcpy_s(parameters->outfile, sizeof(parameters->outfile), outfile) != 0)
		 {
			return GrkRCFail;
		 }
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
			return GrkRCFail;
		 memcpy(substr1, rawFormatArg.getValue().c_str(), len);
		 substr1[len] = '\0';
		 char signo;
		 if(sscanf(substr1, "%d,%d,%d,%d,%c", &width, &height, &ncomp, &bitdepth, &signo) == 5)
		 {
			if(signo == 's')
			   raw_signed = true;
			else if(signo == 'u')
			   raw_signed = false;
			else
			   wrong = true;
		 }
		 else
		 {
			wrong = true;
		 }
		 if(!wrong)
		 {
			grk_raw_cparameters* raw_cp = &parameters->raw_cp;
			uint16_t compno;
			uint32_t lastdx = 1;
			uint32_t lastdy = 1;
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
			   return GrkRCFail;
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
				  uint32_t dx, dy;
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
			return GrkRCFail;
		 }
	  }
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
			   return GrkRCFail;
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
		 if(sscanf(codeBlockDimArg.getValue().c_str(), "%d,%d", &cblockw_init, &cblockh_init) ==
			EOF)
		 {
			spdlog::error("sscanf failed for code block dimension argument");
			return GrkRCFail;
		 }
		 if(cblockw_init * cblockh_init > 4096 || cblockw_init > 1024 || cblockw_init < 4 ||
			cblockh_init > 1024 || cblockh_init < 4)
		 {
			spdlog::error("Size of code block error (option -b)\n\nRestriction :\n"
						  "    * width*height<=4096\n    * 4<=width,height<= 1024");
			return GrkRCFail;
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
			   spdlog::error("POC beginning resolution must be strictly less than end resolution");
			   return GrkRCFail;
			}
			if(progression[numProgressions].compS >= progression[numProgressions].compE)
			{
			   spdlog::error("POC beginning component must be strictly less than end component");
			   return GrkRCFail;
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
			return GrkRCFail;
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
			return GrkRCFail;
		 }
	  }
	  if(sopArg.isSet())
		 parameters->csty |= 0x02;
	  if(ephArg.isSet())
		 parameters->csty |= 0x04;
	  if(irreversibleArg.isSet())
		 parameters->irreversible = true;
	  if(guardBits.isSet())
	  {
		 if(guardBits.getValue() > 7)
		 {
			spdlog::error("Number of guard bits {} is greater than 7", guardBits.getValue());
			return GrkRCFail;
		 }
		 parameters->numgbits = (uint8_t)guardBits.getValue();
	  }
	  if(captureResArg.isSet())
	  {
		 if(sscanf(captureResArg.getValue().c_str(), "%lf,%lf", parameters->capture_resolution,
				   parameters->capture_resolution + 1) != 2)
		 {
			spdlog::error("-Q 'capture resolution' argument error  [-Q X0,Y0]");
			return GrkRCFail;
		 }
		 parameters->write_capture_resolution = true;
	  }
	  if(displayResArg.isSet())
	  {
		 if(sscanf(captureResArg.getValue().c_str(), "%lf,%lf", parameters->display_resolution,
				   parameters->display_resolution + 1) != 2)
		 {
			spdlog::error("-D 'display resolution' argument error  [-D X0,Y0]");
			return GrkRCFail;
		 }
		 parameters->write_display_resolution = true;
	  }
	  if(mctArg.isSet())
	  {
		 uint32_t mct_mode = mctArg.getValue();
		 if(mct_mode > 2)
		 {
			spdlog::error("Incorrect MCT value {}. Must be equal to 0, 1 or 2.", mct_mode);
			return GrkRCFail;
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
			return GrkRCFail;
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
			return GrkRCFail;
	  }
	  if(roiArg.isSet())
	  {
		 if(sscanf(roiArg.getValue().c_str(), "c=%u,U=%u", &parameters->roi_compno,
				   &parameters->roi_shift) != 2)
		 {
			spdlog::error("ROI argument must be of the form: [-ROI c='compno',U='shift']");
			return GrkRCFail;
		 }
	  }
	  // Canvas coordinates
	  if(tilesArg.isSet())
	  {
		 int32_t t_width = 0, t_height = 0;
		 if(sscanf(tilesArg.getValue().c_str(), "%d,%d", &t_width, &t_height) == EOF)
		 {
			spdlog::error("sscanf failed for tiles argument");
			return GrkRCFail;
		 }
		 // sanity check on tile dimensions
		 if(t_width <= 0 || t_height <= 0)
		 {
			spdlog::error("Tile dimensions ({}, {}) must be "
						  "strictly positive",
						  t_width, t_height);
			return GrkRCFail;
		 }
		 parameters->t_width = (uint32_t)t_width;
		 parameters->t_height = (uint32_t)t_height;
		 parameters->tile_size_on = true;
	  }
	  if(tileOffsetArg.isSet())
	  {
		 int32_t off1, off2;
		 if(sscanf(tileOffsetArg.getValue().c_str(), "%d,%d", &off1, &off2) != 2)
		 {
			spdlog::error("-T 'tile offset' argument must be in the form: -T X0,Y0");
			return GrkRCFail;
		 }
		 if(off1 < 0 || off2 < 0)
		 {
			spdlog::error("-T 'tile offset' values ({},{}) can't be negative", off1, off2);
			return GrkRCFail;
		 }
		 parameters->tx0 = (uint32_t)off1;
		 parameters->ty0 = (uint32_t)off2;
	  }
	  if(imageOffsetArg.isSet())
	  {
		 int32_t off1, off2;
		 if(sscanf(imageOffsetArg.getValue().c_str(), "%d,%d", &off1, &off2) != 2)
		 {
			spdlog::error("-d 'image offset' argument must be specified as:  -d x0,y0");
			return GrkRCFail;
		 }
		 if(off1 < 0 || off2 < 0)
		 {
			spdlog::error("-T 'image offset' values ({},{}) can't be negative", off1, off2);
			return GrkRCFail;
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
			return GrkRCFail;
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
			   return GrkRCFail;
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
	  if(!isHT && cblkSty.isSet())
	  {
		 parameters->cblk_sty = cblkSty.getValue() & 0X7F;
		 if(parameters->cblk_sty == GRK_CBLKSTY_HT)
		 {
		   spdlog::error("High throughput compression mode cannot be be used for non HTJ2K file");
		   return GrkRCFail;
		 }
	  }
	  if(!isHT && compressionRatiosArg.isSet() && qualityArg.isSet())
	  {
		 spdlog::error("compression by both rate distortion and quality is not allowed");
		 return GrkRCFail;
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
			   return GrkRCFail;
			}
			if(parameters->layer_rate[i] < 1.0)
			{
			   spdlog::error("rates must be greater than or equal to one");
			   return GrkRCFail;
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
			   return GrkRCFail;
			}
			if(distortion < lastDistortion &&
			   !(i == (uint16_t)(parameters->numlayers - 1) && distortion == 0))
			{
			   spdlog::error("PSNR values must be listed in ascending order");
			   return GrkRCFail;
			}
			lastDistortion = distortion;
		 }
	  }
#else
	  if(!cinema2KArg.isSet() && !cinema4KArg.isSet())
		 return GrkRCFail;

#endif
	  if(pluginPathArg.isSet())
	  {
		 if(pluginPath)
			strcpy(pluginPath, pluginPathArg.getValue().c_str());
	  }
	  inputFolder->set_imgdir = false;
	  if(batchSrcArg.isSet())
	  {
		 // first check if this is a comma separated list
		 std::stringstream ss(batchSrcArg.getValue());
		 uint32_t count = 0;
		 while(ss.good())
		 {
			std::string substr;
			std::getline(ss, substr, ',');
			count++;
		 }
		 if(count >= 6)
		 {
			parameters->sharedMemoryInterface = true;
		 }
		 else
		 {
			if(!validateDirectory(batchSrcArg.getValue()))
			   return GrkRCFail;
		 }
		 inputFolder->imgdirpath = (char*)malloc(strlen(batchSrcArg.getValue().c_str()) + 1);
		 strcpy(inputFolder->imgdirpath, batchSrcArg.getValue().c_str());
		 inputFolder->set_imgdir = true;
	  }
	  if(outFolder)
	  {
		 outFolder->set_imgdir = false;
		 if(outDirArg.isSet())
		 {
			if(!validateDirectory(outDirArg.getValue()))
			   return GrkRCFail;
			outFolder->imgdirpath = (char*)malloc(strlen(outDirArg.getValue().c_str()) + 1);
			strcpy(outFolder->imgdirpath, outDirArg.getValue().c_str());
			outFolder->set_imgdir = true;
		 }
	  }
	  if(kernelBuildOptionsArg.isSet())
		 parameters->kernelBuildOptions = kernelBuildOptionsArg.getValue();
	  if(!isHT && !qualityArg.isSet() && !compressionRatiosArg.isSet())
	  {
		 /* if no rate was entered, then lossless by default */
		 parameters->layer_rate[0] = 0;
		 parameters->numlayers = 1;
		 parameters->allocationByRateDistoration = false;
	  }
	  // cinema/broadcast profiles
	  if(!isHT)
	  {
		 if(cinema2KArg.isSet())
		 {
			if(!validateCinema(&cinema2KArg, GRK_PROFILE_CINEMA_2K, parameters))
			   return GrkRCFail;
			parameters->writeTLM = true;
			spdlog::warn("Cinema 2K profile activated. Other options specified may be overridden");
		 }
		 else if(cinema4KArg.isSet())
		 {
			if(!validateCinema(&cinema4KArg, GRK_PROFILE_CINEMA_4K, parameters))
			   return GrkRCFail;
			spdlog::warn("Cinema 4K profile activated. Other options specified may be overridden");
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
			if(comma && sscanf(comma + 1, "mainlevel=%d", &mainlevel) != 1)
			{
			   spdlog::error("{}", msg);
			   return GrkRCFail;
			}
			comma = strstr(arg, ",framerate=");
			if(comma && sscanf(comma + 1, "framerate=%d", &framerate) != 1)
			{
			   spdlog::error("{}", msg);
			   return GrkRCFail;
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
			   return GrkRCFail;
			}

			if(!(mainlevel >= 0 && mainlevel <= 11))
			{
			   /* Voluntarily rough validation. More fine grained done in library */
			   spdlog::error("Invalid mainlevel value {}.", mainlevel);
			   return GrkRCFail;
			}
			parameters->rsiz = (uint16_t)(profile | mainlevel);
			spdlog::warn("Broadcast profile activated. Other options specified may be overridden");
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
			   spdlog::info("Setting max code stream size to {} bytes.", parameters->max_cs_size);
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
			if(comma && sscanf(comma + 1, "mainlevel=%d", &mainlevel) != 1)
			{
			   spdlog::error("{}", msg);
			   return GrkRCFail;
			}

			comma = strstr(arg, ",sublevel=");
			if(comma && sscanf(comma + 1, "sublevel=%d", &sublevel) != 1)
			{
			   spdlog::error("{}", msg);
			   return GrkRCFail;
			}

			comma = strstr(arg, ",framerate=");
			if(comma && sscanf(comma + 1, "framerate=%d", &framerate) != 1)
			{
			   spdlog::error("{}", msg);
			   return GrkRCFail;
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
			   return GrkRCFail;
			}
			if(!(mainlevel >= 0 && mainlevel <= 11))
			{
			   /* Voluntarily rough validation. More fine grained done in library */
			   spdlog::error("Invalid main level {}.", mainlevel);
			   return GrkRCFail;
			}
			if(!(sublevel >= 0 && sublevel <= 9))
			{
			   /* Voluntarily rough validation. More fine grained done in library */
			   spdlog::error("Invalid sub-level {}.", sublevel);
			   return GrkRCFail;
			}
			parameters->rsiz = (uint16_t)(profile | (sublevel << 4) | mainlevel);
			spdlog::warn("IMF profile activated. Other options specified may be overridden");

			parameters->framerate = (uint16_t)framerate;
			if(framerate > 0 && sublevel != 0)
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
			   spdlog::info("Setting max code stream size to {} bytes.", parameters->max_cs_size);
			}
			parameters->writeTLM = true;
		 }
		 if(rsizArg.isSet())
		 {
			if(cinema2KArg.isSet() || cinema4KArg.isSet())
			   grk::warningCallback("Cinema profile set - rsiz parameter ignored.", nullptr);
			else if(IMFArg.isSet())
			   grk::warningCallback("IMF profile set - rsiz parameter ignored.", nullptr);
			else
			   parameters->rsiz = rsizArg.getValue();
		 }
	  }
	  else
	  {
		 parameters->rsiz |= GRK_JPH_RSIZ_FLAG;
	  }
	  if(outForArg.isSet())
	  {
		 std::string outformat = std::string(".") + outForArg.getValue();
		 inputFolder->set_out_format = true;
		 parameters->cod_format = grk_get_file_format(outformat.c_str(), isHT);
		 switch(parameters->cod_format)
		 {
			case GRK_FMT_J2K:
			   inputFolder->out_format = "j2k";
			   break;
			case GRK_FMT_JP2:
			   inputFolder->out_format = "jp2";
			   break;
			default:
			   spdlog::error("Unknown output format image [only *.j2k, *.j2c, *.jp2, *.jpc, *.jph or *.jhc] supported");
			   return GrkRCFail;
		 }
		 if (isHT)
			 setHT(parameters,compressionRatiosArg, qualityArg);
	  }
	  if(serverArg.isSet() && licenseArg.isSet())
	  {
		 initParams->server_ = serverArg.getValue();
		 initParams->license_ = licenseArg.getValue();
	  }
   }
   catch(const TCLAP::ArgException& e) // catch any exceptions
   {
	  std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
	  return GrkRCFail;
   }
   catch(const TCLAP::ExitException& e) // catch any exceptions
   {
	  return GrkRCUsage;
   }

   if(inputFolder->set_imgdir)
   {
	  if(!(parameters->infile[0] == 0))
	  {
		 spdlog::error("options -batch_src and -in_file cannot be used together ");
		 return GrkRCFail;
	  }
	  if(!inputFolder->set_out_format)
	  {
		 spdlog::error("When -batch_src is used, -out_fmt <FORMAT> must be used ");
		 spdlog::error("Only one format allowed! Valid formats are j2k and jp2");
		 return GrkRCFail;
	  }
	  if(!((parameters->outfile[0] == 0)))
	  {
		 spdlog::error("options -batch_src and -out_file cannot be used together ");
		 spdlog::error("Specify OutputFormat using -out_fmt<FORMAT> ");
		 return GrkRCFail;
	  }
   }
   else
   {
	  if(parameters->cod_format == GRK_FMT_UNK && !initParams->in_image)
	  {
		 if(parameters->infile[0] == 0)
		 {
			spdlog::error("Missing input file parameter\n"
						  "Example: {} -i image.pgm -o image.j2k",
						  argv[0]);
			spdlog::error("   Help: {} -h", argv[0]);
			return GrkRCFail;
		 }
	  }

	  if(parameters->outfile[0] == 0 && !initParams->stream_)
	  {
		 spdlog::error("Missing output file parameter\n"
					   "Example: {} -i image.pgm -o image.j2k",
					   argv[0]);
		 spdlog::error("   Help: {} -h", argv[0]);
		 return GrkRCFail;
	  }
   }
   if((parameters->decod_format == GRK_FMT_RAW && parameters->raw_cp.width == 0) ||
	  (parameters->decod_format == GRK_FMT_RAWL && parameters->raw_cp.width == 0))
   {
	  spdlog::error("invalid raw image parameters");
	  spdlog::error("Please use the Format option -F:");
	  spdlog::error("-F rawWidth,rawHeight,rawComp,rawBitDepth,s/u (Signed/Unsigned)");
	  spdlog::error("Example: -i lena.raw -o lena.j2k -F 512,512,3,8,u");
	  return GrkRCFail;
   }
   if((parameters->tx0 > 0 && parameters->tx0 > parameters->image_offset_x0) ||
	  (parameters->ty0 > 0 && parameters->ty0 > parameters->image_offset_y0))
   {
	  spdlog::error("Tile offset cannot be greater than image offset : TX0({})<=IMG_X0({}) "
					"TYO({})<=IMG_Y0({}) ",
					parameters->tx0, parameters->image_offset_x0, parameters->ty0,
					parameters->image_offset_y0);
	  return GrkRCFail;
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
	  return GrkRCFail;
   }

   return GrkRCSuccess;
}

static uint64_t pluginCompressCallback(grk_plugin_compress_user_callback_info* info)
{
   auto parameters = info->compressor_parameters;
   uint64_t compressedBytes = 0;
   grk_codec* codec = nullptr;
   grk_image* image = info->image;
   bool createdImage = false;
   std::string outfile;
   std::string temp_ofname;

   bool hasStreamParams = info->stream_params.file || info->stream_params.buf;
   // get output file
   if(!hasStreamParams)
   {
	  if(info->output_file_name && info->output_file_name[0])
	  {
		 if(info->outputFileNameIsRelative)
		 {
			temp_ofname = get_file_name((char*)info->output_file_name);
			if(img_fol_plugin.set_out_format)
			{
			   outfile = (out_fol_plugin.imgdirpath ? out_fol_plugin.imgdirpath
													: img_fol_plugin.imgdirpath);
			   outfile += grk::pathSeparator() + temp_ofname + "." + img_fol_plugin.out_format;
			}
		 }
		 else
		 {
			outfile = info->output_file_name;
		 }
	  }
	  else
	  {
		 goto cleanup;
	  }
   }

   // read image from disk if in-memory image is not available
   if(!image)
   {
	  if(parameters->decod_format == GRK_FMT_UNK)
	  {
		 int fmt = grk_get_file_format((char*)info->input_file_name);
		 if(fmt <= GRK_FMT_UNK)
		 {
			goto cleanup;
		 }
		 parameters->decod_format = (GRK_SUPPORTED_FILE_FMT)fmt;
		 if(!isDecodedFormatSupported(parameters->decod_format))
		 {
			goto cleanup;
		 }
	  }
	  /* decode the source image */
	  switch(info->compressor_parameters->decod_format)
	  {
		 case GRK_FMT_PGX: {
			PGXFormat pgx;
			image = pgx.decode(info->input_file_name, info->compressor_parameters);
			if(!image)
			{
			   spdlog::error("Unable to load pgx file");
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
			   goto cleanup;
			}
		 }
		 break;

#ifdef GROK_HAVE_LIBTIFF
		 case GRK_FMT_TIF: {
			TIFFFormat tif;
			image = tif.decode(info->input_file_name, info->compressor_parameters);
			if(!image)
			   goto cleanup;
		 }
		 break;
#endif /* GROK_HAVE_LIBTIFF */

		 case GRK_FMT_RAW: {
			RAWFormat raw(true);
			image = raw.decode(info->input_file_name, info->compressor_parameters);
			if(!image)
			{
			   spdlog::error("Unable to load raw file");
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
			   goto cleanup;
			}
		 }
		 break;
#endif /* GROK_HAVE_LIBPNG */
		 default: {
			spdlog::error("Input file format {} is not supported",
						  convertFileFmtToString(info->compressor_parameters->decod_format));
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
		 goto cleanup;
	  }
	  createdImage = true;
   }
#if 0
	if(hasStreamParams)
	{
		info->stream_params.len = info->stream->len;
		info->stream_params.buf = info->stream->buf;
		/*
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
			info->stream_params.len =
				(size_t)fileLength > imageSize ? (size_t)fileLength : imageSize;
			info->stream_params.buf = new uint8_t[info->stream_params.len];
		}
		*/
	}
#endif
   // limit to 16 bit precision
   for(uint32_t i = 0; i < image->numcomps; ++i)
   {
	  if(image->comps[i].prec > GRK_MAX_SUPPORTED_IMAGE_PRECISION)
	  {
		 spdlog::error("precision = {} not supported:", image->comps[i].prec);
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
		 goto cleanup;
	  }
	  if((parameters->mct == 2) && (!parameters->mct_data))
	  {
		 spdlog::error("Custom MCT has been set but no array-based MCT");
		 spdlog::error("has been provided.");
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

   if(!info->stream_params.buf)
	  info->stream_params.file = outfile.c_str();
   codec = grk_compress_init(&info->stream_params, parameters, image);
   if(!codec)
   {
	  spdlog::error("failed to compress image: grk_compress_init");
	  goto cleanup;
   }
   compressedBytes = grk_compress(codec, info->tile);
   if(!compressedBytes)
   {
	  spdlog::error("failed to compress image: grk_compress");
	  goto cleanup;
   }
#ifdef GROK_HAVE_EXIFTOOL
   if(compressedBytes && info->transferExifTags &&
	  info->compressor_parameters->cod_format == GRK_FMT_JP2)
	  transferExifTags(info->input_file_name, info->output_file_name);
#endif

cleanup:
   grk_object_unref(codec);
   if(createdImage)
	  grk_object_unref(&image->obj);
   if(!compressedBytes)
   {
	  spdlog::error("failed to compress image");
	  if(parameters->outfile[0])
	  {
		 bool allocated = false;
		 char* p = actual_path(parameters->outfile, &allocated);
		 remove(p);
		 if(allocated)
			free(p);
	  }
   }
   return compressedBytes;
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
   callbackInfo.image = initParams->in_image;
   if(initParams->stream_)
	  callbackInfo.stream_params = *initParams->stream_;
   callbackInfo.output_file_name = initParams->parameters.outfile;
   callbackInfo.input_file_name = initParams->parameters.infile;
   callbackInfo.transferExifTags = initParams->transferExifTags;

   uint64_t compressedBytes = pluginCompressCallback(&callbackInfo);
   if(initParams->stream_)
	  initParams->stream_->buf_compressed_len = compressedBytes;

   return compressedBytes ? 1 : 0;
}

} // namespace grk
