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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _MSC_VER
#include <intrin.h>
#define strcasecmp _stricmp
#endif

#include "spdlog/spdlog.h"
#include "common.h"

namespace grk
{

ChronoTimer::ChronoTimer(const std::string &msg) : message(msg){
}
void ChronoTimer::start(void){
	startTime = std::chrono::high_resolution_clock::now();
}
void ChronoTimer::finish(void){
	auto finish = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> elapsed = finish - startTime;
	spdlog::info("{} : {} ms",message, elapsed.count() * 1000);
}

bool validateDirectory(std::string dir){
	if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)){
		spdlog::error("Directory {} does not exist or is not in fact a directory",dir);
		return false;
	}

	return true;
}
std::string convertFileFmtToString(GRK_SUPPORTED_FILE_FMT fmt)
{
	switch(fmt)
	{
		case GRK_FMT_J2K:
			return "J2K";
			break;
		case GRK_FMT_JP2:
			return "JP2";
			break;
		case GRK_FMT_PXM:
			return "PNM";
			break;
		case GRK_FMT_PGX:
			return "PGX";
			break;
		case GRK_FMT_PAM:
			return "PAM";
			break;
		case GRK_FMT_BMP:
			return "BMP";
			break;
		case GRK_FMT_TIF:
			return "TIFF";
			break;
		case GRK_FMT_RAW:
			return "RAW";
			break;
		case GRK_FMT_PNG:
			return "PNG";
			break;
		case GRK_FMT_RAWL:
			return "RAWL";
			break;
		case GRK_FMT_JPG:
			return "JPEG";
			break;
		case GRK_FMT_UNK:
		default:
			return "UNKNOWN";
			break;
	}
}

bool parseWindowBounds(char* inArg, float* dw_x0, float* dw_y0, float* dw_x1,	float* dw_y1)
{
	int it = 0;
	float val[4];
	char delims[] = ",";
	char* result = nullptr;
	result = strtok(inArg, delims);

	while((result != nullptr) && (it < 4))
	{
		val[it] = (float)atof(result);
		result = strtok(nullptr, delims);
		it++;
	}

	// region must be specified by 4 values exactly
	if(it != 4)
	{
		spdlog::warn("Decompress region must be specified by exactly "
					 "four coordinates. Ignoring specified region.");
		return false;
	}

	// don't allow negative values
	if((val[0] < 0 || val[1] < 0 || val[2] < 0 || val[3] < 0))
	{
		spdlog::warn("Decompress region cannot contain negative "
					 "values.\n Ignoring specified region ({},{},{},{}).",
					 val[0], val[1], val[2], val[3]);
		return false;
	}
	if(val[2] <= val[0] || val[3] <= val[1])
	{
		spdlog::warn("Decompress region must have strictly "
					 "positive area.\n Ignoring specified region ({},{},{},{}).",
					 val[0], val[1], val[2], val[3]);
		return false;
	}

	//sanity check
	bool allLessThanOne = true;
	for (uint8_t i = 0; i < 4; ++i){
		if (val[i] > 1.0f)
			allLessThanOne = false;
	}
	// note: special case of [0,0,1,1] is interpreted as relative coordinates
	if (!allLessThanOne && (val[0] != 0 || val[1]!= 0 || val[2] != 1 || val[3] != 1)){
		for (uint8_t i = 0; i < 4; ++i){
			if (val[i] != (uint32_t)val[i]){
				spdlog::warn("Decompress region in absolute coordinates must only contain integers."
						"\n Ignoring specified region ({},{},{},{}).", 	 val[0], val[1], val[2], val[3]);
				return false;
			}
		}
	}

	*dw_x0 = val[0];
	*dw_y0 = val[1];
	*dw_x1 = val[2];
	*dw_y1 = val[3];

	return true;
}

bool safe_fclose(FILE* file)
{
	if(!file)
		return true;
	return fclose(file) ? false : true;
}

bool useStdio(const std::string  &filename)
{
	return  filename.empty();
}

bool supportedStdioFormat(GRK_SUPPORTED_FILE_FMT format, bool compress)
{
    if (compress) {
        for(size_t i = 0; i < sizeof(supportedStdoutFileFormatsCompress) / sizeof(GRK_SUPPORTED_FILE_FMT); ++i)
            if(supportedStdoutFileFormatsCompress[i] == format)
                return true;
    } else {
        for(size_t i = 0; i < sizeof(supportedStdoutFileFormatsDecompress) / sizeof(GRK_SUPPORTED_FILE_FMT); ++i)
            if(supportedStdoutFileFormatsDecompress[i] == format)
                return true;
    }
	return false;
}

bool grk_set_binary_mode([[maybe_unused]] FILE* file)
{
#ifdef _WIN32
	return (_setmode(_fileno(file), _O_BINARY) != -1);
#else
	return true;
#endif
}

bool grk_open_for_output(FILE** fdest, const char* outfile, bool writeToStdout)
{
	assert(fdest);
	assert(!*fdest);
	if(writeToStdout)
	{
		if(!grk::grk_set_binary_mode(stdout))
			return false;
		*fdest = stdout;
	}
	else
	{
		*fdest = fopen(outfile, "wb");
		if(!*fdest)
		{
			spdlog::error("failed to open {} for writing", outfile);
			return false;
		}
	}
	return true;
}

GRK_SUPPORTED_FILE_FMT grk_get_file_format(const char* filename, bool &isHTJ2K)
{
	const char* ext = strrchr(filename, '.');
	isHTJ2K = false;
	if(ext == nullptr)
		return GRK_FMT_UNK;
	ext++;
	if(*ext)
	{
		static const char* extension[] = {"pgx",  "pam", "pnm",	 "pgm", "ppm",	"pbm",
										  "bmp",  "tif", "tiff", "jpg", "jpeg", "raw",
										  "rawl", "png", "j2k",	 "jp2", "j2c",	"jpc", "jph", "jhc"};
		static const GRK_SUPPORTED_FILE_FMT format[] = {
			GRK_FMT_PGX,  GRK_FMT_PXM, GRK_FMT_PXM, GRK_FMT_PXM, GRK_FMT_PXM, GRK_FMT_PXM,
			GRK_FMT_BMP,  GRK_FMT_TIF, GRK_FMT_TIF, GRK_FMT_JPG, GRK_FMT_JPG, GRK_FMT_RAW,
			GRK_FMT_RAWL, GRK_FMT_PNG, GRK_FMT_J2K, GRK_FMT_JP2, GRK_FMT_J2K, GRK_FMT_J2K, GRK_FMT_JP2, GRK_FMT_J2K};
		for(uint32_t i = 0; i < sizeof(format) / sizeof(*format); i++)
		{
			if(strcasecmp(ext, extension[i]) == 0) {
				if (i == 18 || i == 19)
					isHTJ2K = true;
				return format[i];
			}
		}
	}

	return GRK_FMT_UNK;
}

GRK_SUPPORTED_FILE_FMT grk_get_file_format(const char* filename) {
	bool isHTJ2K;
	return grk_get_file_format(filename, isHTJ2K);
}

bool isFinalOutputSubsampled(grk_image* image)
{
	assert(image);
	if (image->upsample || image->force_rgb)
		return false;
	for(uint32_t i = 0; i < image->numcomps; ++i)
	{
		if(image->comps[i].dx != 1 || image->comps[i].dy != 1)
			return true;
	}
	return false;
}


const char* pathSeparator()
{
#ifdef _WIN32
	return "\\";
#else
	return "/";
#endif
}

char* get_file_name(char* name)
{
	return strtok(name, ".");
}

uint32_t get_num_images(char* imgdirpath)
{
	uint32_t i = 0;
    for (const auto & entry : std::filesystem::directory_iterator(imgdirpath)) {
    	if (entry.is_regular_file())
    		i++;
    }

    return i;
}

char* actual_path(const char* outfile, bool* mem_allocated)
{
	if(mem_allocated)
		*mem_allocated = false;
	if(!outfile)
		return nullptr;
#ifdef _WIN32
	return (char*)outfile;
#else
	char* actualpath = realpath(outfile, nullptr);
	if(actualpath != nullptr)
	{
		if(mem_allocated)
			*mem_allocated = true;
		return actualpath;
	}
	return (char*)outfile;
#endif
}

uint32_t uint_adds(uint32_t a, uint32_t b)
{
	uint64_t sum = (uint64_t)a + (uint64_t)b;
	return (uint32_t)(-(int32_t)(sum >> 32)) | (uint32_t)sum;
}


int population_count(uint32_t val)
{
#ifdef _MSC_VER
	return __popcnt(val);
#elif(defined(__GNUC__) || defined(__clang__))
	return __builtin_popcount(val);
#else
	val -= ((val >> 1) & 0x55555555);
	val = (((val >> 2) & 0x33333333) + (val & 0x33333333));
	val = (((val >> 4) + val) & 0x0f0f0f0f);
	val += (val >> 8);
	val += (val >> 16);
	return (int)(val & 0x0000003f);
#endif
}

#ifdef _MSC_VER
#pragma intrinsic(_BitScanReverse)
#endif
int count_leading_zeros(uint32_t val)
{
#ifdef _MSC_VER
	unsigned long result = 0;
	_BitScanReverse(&result, val);
	return 31 ^ (int)result;
#elif(defined(__GNUC__) || defined(__clang__))
	return __builtin_clz(val);
#else
	val |= (val >> 1);
	val |= (val >> 2);
	val |= (val >> 4);
	val |= (val >> 8);
	val |= (val >> 16);
	return 32 - population_count(val);
#endif
}

#ifdef _MSC_VER
#pragma intrinsic(_BitScanForward)
#endif
int count_trailing_zeros(uint32_t val)
{
#ifdef _MSC_VER
	unsigned long result = 0;
	_BitScanForward(&result, val);
	return (int)result;
#elif(defined(__GNUC__) || defined(__clang__))
	return __builtin_ctz(val);
#else
	val |= (val << 1);
	val |= (val << 2);
	val |= (val << 4);
	val |= (val << 8);
	val |= (val << 16);
	return 32 - population_count(val);
#endif
}

void errorCallback(const char* msg, [[maybe_unused]] void* client_data)
{
	spdlog::default_logger()->error(msg);
}
void warningCallback(const char* msg, [[maybe_unused]] void* client_data)
{
	spdlog::default_logger()->warn(msg);
}
void infoCallback(const char* msg, [[maybe_unused]] void* client_data)
{
	spdlog::default_logger()->info(msg);
}

} // namespace grk
