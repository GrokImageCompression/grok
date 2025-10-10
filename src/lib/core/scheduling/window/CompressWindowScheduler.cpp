/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

#include "grk_includes.h"

namespace grk
{

CompressWindowScheduler::CompressWindowScheduler(Tile* tile, bool needsRateControl,
                                                 TileCodingParams* tcp, const double* mct_norms,
                                                 uint16_t mct_numcomps)
    : WindowScheduler(tile->numcomps_), tile_(tile), needsRateControl_(needsRateControl),
      blockCount_(-1), tcp_(tcp), mct_norms_(mct_norms), mct_numcomps_(mct_numcomps)

{}

} // namespace grk
