/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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
#include "common.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
using namespace std::chrono_literals;

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace grk
{
std::condition_variable sleep_cv;
std::mutex sleep_cv_m;
// val == # of 100ms increments to wait
int batch_sleep(int val)
{
	std::unique_lock<std::mutex> lk(sleep_cv_m);
	sleep_cv.wait_for(lk, val * 100ms, [] { return false; });
	return 0;
};

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

/* -------------------------------------------------------------------------- */
/**
 * Parse decoding area input values
 * separator = ","
 */
/* -------------------------------------------------------------------------- */
int parseWindowBounds(char* inArg, uint32_t* DA_x0, uint32_t* DA_y0, uint32_t* DA_x1,
					  uint32_t* DA_y1)
{
	int it = 0;
	int values[4];
	char delims[] = ",";
	char* result = nullptr;
	result = strtok(inArg, delims);

	while((result != nullptr) && (it < 4))
	{
		values[it] = atoi(result);
		result = strtok(nullptr, delims);
		it++;
	}

	// region must be specified by 4 values exactly
	if(it != 4)
	{
		spdlog::warn("Decompress window must be specified by exactly "
					 "four coordinates. Ignoring specified region.");
		return EXIT_FAILURE;
	}

	// don't allow negative values
	if((values[0] < 0 || values[1] < 0 || values[2] < 0 || values[3] < 0))
	{
		spdlog::warn("Decompress window cannot contain negative "
					 "values.\n Ignoring specified region ({},{},{},{}).",
					 values[0], values[1], values[2], values[3]);
		return EXIT_FAILURE;
	}
	if(values[2] <= values[0] || values[3] <= values[1])
	{
		spdlog::warn("Decompress window must have strictly "
					 "positive area.\n Ignoring specified window ({},{},{},{}).",
					 values[0], values[1], values[2], values[3]);
		return EXIT_FAILURE;
	}

	*DA_x0 = (uint32_t)values[0];
	*DA_y0 = (uint32_t)values[1];
	*DA_x1 = (uint32_t)values[2];
	*DA_y1 = (uint32_t)values[3];
	return EXIT_SUCCESS;
}

bool safe_fclose(FILE* fd)
{
	if(!fd)
		return true;
	return fclose(fd) ? false : true;
}

bool useStdio(const char* filename)
{
	return (filename == nullptr) || (filename[0] == 0);
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
	(void)file;
	return true;
#endif
}

bool grk_open_for_output(FILE** fdest, const char* outfile, bool writeToStdout)
{
	assert(fdest);
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
    	(void)entry;
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

/**
 * return false if :
 * 1. any component's data buffer is NULL
 * 2. any component's precision is either 0 or greater than GRK_MAX_SUPPORTED_IMAGE_PRECISION
 * 3. any component's signedness does not match another component's signedness
 * 4. any component's precision does not match another component's precision
 *    (if equalPrecision is true)
 *
 */
bool allComponentsSanityCheck(grk_image* image, bool equalPrecision)
{
	if(!image || image->numcomps == 0)
		return false;
	auto comp0 = image->comps;

	if(!comp0->data)
	{
		spdlog::error("component 0 : data is null.");
		return false;
	}
	if(comp0->prec == 0 || comp0->prec > GRK_MAX_SUPPORTED_IMAGE_PRECISION)
	{
		spdlog::warn("component 0 precision {} is not supported.", 0, comp0->prec);
		return false;
	}

	for(uint16_t i = 1U; i < image->numcomps; ++i)
	{
		auto compi = image->comps + i;

		if(!compi->data)
		{
			spdlog::error("component {} : data is null.", i);
			return false;
		}
		if(equalPrecision && comp0->prec != compi->prec)
		{
			spdlog::warn("precision {} of component {}"
						 " differs from precision {} of component 0.",
						 compi->prec, i, comp0->prec);
			return false;
		}
		if(comp0->sgnd != compi->sgnd)
		{
			spdlog::warn("signedness {} of component {}"
						 " differs from signedness {} of component 0.",
						 compi->sgnd, i, comp0->sgnd);
			return false;
		}
	}
	return true;
}

bool areAllComponentsSameSubsampling(grk_image* image)
{
	if(!image || image->numcomps == 1)
		return true;
	auto comp0 = image->comps;
	for(uint32_t i = 0; i < image->numcomps; ++i)
	{
		auto comp = image->comps + i;
		if(comp->dx != comp0->dx || comp->dy != comp0->dy)
		{
			spdlog::error("Not all components have same sub-sampling");
			return false;
		}
	}
	return true;
}

bool isSubsampled(grk_image* image)
{
	if(!image)
		return false;
	for(uint32_t i = 0; i < image->numcomps; ++i)
	{
		if(image->comps[i].dx != 1 || image->comps[i].dy != 1)
			return true;
	}
	return false;
}

bool isChromaSubsampled(grk_image* image)
{
	if(!image || image->numcomps < 3)
		return false;
	for(uint32_t i = 0; i < image->numcomps; ++i)
	{
		auto comp = image->comps + i;
		switch(i)
		{
			case 1:
			case 2:
				if(comp->type != GRK_COMPONENT_TYPE_COLOUR)
					return false;
				break;
			default:
				if(comp->dx != 1 || comp->dy != 1)
				{
					return false;
				}
				break;
		}
	}
	auto compB = image->comps + 1;
	auto compR = image->comps + 2;

	return (compB->dx == compR->dx && compB->dy == compR->dy);
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
	(void)client_data;
	spdlog::default_logger()->error(msg);
}
void warningCallback(const char* msg, void* client_data)
{
	(void)client_data;
	spdlog::default_logger()->warn(msg);
}
void infoCallback(const char* msg, void* client_data)
{
	(void)client_data;
	spdlog::default_logger()->info(msg);
}

} // namespace grk
