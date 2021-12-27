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
#pragma once

//#define GRK_FORCE_SIGNED_COMPRESS

/*
 * This must be included before any system headers,
 * since they are affected by macros defined therein
 */
#include "grk_config_private.h"
#include <memory.h>
#include <cstdlib>
#include <string>
#ifdef _MSC_VER
#define _USE_MATH_DEFINES // for C++
#endif
#include <cmath>
#include <cfloat>
#include <time.h>
#include <cstdio>
#include <cstdarg>
#include <ctype.h>
#include <cassert>
#include <cinttypes>
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
#define fseek fseeko
#define ftell ftello
#endif
#if defined(_WIN32)
#define GRK_FSEEK(stream, offset, whence) _fseeki64(stream, /* __int64 */ offset, whence)
#define GRK_FSTAT(fildes, stat_buff) _fstati64(fildes, /* struct _stati64 */ stat_buff)
#define GRK_FTELL(stream) /* __int64 */ _ftelli64(stream)
#define GRK_STAT_STRUCT struct _stati64
#define GRK_STAT(path, stat_buff) _stati64(path, /* struct _stati64 */ stat_buff)
#else
#define GRK_FSEEK(stream, offset, whence) fseek(stream, offset, whence)
#define GRK_FSTAT(fildes, stat_buff) fstat(fildes, stat_buff)
#define GRK_FTELL(stream) ftell(stream)
#define GRK_STAT_STRUCT struct stat
#define GRK_STAT(path, stat_buff) stat(path, stat_buff)
#endif
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
#define GRK_UNUSED(x) (void)x

#include "simd.h"
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
#include <taskflow/taskflow.hpp>
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif
#include "SequentialCache.h"
#include "SparseCache.h"
#include "CodeStreamLimits.h"
#include "util.h"
#include "MemManager.h"
#include "minpf_plugin_manager.h"
#include "plugin_interface.h"
#include "ICacheable.h"
#include "GrkObjectWrapper.h"
#include "logger.h"
#include "testing.h"
#include "ThreadPool.hpp"
#include "MemStream.h"
#include "GrkMappedFile.h"
#include "GrkMatrix.h"
#include "GrkImage.h"
#include "grk_exceptions.h"
#include "SparseBuffer.h"
#include "BitIO.h"
#include "BufferedStream.h"
#include "Profile.h"
#include "LengthCache.h"
#include "PacketLengthMarkers.h"
#include "PacketLengthCache.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
#include "SOTMarker.h"
#include "CodeStream.h"
#include "CodeStreamCompress.h"
#include "CodeStreamDecompress.h"
#include "FileFormat.h"
#include "FileFormatCompress.h"
#include "FileFormatDecompress.h"
#include "BitIO.h"
#include "TagTree.h"
#include "t1_common.h"
#include "T1Interface.h"
#include "Codeblock.h"
#include "Precinct.h"
#include "Subband.h"
#include "Resolution.h"
#include "BlockExec.h"
#include "WaveletReverse.h"
#include "TileComponentWindowBuffer.h"
#include "PacketIter.h"
#include "PacketManager.h"
#include "SparseCanvas.h"
#include "TileComponent.h"
#include "TileProcessor.h"
#include "TileCache.h"
#include "WaveletFwd.h"
#include "T2Compress.h"
#include "T2Decompress.h"
#include "mct.h"
#include "grk_intmath.h"
#include "plugin_bridge.h"
#include "RateControl.h"
#include "RateInfo.h"
#include "T1Factory.h"
#include "T1DecompressScheduler.h"
#include "T1CompressScheduler.h"
