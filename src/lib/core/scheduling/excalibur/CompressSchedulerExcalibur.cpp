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

#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "Logger.h"
#include "buffer.h"
#include "GrkObjectWrapper.h"
#include "SparseCanvas.h"
#include "ImageComponentFlow.h"
#include "IStream.h"
#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct ITileProcessor;
}
#include "CodingParams.h"

#include "ICoder.h"
#include "CoderPool.h"
#include "BitIO.h"

#include "TagTree.h"

#include "CodeblockCompress.h"

#include "CodeblockDecompress.h"
#include "Precinct.h"
#include "Subband.h"
#include "Resolution.h"

#include "TileComponentWindow.h"
#include "canvas/tile/Tile.h"
#include "CompressSchedulerExcalibur.h"

namespace grk
{

CompressSchedulerExcalibur::CompressSchedulerExcalibur(Tile* tile, bool needsRateControl,
                                                 TileCodingParams* tcp, const double* mct_norms,
                                                 uint16_t mct_numcomps)
    : SchedulerExcalibur(tile->numcomps_), tile_(tile), needsRateControl_(needsRateControl),
      blockCount_(-1), tcp_(tcp), mct_norms_(mct_norms), mct_numcomps_(mct_numcomps)

{}

} // namespace grk
