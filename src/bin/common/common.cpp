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

#include "common.h"
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <Windows.h>
#include "windirent.h"
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#endif /* _WIN32 */

#include <cstring>
#include <condition_variable>
using namespace std::chrono_literals;


namespace grk {

std::condition_variable sleep_cv;
std::mutex sleep_cv_m;
// val == # of 100ms increments to wait
int batch_sleep(int val) {
	std::unique_lock<std::mutex> lk(sleep_cv_m);
	sleep_cv.wait_for(lk, val * 100ms, [] {return false; });
	return 0;
};


/* -------------------------------------------------------------------------- */
/**
* Parse decoding area input values
* separator = ","
*/
/* -------------------------------------------------------------------------- */
int parse_DA_values(bool verbose,
					char* inArg,
					uint32_t *DA_x0,
					uint32_t *DA_y0,
					uint32_t *DA_x1,
					uint32_t *DA_y1)
{
	int it = 0;
	int values[4];
	char delims[] = ",";
	char *result = nullptr;
	result = strtok(inArg, delims);

	while ((result != nullptr) && (it < 4)) {
		values[it] = atoi(result);
		result = strtok(nullptr, delims);
		it++;
	}

	// region must be specified by 4 values exactly
	if (it != 4) {
		if (verbose)
			spdlog::warn("Decode region must be specified by exactly four coordinates. Ignoring specified region.");
		return EXIT_FAILURE;

	}

	// don't allow negative values
	if ((values[0] < 0 ||
		values[1] < 0 ||
		values[2] < 0 ||
		values[3] < 0)) {
		if (verbose)
			spdlog::warn("Decode region cannot contain negative values. Ignoring specified region ({},{},{},{}).",
				values[0], values[1], values[2], values[3]);
		return EXIT_FAILURE;
	}
	else {
		*DA_x0 = (uint32_t)values[0];
		*DA_y0 = (uint32_t)values[1];
		*DA_x1 = (uint32_t)values[2];
		*DA_y1 = (uint32_t)values[3];
		return EXIT_SUCCESS;
	}
}

bool safe_fclose(FILE* fd){
	if (!fd)
		return true;
	return fclose(fd) ? false : true;

}

bool useStdio( const char *filename){
	return (filename == nullptr) || (filename[0] == 0);
}

bool supportedStdioFormat(GRK_SUPPORTED_FILE_FMT format){
	for (size_t i = 0; i < sizeof(supportedStdoutFileFormats)/sizeof(GRK_SUPPORTED_FILE_FMT); ++i){
		if (supportedStdoutFileFormats[i] == format){
			return true;
		}
	}
	return false;
}

int get_file_format(const char *filename)
{
    unsigned int i;
    static const char *extension[] = {"pgx", "pnm", "pgm", "ppm", "bmp","tif", "tiff", "jpg", "jpeg", "raw", "rawl", "tga", "png", "j2k", "jp2","j2c", "jpc" };
    static const GRK_SUPPORTED_FILE_FMT format[] = { GRK_PGX_FMT, GRK_PXM_FMT, GRK_PXM_FMT, GRK_PXM_FMT, GRK_BMP_FMT, GRK_TIF_FMT, GRK_TIF_FMT, GRK_JPG_FMT, GRK_JPG_FMT, GRK_RAW_FMT, GRK_RAWL_FMT, GRK_TGA_FMT, GRK_PNG_FMT, GRK_J2K_FMT, GRK_JP2_FMT,GRK_J2K_FMT, GRK_J2K_FMT };
    const char * ext = strrchr(filename, '.');
    if (ext == nullptr)
        return -1;
    ext++;
    if(*ext) {
        for(i = 0; i < sizeof(format)/sizeof(*format); i++) {
            if(strcasecmp(ext, extension[i]) == 0) {
                return format[i];
            }
        }
    }

    return GRK_UNK_FMT;
}

#define JP2_RFC3745_MAGIC "\x00\x00\x00\x0c\x6a\x50\x20\x20\x0d\x0a\x87\x0a"
/* position 45: "\xff\x52" */
#define J2K_CODESTREAM_MAGIC "\xff\x4f\xff\x51"
bool jpeg2000_file_format(const char *fname, GRK_SUPPORTED_FILE_FMT* fmt)
{
    FILE *reader;
    const char *s, *magic_s;
    GRK_SUPPORTED_FILE_FMT ext_format = GRK_UNK_FMT, magic_format = GRK_UNK_FMT;
    uint8_t buf[12];
    size_t l_nb_read;

    reader = fopen(fname, "rb");
    if (reader == nullptr)
        return false;

    memset(buf, 0, 12);
    l_nb_read = fread(buf, 1, 12, reader);
    if (!grk::safe_fclose(reader)){
    	return false;
    }
    if (l_nb_read != 12)
        return false;

    int temp = get_file_format(fname);
    if (temp > GRK_UNK_FMT)
    	ext_format = (GRK_SUPPORTED_FILE_FMT)temp;

    if (memcmp(buf, JP2_RFC3745_MAGIC, 12) == 0 ) {
        magic_format = GRK_JP2_FMT;
        magic_s = ".jp2";
    } else if (memcmp(buf, J2K_CODESTREAM_MAGIC, 4) == 0) {
        magic_format = GRK_J2K_FMT;
        magic_s = ".j2k or .jpc or .j2c";
    } else {
    	*fmt = GRK_UNK_FMT;
    	return true;
    }


    if (magic_format == ext_format) {
        *fmt =  ext_format;
        return true;
    }

    s = fname + (strlen(fname) > 4 ? strlen(fname) - 4 : 0);
    spdlog::warn("The extension of this file is incorrect.\n Found {}. Should be {}\n", s, magic_s);
    *fmt = magic_format;
    return true;
}

const char* get_path_separator() {
#ifdef _WIN32
	return "\\";
#else
	return "/";
#endif
}

char * get_file_name(char *name)
{
	return strtok(name,".");
}

uint32_t get_num_images(char *imgdirpath)
{
    DIR *dir;
    struct dirent* content;
    uint32_t num_images = 0;

    /*Reading the input images from given input directory*/

    dir= opendir(imgdirpath);
    if(!dir) {
        spdlog::error("Could not open Folder {}\n",imgdirpath);
        return 0;
    }

    while((content=readdir(dir))!=nullptr) {
        if(strcmp(".",content->d_name)==0 || strcmp("..",content->d_name)==0 )
            continue;
        num_images++;
    }
    closedir(dir);
    return num_images;
}


char* actual_path(const char* outfile){
#ifdef _WIN32
	return (char*)outfile;
#else
	char *actualpath = realpath(outfile, NULL);
	if (actualpath != nullptr)
		return actualpath;
	return (char*)outfile;
#endif
}


}
