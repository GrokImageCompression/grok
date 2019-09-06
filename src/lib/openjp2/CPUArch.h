/*
 *    Copyright (C) 2016-2019 Grok Image Compression Inc.
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
 */

#pragma once

#define GROK_SKIP_POISON

#include "grok_includes.h"

#ifdef WIN32
	#include <intrin.h>
#else
	#include <x86intrin.h>
	#ifdef AVX2_FOUND
		#ifndef __AVX2__
			#define __AVX2__
		#endif
		#ifndef __SSE4_1__
			#define __SSE4_1__
		#endif
	#elif defined(AVX_FOUND)
		#ifndef __AVX__
			#define __AVX__
		#endif
	#elif defined(SSE4_1_FOUND)
		#ifndef __SSE4_1__
			#define __SSE4_1__
		#endif
	#elif defined(SSE3_FOUND)
		#ifndef __SSE3__
			#define __SSE3__
		#endif
	#endif
#endif

#if defined(__GNUC__)
#pragma GCC poison malloc calloc realloc free
#endif

namespace grk {

class CPUArch {
public:
	bool AVX2();
	bool AVX();
	bool SSE4_1();
	bool SSE3();
	bool BMI1();
	bool BMI2();

};

}

