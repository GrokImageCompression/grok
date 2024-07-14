#include "grk_includes.h"

namespace grk
{

TileSet::TileSet() : lastTileToDecompress_(0) {}
uint16_t TileSet::numScheduled(void)
{
   return (uint16_t)tilesToDecompress_.size();
}

void TileSet::init(grk_rect16 allTiles)
{
   assert(!allTiles.empty());
   allTiles_ = allTiles;
   schedule(allTiles_);
}
void TileSet::schedule(grk_rect16 tiles)
{
   tilesToDecompress_.clear();
   assert(!tiles.empty());
   for(uint16_t j = tiles.y0; j < tiles.y1; ++j)
   {
	  for(uint16_t i = tiles.x0; i < tiles.x1; ++i)
		 tilesToDecompress_.insert((uint16_t)(i + j * allTiles_.width()));
   }
   lastTileToDecompress_ = (uint16_t)((tiles.x1 - 1) + (tiles.y1 - 1) * allTiles_.width());
}
void TileSet::schedule(grk_pt16 tile)
{
   schedule(index(tile.x, tile.y));
}
void TileSet::schedule(uint16_t tile_index)
{
   tilesToDecompress_.clear();
   tilesToDecompress_.insert(tile_index);
   lastTileToDecompress_ = tile_index;
}
bool TileSet::isScheduled(uint16_t tile_index)
{
   return tilesToDecompress_.contains(tile_index);
}
bool TileSet::isScheduled(grk_pt16 tile)
{
   return isScheduled(index(tile));
}
uint16_t TileSet::getSingle(void)
{
   return *tilesToDecompress_.begin();
}
uint16_t TileSet::index(uint16_t x, uint16_t y)
{
   return (uint16_t)(x + y * allTiles_.width());
}
uint16_t TileSet::index(grk_pt16 tile)
{
   return (uint16_t)(tile.x + tile.y * allTiles_.width());
}
void TileSet::setComplete(uint16_t tile_index)
{
   if(isScheduled(tile_index))
   {
	  tilesDecompressed_.insert(tile_index);
	  // Logger::logger_.info("Complete %d", tile_index);
	  // if (allComplete())
	  //	Logger::logger_.info("Complete");
   }
}
bool TileSet::isComplete(uint16_t tile_index)
{
   return tilesDecompressed_.contains(tile_index);
}
bool TileSet::allComplete(void)
{
   return tilesDecompressed_.size() == tilesToDecompress_.size();
}
} // namespace grk
