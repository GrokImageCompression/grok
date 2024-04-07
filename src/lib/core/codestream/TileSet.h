#pragma once

#include <set>

namespace grk
{

class TileSet
{
 public:
   TileSet();
   virtual ~TileSet() = default;
   uint16_t numScheduled(void);
   void init(grk_rect16 allTiles);
   void schedule(grk_rect16 tiles);
   void schedule(grk_pt16 tile);
   void schedule(uint16_t tileIndex);
   bool isScheduled(uint16_t tileIndex);
   bool isScheduled(grk_pt16 tile);
   void setComplete(uint16_t tileIndex);
   bool isComplete(uint16_t tileIndex);
   bool allComplete(void);
   uint16_t getSingle(void);

 private:
   uint16_t index(uint16_t x, uint16_t y);
   uint16_t index(grk_pt16 tile);
   std::set<uint16_t> tilesToDecompress_;
   std::set<uint16_t> tilesDecompressed_;
   grk_rect16 allTiles_;
   uint16_t lastTileToDecompress_;
};

} // namespace grk
