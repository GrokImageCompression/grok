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
 */

#pragma once

#define GROK_SKIP_POISON

#include "grk_includes.h"

#ifdef WIN32
	#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
	#include <x86intrin.h>
#endif


#if defined(__GNUC__)
#pragma GCC poison malloc calloc realloc free
#endif

namespace grk {

class CPUArch {
public:
	static bool AVX2();
	static bool AVX();
	static bool SSE4_1();
	static bool SSE3();
	static bool SSE2();
	static bool BMI1();
	static bool BMI2();

};

}

