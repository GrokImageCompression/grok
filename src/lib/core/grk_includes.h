/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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

#include "EnvVarManager.h"
#include "flag_query.h"
#include "grk_exceptions.h"
#include "ILogger.h"
#include <Logger.h>
#include "IniParser.h"
#include "SimpleXmlParser.h"
#include "simd.h"
#include "ExecSingleton.h"
#include "packer.h"
#include "MinHeap.h"
#include "SequentialCache.h"
#include "SparseCache.h"
#include "CodeStreamLimits.h"
#include "geometry.h"
#include "MemManager.h"
#include "buffer.h"
#include "ChunkBuffer.h"
#include "minpf_plugin_manager.h"
#include "plugin_interface.h"
#include "TileWindow.h"
#include "GrkObjectWrapper.h"
#include "ChronoTimer.h"
#include "testing.h"
#include "MappedFile.h"
#include "GrkMatrix.h"
#include "Quantizer.h"
#include "SparseBuffer.h"
#include "ResSimple.h"
#include "SparseCanvas.h"
#include "intmath.h"
#include "ImageComponentFlow.h"
#include "MarkerCache.h"
#include "SlabPool.h"
#include "StreamIO.h"
#include "IStream.h"
#include "MemAdvisor.h"

#include "FetchCommon.h"
#include "TPFetchSeq.h"

#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "ICompressor.h"
#include "IDecompressor.h"

#include "MemStream.h"

#include "StreamGenerator.h"
#include "Profile.h"
#include "MarkerParser.h"
#include "Codec.h"

#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct TileProcessor;
struct TileProcessorCompress;
} // namespace grk
#include "PacketParser.h"
#include "PacketCache.h"
#include "CodingParams.h"
#include "CodeStream.h"
#include "PacketIter.h"

#include "PacketLengthCache.h"
#include "TLMMarker.h"
#include "ICoder.h"
#include "CoderPool.h"
#include "FileFormatJP2Family.h"
#include "FileFormatJP2Compress.h"
#include "FileFormatJP2Decompress.h"
#include "FileFormatMJ2.h"
#include "FileFormatMJ2Compress.h"
#include "FileFormatMJ2Decompress.h"

#include "BitIO.h"
#include "TagTree.h"

#include "Codeblock.h"
#include "CodeblockCompress.h"
#include "CodeblockDecompress.h"

#include "Precinct.h"
#include "Subband.h"
#include "Resolution.h"
#include "BlockExec.h"
#include "WindowScheduler.h"
#include "WholeTileScheduler.h"

#include "canvas/tile/TileComponentWindow.h"
#include "WaveletCommon.h"
#include "WaveletReverse.h"
#include "WaveletFwd.h"

#include "PacketManager.h"
#include "canvas/tile/TileComponent.h"
#include "canvas/tile/Tile.h"
#include "mct.h"

#include "TileProcessor.h"
#include "TileProcessorCompress.h"
#include "SOTMarker.h"
#include "CodeStreamCompress.h"
#include "TileCache.h"
#include "TileCompletion.h"
#include "CodeStreamDecompress.h"
#include "T2Compress.h"
#include "T2Decompress.h"
#include "plugin_bridge.h"
#include "RateControl.h"
#include "RateInfo.h"
#include "CoderFactory.h"
#include "DecompressScheduler.h"
#include "CompressScheduler.h"
#include "DecompressWindowScheduler.h"
#include "CompressWindowScheduler.h"

#if (defined(__aarch64__) || defined(_M_ARM64)) && !defined(__ARM_FEATURE_SVE2) && \
    !defined(__ARM_FEATURE_SVE2)
#define HWY_DISABLED_TARGETS (HWY_SVE | HWY_SVE2 | HWY_SVE_256 | HWY_SVE2_128)
#endif
