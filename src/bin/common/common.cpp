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
			fprintf(stdout, "[WARNING] Decode region must be specified by exactly four coordinates. Ignoring specified region\n");
		return EXIT_FAILURE;

	}

	// don't allow negative values
	if ((values[0] < 0 ||
		values[1] < 0 ||
		values[2] < 0 ||
		values[3] < 0)) {
		fprintf(stdout, "[WARNING] Decode region cannot contain negative values. Ignoring specified region (%d,%d,%d,%d).\n",
			values[0], values[1], values[2], values[3]);
		return EXIT_FAILURE;
	}
	else {
		*DA_x0 = values[0];
		*DA_y0 = values[1];
		*DA_x1 = values[2];
		*DA_y1 = values[3];
		return EXIT_SUCCESS;
	}
}


}
