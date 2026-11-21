#pragma once
#include <cstdint>
#include <vector>

namespace grk
{
class GrkImage;

struct MercuryAsyncDecode
{
  virtual ~MercuryAsyncDecode() = default;
  virtual GrkImage* imageForTile(uint16_t tileIndex) = 0;
  virtual void imagesForSwath(uint16_t tileIndex, uint32_t reducedY0, uint32_t reducedY1,
                              std::vector<GrkImage*>& out) = 0;
  virtual void waitForTile(uint16_t tileIndex) = 0;
  virtual void finish() = 0;
  virtual void fillComposite() = 0;
};
} // namespace grk
