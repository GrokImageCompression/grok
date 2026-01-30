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

#pragma once

#include "t1_common.h"

namespace grk::t1
{

class BlockCoder
{
public:
  BlockCoder(bool isCompressor, uint16_t maxCblkW, uint16_t maxCblkH, uint32_t cacheStrategy);
  ~BlockCoder();

  void print(void);
  bool alloc(uint16_t w, uint16_t h);

  void code_block_enc_deallocate(cblk_enc* p_code_block);
  double compress_cblk(cblk_enc* cblk, uint32_t max, uint8_t orientation, uint16_t compno,
                       uint8_t level, uint8_t qmfbid, double stepsize, uint32_t cblksty,
                       const double* mct_norms, uint16_t mct_numcomps, bool doRateControl);
  static double getnorm(uint32_t level, uint8_t orientation, bool reversible);

  bool decompress_cblk(CodeblockDecompress* cblk, uint8_t orientation, uint32_t cblksty);
  int32_t* getUncompressedData(void);
  void decompressInitOrientation(uint8_t orientation);
  void decompressInitSegment(uint8_t type, uint8_t** buffers, uint32_t* buffer_lengths,
                             uint16_t num_buffers);
  void decompressUpdateSegment(uint8_t** buffers, uint32_t* buffer_lengths, uint16_t num_buffers);
  bool decompressPass(uint8_t passno, uint8_t passtype, uint8_t numBpsToDecompress, uint8_t type,
                      uint32_t cblksty);
  void decompressFinish(uint32_t cblksty, bool finalLayer);
  void decompressBackup(void);
  void decompressRestore(uint8_t* passno, uint8_t* passtype, uint8_t* numBpsToDecompress);
  void setFinalLayer(bool isFinal);
  static bool cacheAll(uint32_t strategy);

private:
  void initFlags(void);
  uint16_t getFlagsStride(void);
  uint16_t getFlagsHeight(void);

  uint32_t cacheStrategy_;
  mqcoder coder;

  /**
   * @brief cached block width
   */
  uint16_t w_;
  /**
   * @brief cached block stride
   */
  uint8_t stride_;
  /**
   * @brief cached block height
   */
  uint16_t h_;

  /**
   * @brief uncompressed data buffer
   */
  Buffer2dAligned32 uncompressedBuf_;
  /**
   * @brief pointer to @ref uncompressedBuf_ data
   */
  int32_t* uncompressedData_;

  /** Flags used by decompressor and compressor.
   * Such that flags[1+0] is for state of col=0,row=0..3,
   flags[1+1] for col=1, row=0..3, flags[1+flagsStride] for col=0,row=4..7, ...
   This array avoids too much cache trashing when processing by 4 vertical samples
   as done in the various decoding steps. */
  grk_flag* flags_;
  uint32_t flagsLen_;
  bool compressor;

  void checkSegSym(int32_t cblksty);

  template<uint16_t w, uint16_t h, bool vsc>
  void dec_clnpass(int8_t bpno);
  void dec_clnpass(int8_t bpno, int32_t cblksty);
  template<uint16_t w, uint16_t h, bool vsc>
  void dec_clnpass_diff(int8_t bpno, uint8_t passno, uint8_t passtype);
  void dec_clnpass_diff(int8_t bpno, uint8_t passno, uint8_t passtype, int32_t cblksty);
  template<uint16_t w, uint16_t h, bool vsc>
  void dec_clnpass_diff_final(int8_t bpno, uint8_t passno, uint8_t passtype);
  void dec_clnpass_diff_final(int8_t bpno, uint8_t passno, uint8_t passtype, int32_t cblksty);

  template<uint16_t w, uint16_t h, bool vsc>
  void dec_sigpass(int8_t bpno);
  void dec_sigpass(int8_t bpno, int32_t cblksty);
  template<uint16_t w, uint16_t h, bool vsc>
  void dec_sigpass_diff(int8_t bpno, uint8_t passno, uint8_t passtype);
  void dec_sigpass_diff(int8_t bpno, uint8_t passno, uint8_t passtype, int32_t cblksty);
  template<uint16_t w, uint16_t h, bool vsc>
  void dec_sigpass_diff_final(int8_t bpno, uint8_t passno, uint8_t passtype);
  void dec_sigpass_diff_final(int8_t bpno, uint8_t passno, uint8_t passtype, int32_t cblksty);

  template<uint16_t w, uint16_t h>
  void dec_refpass(int8_t bpno);
  void dec_refpass(int8_t bpno);
  template<uint16_t w, uint16_t h>
  void dec_refpass_diff(int8_t bpno, uint8_t passno, uint8_t passtype);
  void dec_refpass_diff(int8_t bpno, uint8_t passno, uint8_t passtype);
  template<uint16_t w, uint16_t h>
  void dec_refpass_diff_final(int8_t bpno, uint8_t passno, uint8_t passtype);
  void dec_refpass_diff_final(int8_t bpno, uint8_t passno, uint8_t passtype);

  void dec_sigpass_raw(int8_t bpno, int32_t cblksty);
  void dec_refpass_raw(int8_t bpno);
  inline void dec_refpass_step_raw(grk_flag* flagsPtr, int32_t* datap, int32_t poshalf,
                                   uint32_t ci);
  inline void dec_sigpass_step_raw(grk_flag* flagsPtr, int32_t* datap, int32_t oneplushalf,
                                   uint32_t vsc, uint32_t ci);

  void enc_clnpass(int8_t bpno, int32_t* nmsedec, uint32_t cblksty);
  void enc_sigpass(int8_t bpno, int32_t* nmsedec, uint8_t type, uint32_t cblksty);
  void enc_refpass(int8_t bpno, int32_t* nmsedec, uint8_t type);
  int enc_is_term_pass(cblk_enc* cblk, uint32_t cblksty, int8_t bpno, uint32_t passtype);
  void code_block_enc_allocate(cblk_enc* p_code_block);
  /**
   Get the norm of a wavelet function of a subband at a specified level for the reversible 5-3
   DWT.
   @param level Level of the wavelet function
   @param orientation Band of the wavelet function
   @return the norm of the wavelet function
   */
  double getnorm_53(uint32_t level, uint8_t orientation);
  /**
   Get the norm of a wavelet function of a subband at a specified level for the irreversible 9-7
   DWT
   @param level Level of the wavelet function
   @param orientation Band of the wavelet function
   @return the norm of the 9-7 wavelet
   */
  double getnorm_97(uint32_t level, uint8_t orientation);

  double getwmsedec(int32_t nmsedec, uint16_t compno, uint32_t level, uint8_t orientation,
                    int8_t bpno, uint32_t qmfbid, double stepsize, const double* mct_norms,
                    uint32_t mct_numcomps);
};

} // namespace grk::t1
