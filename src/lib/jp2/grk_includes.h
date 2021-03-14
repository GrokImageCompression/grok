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

#pragma once


/*
 * This must be included before any system headers,
 * since they can react to macro defined there
 */
#include "grk_config_private.h"

/*
 ==========================================================
 Standard includes used by the library
 ==========================================================
 */
#include <memory.h>
#include <stdlib.h>
#include <string>
#ifdef _MSC_VER
#define _USE_MATH_DEFINES // for C++
#endif
#include <cmath>
#include <float.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>
#include <inttypes.h>
#include <climits>
#include <algorithm>
#include <sstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>

/*
 Use fseeko() and ftello() if they are available since they use
 'int64_t' rather than 'long'.  It is wrong to use fseeko() and
 ftello() only on systems with special LFS support since some systems
 (e.g. FreeBSD) support a 64-bit int64_t by default.
 */
#if defined(GROK_HAVE_FSEEKO) && !defined(fseek)
#  define fseek  fseeko
#  define ftell  ftello
#endif

#if defined(_WIN32)
#  define GROK_FSEEK(stream,offset,whence) _fseeki64(stream,/* __int64 */ offset,whence)
#  define GROK_FSTAT(fildes,stat_buff) _fstati64(fildes,/* struct _stati64 */ stat_buff)
#  define GROK_FTELL(stream) /* __int64 */ _ftelli64(stream)
#  define GROK_STAT_STRUCT_T struct _stati64
#  define GROK_STAT(path,stat_buff) _stati64(path,/* struct _stati64 */ stat_buff)
#else
#  define GROK_FSEEK(stream,offset,whence) fseek(stream,offset,whence)
#  define GROK_FSTAT(fildes,stat_buff) fstat(fildes,stat_buff)
#  define GROK_FTELL(stream) ftell(stream)
#  define GROK_STAT_STRUCT_T struct stat
#  define GROK_STAT(path,stat_buff) stat(path,stat_buff)
#endif

/*
 ==========================================================
 Grok interface
 ==========================================================
 */

#include "minpf_plugin_manager.h"
#include "plugin_interface.h"

/*
 ==========================================================
 Grok modules
 ==========================================================
 */

#if defined(__GNUC__)
#define GRK_RESTRICT __restrict__
#else
#define GRK_RESTRICT /* GRK_RESTRICT */
#endif


#ifdef __has_attribute
#if __has_attribute(no_sanitize)
#define GROK_NOSANITIZE(kind) __attribute__((no_sanitize(kind)))
#endif
#endif
#ifndef GROK_NOSANITIZE
#define GROK_NOSANITIZE(kind)
#endif

#include "simd.h"

#if defined(_MSC_VER)
#include <intrin.h>
static inline long grk_lrintf(float f)
{
#ifdef _M_X64
    return _mm_cvt_ss2si(_mm_load_ss(&f));
#elif defined(_M_IX86)
    int i;
    _asm{
        fld f
        fistp i
    };

    return i;
#else
    return (long)((f>0.0f) ? (f + 0.5f) : (f - 0.5f));
#endif
}
#else
static inline long grk_lrintf(float f) {
	return lrintf(f);
}
#endif

#if defined(_MSC_VER) && (_MSC_VER < 1400)
#define vsnprintf _vsnprintf
#endif

/* MSVC x86 is really bad at doing int64 = int32 * int32 on its own. Use intrinsic. */
#if defined(_MSC_VER) && (_MSC_VER >= 1400) && !defined(__INTEL_COMPILER) && defined(_M_IX86)
#	include <intrin.h>
#	pragma intrinsic(__emul)
#endif

#define GRK_UNUSED(x) (void)x
#include "ICacheable.h"
#include "GrkObjectWrapper.h"
#include "logger.h"
#include "testing.h"
#include "ThreadPool.hpp"
#include "MemStream.h"
#include "GrkMappedFile.h"
#include "util.h"
#include "MemManager.h"
#include "GrkMatrix.h"
#include "GrkImage.h"
#include "grk_exceptions.h"
#include "ChunkBuffer.h"
#include "BitIO.h"
#include "BufferedStream.h"
#include "Quantizer.h"
#include "Profile.h"
#include "LengthMarkers.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
#include "SOTMarker.h"
#include "CodeStream.h"
#include "CodeStreamCompress.h"
#include "CodeStreamDecompress.h"
#include "CodeStreamTranscode.h"
#include "FileFormat.h"
#include "FileFormatCompress.h"
#include "FileFormatDecompress.h"
#include "FileFormatTranscode.h"
#include "BitIO.h"
#include "TagTree.h"
#include "T1Structs.h"
#include "WaveletReverse.h"
#include "TileComponentWindowBuffer.h"
#include "PacketIter.h"
#include "SparseBuffer.h"
#include "TileComponent.h"
#include "TileProcessor.h"
#include "TileCache.h"
#include <WaveletFwd.h>
#include "t1_common.h"
#include "SparseBuffer.h"
#include "T2Compress.h"
#include "T2Decompress.h"
#include "mct.h"
#include "grk_intmath.h"
#include "plugin_bridge.h"
#include "RateControl.h"
#include "RateInfo.h"
#include "T1Interface.h"
#include "T1Factory.h"
#include "T1DecompressScheduler.h"
#include "T1CompressScheduler.h"
