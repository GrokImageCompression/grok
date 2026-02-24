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
#include "grk_includes.h"
#include "SparseBuffer.h"
#include "IStream.h"
#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct TileProcessor;
}
#include "CodeStream.h"

namespace grk
{
CodeStream::CodeStream(IStream* stream)
    : headerImage_(nullptr), stream_(stream), current_plugin_tile(nullptr)
{}
CodeStream::~CodeStream()
{
  grk_unref(headerImage_);
}
CodingParams* CodeStream::getCodingParams(void)
{
  return &cp_;
}
GrkImage* CodeStream::getHeaderImage(void)
{
  return headerImage_;
}
bool CodeStream::exec(std::vector<PROCEDURE_FUNC>& procs)
{
  bool result =
      std::all_of(procs.begin(), procs.end(), [](const PROCEDURE_FUNC& proc) { return proc(); });
  procs.clear();

  return result;
}
grk_plugin_tile* CodeStream::getCurrentPluginTile()
{
  return current_plugin_tile;
}
IStream* CodeStream::getStream()
{
  return stream_;
}

} // namespace grk
