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

#include "common.h"

namespace grk
{
std::string convertFileFmtToString(GRK_SUPPORTED_FILE_FMT fmt)
{
	switch(fmt)
	{
		case GRK_J2K_FMT:
			return "J2K";
			break;
		case GRK_JP2_FMT:
			return "JP2";
			break;
		case GRK_PXM_FMT:
			return "PNM";
			break;
		case GRK_PGX_FMT:
			return "PGX";
			break;
		case GRK_PAM_FMT:
			return "PAM";
			break;
		case GRK_BMP_FMT:
			return "BMP";
			break;
		case GRK_TIF_FMT:
			return "TIFF";
			break;
		case GRK_RAW_FMT:
			return "RAW";
			break;
		case GRK_PNG_FMT:
			return "PNG";
			break;
		case GRK_RAWL_FMT:
			return "RAWL";
			break;
		case GRK_JPG_FMT:
			return "JPEG";
			break;
		case GRK_UNK_FMT:
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

bool useStdio(std::string  filename)
{
	return  filename.empty();
}

bool supportedStdioFormat(GRK_SUPPORTED_FILE_FMT format)
{
	for(size_t i = 0; i < sizeof(supportedStdoutFileFormats) / sizeof(GRK_SUPPORTED_FILE_FMT); ++i)
	{
		if(supportedStdoutFileFormats[i] == format)
		{
			return true;
		}
	}
	return false;
}

bool grk_set_binary_mode(FILE* file)
{
#ifdef _WIN32
	return (_setmode(_fileno(file), _O_BINARY) != -1);
#else
	GRK_UNUSED(file);
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

GRK_SUPPORTED_FILE_FMT get_file_format(const char* filename)
{
	const char* ext = strrchr(filename, '.');
	if(ext == nullptr)
		return GRK_UNK_FMT;
	ext++;
	if(*ext)
	{
		static const char* extension[] = {"pgx",  "pam", "pnm",	 "pgm", "ppm",	"pbm",
										  "bmp",  "tif", "tiff", "jpg", "jpeg", "raw",
										  "rawl", "png", "j2k",	 "jp2", "j2c",	"jpc"};
		static const GRK_SUPPORTED_FILE_FMT format[] = {
			GRK_PGX_FMT,  GRK_PXM_FMT, GRK_PXM_FMT, GRK_PXM_FMT, GRK_PXM_FMT, GRK_PXM_FMT,
			GRK_BMP_FMT,  GRK_TIF_FMT, GRK_TIF_FMT, GRK_JPG_FMT, GRK_JPG_FMT, GRK_RAW_FMT,
			GRK_RAWL_FMT, GRK_PNG_FMT, GRK_J2K_FMT, GRK_JP2_FMT, GRK_J2K_FMT, GRK_J2K_FMT};
		for(uint32_t i = 0; i < sizeof(format) / sizeof(*format); i++)
		{
			if(strcasecmp(ext, extension[i]) == 0)
				return format[i];
		}
	}

	return GRK_UNK_FMT;
}

#define JP2_RFC3745_MAGIC "\x00\x00\x00\x0c\x6a\x50\x20\x20\x0d\x0a\x87\x0a"
#define J2K_CODESTREAM_MAGIC "\xff\x4f\xff\x51"
bool jpeg2000_file_format(const char* fname, GRK_SUPPORTED_FILE_FMT* fmt)
{
	FILE* reader;
	GRK_SUPPORTED_FILE_FMT ext_format = GRK_UNK_FMT, magic_format = GRK_UNK_FMT;
	uint8_t buf[12];
	size_t nb_read;

	reader = fopen(fname, "rb");
	if(reader == nullptr)
		return false;

	memset(buf, 0, 12);
	nb_read = fread(buf, 1, 12, reader);
	if(!grk::safe_fclose(reader))
		return false;

	if(nb_read != 12)
		return false;

	int temp = get_file_format(fname);
	if(temp > GRK_UNK_FMT)
		ext_format = (GRK_SUPPORTED_FILE_FMT)temp;

	if(memcmp(buf, JP2_RFC3745_MAGIC, 12) == 0)
	{
		magic_format = GRK_JP2_FMT;
	}
	else if(memcmp(buf, J2K_CODESTREAM_MAGIC, 4) == 0)
	{
		magic_format = GRK_J2K_FMT;
	}
	else
	{
		spdlog::error("{} does not contain a JPEG 2000 code stream", fname);
		*fmt = GRK_UNK_FMT;
		return false;
	}

	if(magic_format == ext_format)
	{
		*fmt = ext_format;
		return true;
	}
	*fmt = magic_format;
	return true;
}

bool isFinalOutputSubsampled(grk_image* image)
{
	assert(image);
	if (image->upsample || image->forceRGB)
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
    for (const auto & entry : std::filesystem::directory_iterator(imgdirpath))
	{
    	GRK_UNUSED(entry);
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

void errorCallback(const char* msg, void* client_data)
{
	GRK_UNUSED(client_data);
	spdlog::default_logger()->error(msg);
}
void warningCallback(const char* msg, void* client_data)
{
	GRK_UNUSED(client_data);
	spdlog::default_logger()->warn(msg);
}
void infoCallback(const char* msg, void* client_data)
{
	GRK_UNUSED(client_data);
	spdlog::default_logger()->info(msg);
}

} // namespace grk
