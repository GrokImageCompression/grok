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

#include "TileWindow.h"

namespace grk
{

TileWindow::TileWindow() {}

void TileWindow::init(Rect16 allTiles)
{
  assert(!allTiles.empty());
  allTiles_ = allTiles;
  slate(allTiles_);
}
uint16_t TileWindow::getTotalNumTiles(void)
{
  return (uint16_t)allTiles_.area();
}
void TileWindow::slate(Rect16 tiles)
{
  assert(!allTiles_.intersection(tiles).empty());
  assert(!tiles.empty());
  tilesSlatedForDecompression_.clear();
  for(uint16_t j = tiles.y0; j < tiles.y1; ++j)
  {
    for(uint16_t i = tiles.x0; i < tiles.x1; ++i)
      tilesSlatedForDecompression_.insert((uint16_t)(i + j * allTiles_.width()));
  }
  slatedTiles_ = tiles;
}
void TileWindow::slate(Point16 tile)
{
  slate(Rect16(tile.x, tile.y, tile.x + 1, tile.y + 1));
}
void TileWindow::slate(uint16_t tile_index)
{
  slate(Point16(tile_index % allTiles_.width(), tile_index / allTiles_.width()));
}

uint16_t TileWindow::index(Point16 tile)
{
  return (uint16_t)(tile.x + tile.y * allTiles_.width());
}

bool TileWindow::isSlated(uint16_t tile_index)
{
  return tilesSlatedForDecompression_.find(tile_index) != tilesSlatedForDecompression_.end();
}

std::set<uint16_t>& TileWindow::getSlatedTiles(void)
{
  return tilesSlatedForDecompression_;
}

Rect16 TileWindow::getSlatedTileRect(void)
{
  return slatedTiles_;
}

} // namespace grk
