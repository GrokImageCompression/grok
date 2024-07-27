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
#pragma once

// #define GRK_FORCE_SIGNED_COMPRESS

/*
 * This must be included before any system headers,
 * since they are affected by macros defined therein
 */
#include "grk_config_private.h"
#include <memory.h>
#include <cstdlib>
#include <string>
#ifdef _MSC_VER
#define _USE_MATH_DEFINES
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
#define GRK_FTELL(stream) /* __int64 */ _ftelli64(stream)
#else
#define GRK_FSEEK(stream, offset, whence) fseek(stream, offset, whence)
#define GRK_FTELL(stream) ftell(stream)
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

#define CMS_NO_REGISTER_KEYWORD 1
#include "lcms2.h"

#include "grok_private.h"
#include "ILogger.h"
#include <Logger.h>
#include "simd.h"
#include "ThreadPool.hpp"
#include "packer.h"
#include "MinHeap.h"
#include "SequentialCache.h"
#include "SparseCache.h"
#include "CodeStreamLimits.h"
#include "geometry.h"
#include "MemManager.h"
#include "buffer.h"
#include "minpf_plugin_manager.h"
#include "plugin_interface.h"
#include "ICacheable.h"
#include "TileSet.h"
#include "GrkObjectWrapper.h"
#include "ChronoTimer.h"
#include "testing.h"
#include "MemStream.h"
#include "GrkMatrix.h"
#include "GrkImage.h"
#include "grk_exceptions.h"
#include "SparseBuffer.h"
#include "BitIO.h"
#include "BufferedStream.h"
#include "Profile.h"
#include "LengthCache.h"
#include "PLMarkerMgr.h"
#include "PLCache.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
#include "SOTMarker.h"
#include "CodeStream.h"
#include "CodingParams.h"
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
#include "PacketParser.h"
#include "ResSimple.h"
#include "Precinct.h"
#include "Subband.h"
#include "Resolution.h"
#include "BlockExec.h"
#include "ImageComponentFlow.h"
#include "Scheduler.h"
#include "SparseCanvas.h"
#include "TileComponentWindow.h"
#include "WaveletCommon.h"
#include "WaveletReverse.h"
#include "WaveletFwd.h"
#include "PacketIter.h"
#include "PacketManager.h"
#include "ImageComponentFlow.h"
#include "TileComponent.h"
#include "mct.h"
#include "TileProcessor.h"
#include "TileCache.h"
#include "T2Compress.h"
#include "T2Decompress.h"
#include "grk_intmath.h"
#include "plugin_bridge.h"
#include "RateInfo.h"
#include "T1Factory.h"
#include "DecompressScheduler.h"
#include "CompressScheduler.h"

#if(defined(__aarch64__) || defined(_M_ARM64)) && !defined(__ARM_FEATURE_SVE2) && \
	!defined(__ARM_FEATURE_SVE2)
#define HWY_DISABLED_TARGETS (HWY_SVE | HWY_SVE2 | HWY_SVE_256 | HWY_SVE2_128)
#endif
