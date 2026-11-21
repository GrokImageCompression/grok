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

#include <cstdint>

#include "spdlog/spdlog.h"

#include "GrkPerLayerValuesTest.h"
#include "Codeblock.h"

namespace grk
{

static const uint16_t writtenLayer = 60000;
static const uint8_t writtenPasses = 7;

int GrkPerLayerValuesTest::main([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
  int failures = 0;
  t1::PerLayerValues<uint8_t> passesByLayer;

  if(passesByLayer.size() != 0)
  {
    spdlog::error("fresh PerLayerValues holds {} entries", passesByLayer.size());
    failures++;
  }
  if(passesByLayer.get(0) != 0 || passesByLayer.get(writtenLayer) != 0)
  {
    spdlog::error("fresh PerLayerValues does not read as zero");
    failures++;
  }

  passesByLayer.set(writtenLayer, writtenPasses);
  if(passesByLayer.size() != (size_t)writtenLayer + 1)
  {
    spdlog::error("write at layer {} grew storage to {} entries", writtenLayer,
                  passesByLayer.size());
    failures++;
  }
  if(passesByLayer.get(writtenLayer) != writtenPasses)
  {
    spdlog::error("layer {} reads {} instead of {}", writtenLayer, passesByLayer.get(writtenLayer),
                  writtenPasses);
    failures++;
  }
  if(passesByLayer.get(0) != 0 || passesByLayer.get(writtenLayer - 1) != 0)
  {
    spdlog::error("unwritten layer below layer {} does not read as zero", writtenLayer);
    failures++;
  }

  passesByLayer.increment(0, writtenPasses);
  if(passesByLayer.get(0) != writtenPasses)
  {
    spdlog::error("increment of unwritten layer 0 reads {}", passesByLayer.get(0));
    failures++;
  }

  passesByLayer.clear();
  if(passesByLayer.size() != 0 || passesByLayer.get(writtenLayer) != 0)
  {
    spdlog::error("cleared PerLayerValues holds {} entries", passesByLayer.size());
    failures++;
  }

  return failures ? 1 : 0;
}

} // namespace grk
