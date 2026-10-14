#pragma once

#include <cstdint>
#include <vector>

#include "mqc_base.h"

namespace mercury_native::t1enc
{

struct EncodedPasses
{
  uint16_t* rates;
  double* distortions;
  uint8_t* terminated;
  const uint8_t** bytes;
  int32_t* numbps;
};

// grok's part-1 block coder for code-block style 0
class BlockEncoder
{
public:
  static double getnorm(uint32_t level, uint8_t orientation, bool reversible);
  int32_t encode(const int32_t* samples, uint16_t width, uint16_t height, uint32_t maxMagnitude,
                 uint8_t orientation, uint16_t compno, uint8_t level, double stepsize,
                 const double* mctNorms, uint16_t mctNumcomps, const EncodedPasses& out);

private:
  void alloc(uint16_t width, uint16_t height);
  void initFlags(void);
  uint16_t getFlagsStride(void);
  uint16_t getFlagsHeight(void);

  double getnorm_53(uint32_t level, uint8_t orientation);
  double getnorm_97(uint32_t level, uint8_t orientation);
  double getwmsedec(int32_t nmsedec, uint16_t compno, uint32_t level, uint8_t orientation,
                    int8_t bpno, uint32_t qmfbid, double stepsize, const double* mct_norms,
                    uint32_t mct_numcomps);
  // each returns the pass's distortion decrease in grok's nmsedec units
  int32_t enc_clnpass(int8_t bpno);
  int32_t enc_sigpass(int8_t bpno);
  int32_t enc_refpass(int8_t bpno);

  mqcoder coder = {};
  uint16_t w_ = 0;
  uint16_t stride_ = 0;
  uint16_t h_ = 0;
  const int32_t* uncompressedData_ = nullptr;
  std::vector<grk_flag> flagStorage_;
  grk_flag* flags_ = nullptr;
  uint32_t flagsLen_ = 0;
  std::vector<uint8_t> stream_;
};

} // namespace mercury_native::t1enc
