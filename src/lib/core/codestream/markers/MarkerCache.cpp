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

#include "grk_includes.h"

namespace grk
{

MarkerCache::MarkerCache() : mainHeaderStart_(0), tileStreamStart_(0) {}

void MarkerCache::add(uint16_t id, uint64_t pos, uint16_t len)
{
  if(id == SOC)
    mainHeaderStart_ = pos;
  markers_.push_back(std::make_unique<Marker>(id, pos, len));
}

uint64_t MarkerCache::getTileStreamStart()
{
  return tileStreamStart_;
}

void MarkerCache::setTileStreamStart(uint64_t tileStreamStart)
{
  tileStreamStart_ = tileStreamStart;
}
void MarkerCache::dump(FILE* out)
{
  assert(out);

  // Print header
  fprintf(out, "Codestream index from main header: {\n");

  // Print main header positions
  fprintf(out, "\t Main header start position=%" PRIu64 "\n", mainHeaderStart_);
  fprintf(out, "\t Main header end position=%" PRIu64 "\n", tileStreamStart_);

  // Print marker list
  fprintf(out, "\t Marker list: {\n");
  for(const auto& marker : markers_)
    marker->dump(out);
  fprintf(out, "\t }\n");

  // Print footer
  fprintf(out, "}\n");
}

} // namespace grk