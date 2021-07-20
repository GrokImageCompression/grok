// Copyright (c) 2019 - 2021, Osamu Watanabe
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
//    modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
//    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
//    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include "j2kmarkers.hpp"
#include <cstring>
#include <functional>

/********************************************************************************
 * j2k_region
 *******************************************************************************/
class j2k_region {
 public:
  // top-left coordinate (inclusive) of a region in the reference grid
  element_siz pos0;
  // bottom-right coordinate (exclusive) of a region in the reference grid
  element_siz pos1;
  // return top-left coordinate (inclusive)
  element_siz get_pos0() const { return pos0; }
  // return bottom-right coordinate (exclusive)
  element_siz get_pos1() const { return pos1; }
  // get size of a region
  void get_size(element_siz &out) const {
    out.x = pos1.x - pos0.x;
    out.y = pos1.y - pos0.y;
  }
  // set top-left coordinate (inclusive)
  void set_pos0(element_siz in) { pos0 = in; }
  // set bottom-right coordinate (exclusive)
  void set_pos1(element_siz in) { pos1 = in; }
  j2k_region() = default;
  j2k_region(element_siz p0, element_siz p1) : pos0(p0), pos1(p1) {}
};

/********************************************************************************
 * j2k_codeblock
 *******************************************************************************/
class j2k_codeblock : public j2k_region {
 public:
  const element_siz size;

 private:
  const uint32_t index;
  const uint8_t band;
  const uint8_t M_b;
  std::unique_ptr<uint8_t[]> compressed_data;
  uint8_t *current_address;
  std::unique_ptr<uint8_t[]> block_states;

 public:
  const uint8_t R_b;
  const uint8_t transformation;
  const float stepsize;
  const uint32_t band_stride;
  const uint16_t num_layers;
  std::unique_ptr<int32_t[]> sample_buf;
  int16_t *const i_samples;
  float *const f_samples;
  uint32_t length;
  uint16_t Cmodes;
  uint8_t num_passes;
  uint8_t num_ZBP;
  uint8_t fast_skip_passes;
  uint32_t Lblock;
  // length of a coding pass in byte
  std::vector<uint32_t> pass_length;
  // index of the coding-pass from which layer starts
  std::unique_ptr<uint8_t[]> layer_start;
  // number of coding-passes included in a layer
  std::unique_ptr<uint8_t[]> layer_passes;
  bool already_included;

  j2k_codeblock(const uint32_t &idx, uint8_t orientation, uint8_t M_b, uint8_t R_b, uint8_t transformation,
                float stepsize, uint32_t band_stride, int16_t *ibuf, float *fbuf, uint32_t offset,
                const uint16_t &numlayers, const uint8_t &codeblock_style, const element_siz &p0,
                const element_siz &p1, const element_siz &s);
  void modify_state(const std::function<void(uint8_t &, uint8_t)> &callback, uint8_t val, int16_t j1,
                    int16_t j2) {
    callback(block_states[(j1 + 1) * (size.x + 2) + (j2 + 1)], val);
  }
  uint8_t get_state(const std::function<uint8_t(uint8_t &)> &callback, int16_t j1, int16_t j2) const {
    return callback(block_states[(j1 + 1) * (size.x + 2) + (j2 + 1)]);
  }
  // DEBUG FUNCTION, SOON BE DELETED
  uint8_t get_orientation() const { return band; }
  uint8_t get_context_label_sig(const uint16_t &j1, const uint16_t &j2) const;
  uint8_t get_signLUT_index(const uint16_t &j1, const uint16_t &j2) const;
  uint8_t get_Mb() const;
  uint8_t *get_compressed_data();
  void set_compressed_data(uint8_t *buf, uint16_t size);
  void create_compressed_buffer(buf_chain *tile_buf, uint16_t buf_limit, const uint16_t &layer);
  float *get_fsample_addr(const int16_t &j1, const int16_t &j2) const;
  void update_sample(const uint8_t &symbol, const uint8_t &p, const uint16_t &j1, const uint16_t &j2) const;
  void update_sign(const int8_t &val, const uint16_t &j1, const uint16_t &j2) const;
  uint8_t get_sign(const uint16_t &j1, const uint16_t &j2) const;
  void set_MagSgn_and_sigma(uint32_t &or_val);
  void calc_mbr(uint8_t &mbr, uint16_t i, uint16_t j, uint32_t mbr_info, uint8_t causal_cond) const;
};

/********************************************************************************
 * j2k_subband
 *******************************************************************************/
class j2k_subband : public j2k_region {
 public:
  uint8_t orientation;
  uint8_t transformation;
  uint8_t R_b;
  uint8_t epsilon_b;
  uint16_t mantissa_b;
  uint8_t M_b;
  float delta;
  float nominal_range;
  int16_t *i_samples;
  float *f_samples;

  // j2k_subband();
  j2k_subband(element_siz p0, element_siz p1, uint8_t orientation, uint8_t transformation, uint8_t R_b,
              uint8_t epsilon_b, uint16_t mantissa_b, uint8_t M_b, float delta, float nominal_range,
              int16_t *ibuf, float *fbuf);
  ~j2k_subband();
  void quantize();
  float get_normalized_step_size();
  // DEBUG FUNCTION, SOON BE DELETED
  void show() {
    printf("---- band = %d ----\n", orientation);
    for (int i = 0; i < pos1.y - pos0.y; ++i) {
      for (int j = 0; j < pos1.x - pos0.x; ++j) {
        printf("%3d ", i_samples[j + i * (pos1.x - pos0.x)]);
      }
      printf("\n");
    }
    printf("-------------------\n");
  }
};

/********************************************************************************
 * j2k_precinct_subband
 *******************************************************************************/
class j2k_precinct_subband : public j2k_region {
 private:
  const uint8_t orientation;
  std::unique_ptr<tagtree> inclusion_info;
  std::unique_ptr<tagtree> ZBP_info;
  std::unique_ptr<std::unique_ptr<j2k_codeblock>[]> codeblocks;

 public:
  uint32_t num_codeblock_x;
  uint32_t num_codeblock_y;
  j2k_precinct_subband(uint8_t orientation, uint8_t M_b, uint8_t R_b, uint8_t transformation,
                       float stepsize, int16_t *ibuf, float *fbuf, const element_siz &bp0,
                       const element_siz &bp1, const element_siz &p0, const element_siz &p1,
                       const uint16_t &num_layers, const element_siz &codeblock_size,
                       const uint8_t &Cmodes);
  //~j2k_precinct_subband();
  tagtree_node *get_inclusion_node(uint32_t i);
  tagtree_node *get_ZBP_node(uint32_t i);
  j2k_codeblock *access_codeblock(uint32_t i);
  void parse_packet_header(buf_chain *packet_header, uint16_t layer_idx, uint16_t Ccap15);
  void generate_packet_header(packet_header_writer &header, uint16_t layer_idx);
};

/********************************************************************************
 * j2k_precinct
 *******************************************************************************/
class j2k_precinct : public j2k_region {
 private:
  // index of this precinct
  const uint32_t index;
  // index of resolution level to which this precinct belongs
  const uint8_t resolution;
  // number of subbands in this precinct
  const uint8_t num_bands;
  // length which includes packet header and body, used only for encoder
  int32_t length;
  // container for a subband within this precinct which includes codeblocks
  std::unique_ptr<std::unique_ptr<j2k_precinct_subband>[]> pband;

 public:
  // buffer for generated packet header: only for encoding
  std::unique_ptr<uint8_t[]> packet_header;
  // length of packet header
  uint32_t packet_header_length;

 public:
  j2k_precinct(const uint8_t &r, const uint32_t &idx, const element_siz &p0, const element_siz &p1,
               const std::unique_ptr<std::unique_ptr<j2k_subband>[]> &subband, const uint16_t &num_layers,
               const element_siz &codeblock_size, const uint8_t &Cmodes);
  //~j2k_precinct();

  j2k_precinct_subband *access_pband(uint8_t b);
  void set_length(int32_t len) { length = len; }
  int32_t get_length() const { return length; }
};

/********************************************************************************
 * j2c_packet
 *******************************************************************************/
class j2c_packet {
 public:
  uint16_t layer;
  uint8_t resolution;
  uint16_t component;
  uint32_t precinct;
  buf_chain *header;
  buf_chain *body;
  // only for encoder
  std::unique_ptr<uint8_t[]> buf;
  int32_t length;

  j2c_packet()
      : layer(0), resolution(0), component(0), precinct(0), header(nullptr), body(nullptr), length(0){};
  // constructor for decoding
  j2c_packet(const uint16_t l, const uint8_t r, const uint16_t c, const uint32_t p,
             buf_chain *const h = nullptr, buf_chain *const bo = nullptr)
      : layer(l), resolution(r), component(c), precinct(p), header(h), body(bo), length(0) {}
  // constructor for encoding
  j2c_packet(uint16_t l, uint8_t r, uint16_t c, uint32_t p, j2k_precinct *cp, uint8_t num_bands);
};

/********************************************************************************
 * j2k_resolution
 *******************************************************************************/
class j2k_resolution : public j2k_region {
 private:
  // resolution level
  const uint8_t index;
  // array of unique pointer to precincts
  std::unique_ptr<std::unique_ptr<j2k_precinct>[]> precincts;
  // unique pointer to subbands
  std::unique_ptr<std::unique_ptr<j2k_subband>[]> subbands;
  // subband nominal ranges
  float child_ranges[4];

 public:
  // number of subbands
  const uint8_t num_bands;
  // number of precincts wide
  const uint32_t npw;
  // number of precincts height
  const uint32_t nph;
  // a resolution is empty if it has no precincts
  const bool is_empty;
  //
  uint8_t normalizing_upshift;
  int16_t *i_samples;
  // int32_t *iw_samples;
  float *f_samples;
  j2k_resolution(const uint8_t &r, const element_siz &p0, const element_siz &p1, const uint32_t &npw,
                 const uint32_t &nph);
  ~j2k_resolution();
  uint8_t get_index() const { return index; }
  void create_subbands(element_siz &p0, element_siz &p1, uint8_t NL, uint8_t transformation,
                       std::vector<uint8_t> &exponents, std::vector<uint16_t> &mantissas,
                       uint8_t num_guard_bits, uint8_t qstyle, uint8_t bitdepth);
  void create_precincts(element_siz PP, uint16_t num_layers, element_siz codeblock_size, uint8_t Cmodes);

  // void create_precinct_bands(uint16_t num_layers, element_siz codeblock_size, uint8_t Cmodes);
  j2k_precinct *access_precinct(uint32_t p);
  j2k_subband *access_subband(uint8_t b);
  void set_nominal_ranges(const float *ranges) {
    child_ranges[0] = ranges[0];
    child_ranges[1] = ranges[1];
    child_ranges[2] = ranges[2];
    child_ranges[3] = ranges[3];
  }
  void scale();
};

/********************************************************************************
 * j2k_tile_part
 *******************************************************************************/
class j2k_tile_part {
 private:
  // tile index to which this tile-part belongs
  uint16_t tile_index;
  // tile-part index
  uint8_t tile_part_index;
  // pointer to tile-part buffer
  uint8_t *body;
  // length of tile-part
  uint32_t length;

 public:
  // pointer to tile-part header
  std::unique_ptr<j2k_tilepart_header> header;
  explicit j2k_tile_part(uint16_t num_components);
  void set_SOT(SOT_marker &tmpSOT);
  int read(j2c_src_memory &);
  uint16_t get_tile_index() const;
  uint8_t get_tile_part_index() const;
  uint32_t get_length() const;
  uint8_t *get_buf();
  void set_tile_index(uint16_t t);
  void set_tile_part_index(uint8_t tp);
  void create_buffer(int32_t len);
};

/********************************************************************************
 * j2k_tile_base
 *******************************************************************************/
class j2k_tile_base : public j2k_region {
 public:
  // number of DWT decomposition levels
  uint8_t NL;
  // resolution reduction
  uint8_t reduce_NL;
  // code-block width and height
  element_siz codeblock_size;
  // codeblock style (Table A.19)
  uint8_t Cmodes;
  // DWT type (Table A.20), 0:9x7, 1:5x3
  uint8_t transformation;
  // precinct width and height as exponents of the power of 2
  std::vector<element_siz> precinct_size;
  // quantization style (Table A.28)
  uint8_t quantization_style;
  // exponents of step sizes
  std::vector<uint8_t> exponents;
  // mantissas of step sizes
  std::vector<uint16_t> mantissas;
  // number of guard bits
  uint8_t num_guard_bits;
  j2k_tile_base() : reduce_NL(0) {}
};

/********************************************************************************
 * j2k_tile_component
 *******************************************************************************/
class j2k_tile_component : public j2k_tile_base {
 private:
  // component index
  uint16_t index;
  // pointer to sample buffer (integer)
  int32_t *samples;
  // pointer to sample buffer (float)
  float *fsamples;
  // shift value for ROI
  uint8_t ROIshift;
  // pointer to instances of resolution class
  std::unique_ptr<std::unique_ptr<j2k_resolution>[]> resolution;
  // set members related to COC marker
  void setCOCparams(COC_marker *COC);
  // set members related to QCC marker
  void setQCCparams(QCC_marker *QCC);
  // set ROIshift from RGN marker
  void setRGNparams(RGN_marker *RGN);

 public:
  // component bit-depth
  uint8_t bitdepth;
  // default constructor
  j2k_tile_component();
  // destructor
  ~j2k_tile_component();
  // initialization of coordinates and parameters defined in tile-part markers
  void init(j2k_main_header *hdr, j2k_tilepart_header *tphdr, j2k_tile_base *tile, uint16_t c,
            std::vector<int32_t *> img = {});
  int32_t *get_sample_address(uint32_t x, uint32_t y);
  float *get_fsample_address(uint32_t x, uint32_t y);
  uint8_t get_dwt_levels();
  uint8_t get_transformation();
  uint8_t get_Cmodes() const;
  uint8_t get_bitdepth() const;
  element_siz get_precinct_size(uint8_t r);
  element_siz get_codeblock_size();
  uint8_t get_ROIshift() const;
  j2k_resolution *access_resolution(uint8_t r);
  void create_resolutions(uint16_t numlayers);

  void perform_dc_offset(uint8_t transformation, bool is_signed);

  void show_samples() {
    for (int i = 0; i < pos1.y - pos0.y; ++i) {
      for (int j = 0; j < pos1.x - pos0.x; ++j) {
        printf("%3d ", this->samples[i * (pos1.x - pos0.x) + j]);
      }
      printf("\n");
    }
  }
};

/********************************************************************************
 * j2k_tile
 *******************************************************************************/
class j2k_tile : public j2k_tile_base {
 private:
  // vector array of tile-parts
  std::vector<std::unique_ptr<j2k_tile_part>> tile_part;
  // index of this tile
  uint16_t index;
  // number of components
  uint16_t num_components;
  // SOP is used or not (Table A.13)
  bool use_SOP;
  // EPH is used or not (Table A.13)
  bool use_EPH;
  // progression order (Table A.16)
  uint8_t progression_order;
  // number of layers (Table A.14)
  uint16_t numlayers;
  // multiple component transform (Table A.17)
  uint8_t MCT;

  // length of tile (in bytes)
  uint32_t length;
  // pointer to tile buffer
  std::unique_ptr<buf_chain> tile_buf;
  // pointer to packet header
  buf_chain *packet_header;
  // buffer for PPM marker segments
  buf_chain sbst_packet_header;
  // number of tile-parts
  uint8_t num_tile_part;
  // position of current tile-part
  int current_tile_part_pos;
  // unique pointer to tile-components
  std::unique_ptr<j2k_tile_component[]> tcomp;
  // pointer to packet header in PPT marker segments
  std::unique_ptr<buf_chain> ppt_header;
  // number_of_packets (for encoder only)
  int32_t num_packets;
  // unique pointer to packets
  std::unique_ptr<j2c_packet[]> packet;
  // value of Ccap15 parameter in CAP marker segment
  uint16_t Ccap15;
  // progression order information for both COD and POC
  POC_marker porder_info;
  // return SOP is used or not
  bool is_use_SOP() const { return this->use_SOP; }
  // return EPH is used or not
  bool is_use_EPH() const { return this->use_EPH; }
  // set members related to COD marker
  void setCODparams(COD_marker *COD);
  // set members related to QCD marker
  void setQCDparams(QCD_marker *QCD);
  // read packets
  void read_packet(j2k_precinct *current_precint, uint16_t layer, uint8_t num_band);
  // function to retrieve greatest common divisor of precinct size among resolution levels
  void find_gcd_of_precinct_size(element_siz &out);

 public:
  j2k_tile();
  // Decoding
  // Initialization with tile-index
  void dec_init(uint16_t idx, j2k_main_header &main_header, uint8_t reduce_levels);
  // read and add a tile_part into a tile
  void add_tile_part(SOT_marker &tmpSOT, j2c_src_memory &in, j2k_main_header &main_header);
  // create buffer to store compressed data for decoding
  void create_tile_buf(j2k_main_header &main_header);
  // decoding (does block decoding and IDWT) function for a tile
  void decode(j2k_main_header &main_header);
  // inverse color transform
  void ycbcr_to_rgb(j2k_main_header &main_header);
  // inverse DC offset and clipping
  void finalize(j2k_main_header &main_header);

  // Encoding
  // Initialization with tile-index
  void enc_init(uint16_t idx, j2k_main_header &main_header, std::vector<int32_t *> img);
  // DC offsetting
  int perform_dc_offset(j2k_main_header &main_header);
  // forward color transform
  void rgb_to_ycbcr(j2k_main_header &main_header);
  // encoding (does block encoding and FDWT) function for a tile
  uint8_t *encode(j2k_main_header &numlayers_local);
  // create packets in encoding
  void construct_packets(j2k_main_header &main_header);
  // write packets into destination
  void write_packets(j2c_destination_base &outbuf);

  // getters
  uint16_t get_numlayers() const { return this->numlayers; }
  j2k_tile_component *get_tile_component(uint16_t c);
  uint8_t get_byte_from_tile_buf();
  uint8_t get_bit_from_tile_buf();
  uint32_t get_length() const;
  uint32_t get_buf_length();
};

int32_t htj2k_encode(j2k_codeblock *block, uint8_t ROIshift) noexcept;
