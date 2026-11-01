#pragma once
#include <cstdint>

namespace grk
{
class GrkImage;

struct MercuryAsyncDecode
{
  virtual ~MercuryAsyncDecode() = default;
  virtual GrkImage* imageForTile(uint16_t tileIndex) = 0;
  // the tile image once allocated, complete or not
  virtual GrkImage* imageForSwath(uint16_t tileIndex) = 0;
  virtual void waitForTile(uint16_t tileIndex) = 0;
  virtual void finish() = 0;
};
} // namespace grk
