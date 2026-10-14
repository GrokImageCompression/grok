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
 */

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "TFSingleton.h"
#include "WaveletCommon.h"
#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "Logger.h"
#include "buffer.h"
#include "GrkObjectWrapper.h"
#include "FlowComponent.h"
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
#include "mct.h"
#include "simd.h"
#include "t1_common.h"
#include "BlockCoder.h"

#if defined(_WIN32)
#define GRK_MERCURY_SHIM_API __declspec(dllexport)
#else
#define GRK_MERCURY_SHIM_API __attribute__((visibility("default")))
#endif

namespace
{
const uint16_t MAX_BLOCK_SIDE = 64;
}

// samples are row-major sign-magnitude with T1_NMSEDEC_FRACBITS fraction bits
extern "C" GRK_MERCURY_SHIM_API int32_t mercury_grok_t1_encode(
    const int32_t* samples, int32_t num_cols, int32_t num_rows, uint32_t max_magnitude,
    int32_t orientation, int32_t compno, int32_t level, double stepsize, int32_t mct,
    uint16_t early_stop_slope, const uint8_t** coded_data, uint16_t* pass_rates, double* pass_distortions,
    uint8_t* pass_terminated, int32_t* numbps)
{
  using namespace grk;
  using namespace grk::t1;

  thread_local std::unique_ptr<BlockCoder> coder;
  thread_local std::vector<uint8_t> stream;
  if(!coder)
    coder = std::make_unique<BlockCoder>(true, MAX_BLOCK_SIDE, MAX_BLOCK_SIDE, 0);
  if(num_cols <= 0 || num_rows <= 0 || num_cols > MAX_BLOCK_SIDE || num_rows > MAX_BLOCK_SIDE)
    return -1;
  if(!coder->alloc((uint16_t)num_cols, (uint16_t)num_rows))
    return -1;
  std::memcpy(coder->getUncompressedData(), samples,
              (size_t)num_cols * (size_t)num_rows * sizeof(int32_t));

  // classic sizes the stream like PrecinctCodeblockStorage and pads it for the coder's data[-1]
  size_t streamBytes =
      std::max<size_t>((size_t)num_cols * (size_t)num_rows * sizeof(uint32_t),
                       PrecinctCodeblockStorage::minCompressedStreamBytes) +
      grk_cblk_enc_compressed_data_pad_left;
  if(stream.size() < streamBytes)
    stream.resize(streamBytes);
  stream[0] = 0;
  stream[1] = 0;

  cblk_enc block = {};
  block.x1 = (uint32_t)num_cols;
  block.y1 = (uint32_t)num_rows;
  block.data = stream.data() + grk_cblk_enc_compressed_data_pad_left;
  const double* mctNorms = mct ? Mct::get_norms_irrev() : nullptr;
  coder->compress_cblk(&block, max_magnitude, (uint8_t)orientation, (uint16_t)compno,
                       (uint8_t)level, 0, stepsize, 0, mctNorms, mct ? 3 : 0, true, early_stop_slope);

  for(uint8_t i = 0; i < block.numPassesTotal; ++i)
  {
    pass_rates[i] = block.passes[i].rate;
    pass_distortions[i] = block.passes[i].distortiondec;
    pass_terminated[i] = block.passes[i].term ? 1 : 0;
  }
  *coded_data = block.data;
  *numbps = block.numbps;
  return block.numPassesTotal;
}

// classic's irreversible step size of band `orientation` at resolution `resno`
extern "C" GRK_MERCURY_SHIM_API float mercury_grok_band_stepsize(uint8_t num_resolutions,
                                                                  uint8_t precision,
                                                                  uint8_t resno,
                                                                  uint8_t orientation)
{
  using namespace grk;
  Quantizer quantizer(false, 0);
  quantizer.generate((uint8_t)(num_resolutions - 1), precision, true, false);
  grk_stepsize steps[GRK_MAXBANDS];
  quantizer.pull(steps);
  uint32_t bandno = resno == 0 ? 0 : 3U * (resno - 1U) + orientation;
  uint32_t log2Gain = orientation == 0 ? 0 : (orientation == 3 ? 2 : 1);
  uint32_t numbps = precision + log2Gain;
  return (float)((1.0 + steps[bandno].mant / 2048.0) *
                 pow(2.0, (int32_t)(numbps - steps[bandno].expn)));
}
