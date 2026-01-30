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

#include "grk_includes.h"

namespace grk
{

namespace
{
  void read_int16_to_float(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
  {
    write<int16_t, float>(p_src_data, p_dest_data, nb_elem);
  }
  void read_int32_to_float(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
  {
    write<int32_t, float>(p_src_data, p_dest_data, nb_elem);
  }
  void read_float32_to_float(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
  {
    write<float, float>(p_src_data, p_dest_data, nb_elem);
  }
  void read_float64_to_float(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
  {
    write<double, float>(p_src_data, p_dest_data, nb_elem);
  }
  void read_int16_to_int32(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
  {
    write<int16_t, int32_t>(p_src_data, p_dest_data, nb_elem);
  }
  void read_int32_to_int32(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
  {
    write<int32_t, int32_t>(p_src_data, p_dest_data, nb_elem);
  }
  void read_float32_to_int32(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
  {
    write<float, int32_t>(p_src_data, p_dest_data, nb_elem);
  }
  void read_float64_to_int32(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
  {
    write<double, int32_t>(p_src_data, p_dest_data, nb_elem);
  }
  const mct_function mct_read_functions_to_float[] = {read_int16_to_float, read_int32_to_float,
                                                      read_float32_to_float, read_float64_to_float};
  const mct_function mct_read_functions_to_int32[] = {read_int16_to_int32, read_int32_to_int32,
                                                      read_float32_to_int32, read_float64_to_int32};

} // namespace

CodingParams::CodingParams()
    : rsiz_(0), pcap_(0), tx0_(0), ty0_(0), t_width_(0), t_height_(0), numComments_(0),
      t_grid_width_(0), t_grid_height_(0), asynchronous_(false), decompressCallback_(nullptr),
      decompressCallbackUserData_(nullptr)
{
  codingParams_ = {};
}
CodingParams::~CodingParams()
{
  for(uint32_t i = 0; i < numComments_; ++i)
    delete[] comment_[i];
  numComments_ = 0;
}

void CodingParams::init(grk_decompress_parameters* parameters,
                        std::unique_ptr<TileCache>& tileCache)
{
  assert(parameters);
  dw_x0 = parameters->dw_x0;
  dw_y0 = parameters->dw_y0;
  dw_x1 = parameters->dw_x1;
  dw_y1 = parameters->dw_y1;
  asynchronous_ = parameters->asynchronous;
  simulate_synchronous_ = parameters->simulate_synchronous;
  decompressCallback_ = parameters->decompress_callback;
  decompressCallbackUserData_ = parameters->decompress_callback_user_data;
  auto core = &parameters->core;
  codingParams_.dec_.reduce_ = core->reduce;
  codingParams_.dec_.disableRandomAccessFlags_ = core->disable_random_access_flags;
  codingParams_.dec_.skipAllocateComposite_ = core->skip_allocate_composite;
  if(core->layers_to_decompress != codingParams_.dec_.layersToDecompress_ ||
     core->reduce != codingParams_.dec_.reduce_)
  {
    tileCache->setDirty(true);
  }
  codingParams_.dec_.layersToDecompress_ = core->layers_to_decompress;
}

bool CodingParams::hasTLM(void) const noexcept
{
  return this->tlmMarkers_ && tlmMarkers_->valid();
}
uint8_t CodingParams::getNumTilePartsFromTLM(uint16_t tileIndex) const noexcept
{
  return hasTLM() ? tlmMarkers_->getNumTileParts(tileIndex) : 0;
}

// (canvas coordinates)
Rect32 CodingParams::getTileBounds(Rect32 imageBounds, uint16_t tile_x, uint16_t tile_y) const
{
  Rect32 rc;

  /* find extent of tile */
  assert(tx0_ + (uint64_t)tile_x * t_width_ < UINT_MAX);
  rc.x0 = std::max<uint32_t>(tx0_ + tile_x * t_width_, imageBounds.x0);
  assert(ty0_ + (uint64_t)tile_y * t_height_ < UINT_MAX);
  rc.y0 = std::max<uint32_t>(ty0_ + tile_y * t_height_, imageBounds.y0);

  uint64_t temp = tx0_ + ((uint64_t)tile_x + 1) * t_width_;
  rc.x1 = (temp > imageBounds.x1) ? imageBounds.x1 : (uint32_t)temp;

  temp = ty0_ + ((uint64_t)tile_y + 1) * t_height_;
  rc.y1 = (temp > imageBounds.y1) ? imageBounds.y1 : (uint32_t)temp;

  return rc;
}

bool CodingParams::readCom(uint8_t* headerData, uint16_t headerSize)
{
  assert(headerData != nullptr);
  assert(headerSize != 0);
  if(headerSize < 2)
  {
    grklog.error("readCom: Corrupt COM segment ");
    return false;
  }
  else if(headerSize == 2)
  {
    grklog.warn("readCom: Empty COM segment. Ignoring ");
    return true;
  }

  std::lock_guard<std::mutex> lk(commentMutex);
  if(numComments_ == GRK_NUM_COMMENTS_SUPPORTED)
  {
    grklog.warn("readCom: Only %u comments are supported. Ignoring", GRK_NUM_COMMENTS_SUPPORTED);
    return true;
  }

  uint16_t commentType;
  grk_read(&headerData, &commentType);
  auto numComments = numComments_;
  isBinaryComment_[numComments] = (commentType == 0);
  if(commentType > 1)
  {
    grklog.warn("readCom: Unrecognized comment type 0x%x. Assuming IS "
                "8859-15:1999 (Latin) values",
                commentType);
  }
  uint16_t commentSize = (uint16_t)(headerSize - 2);
  size_t commentSizeToAlloc = commentSize;
  if(!isBinaryComment_[numComments])
    commentSizeToAlloc++;
  comment_[numComments] = (char*)new uint8_t[commentSizeToAlloc];
  if(!comment_[numComments])
  {
    grklog.error("readCom: Out of memory when allocating memory for comment ");
    return false;
  }
  memcpy(comment_[numComments], headerData, commentSize);
  commentLength_[numComments] = commentSize;

  // make null-terminated string
  if(!isBinaryComment_[numComments])
    comment_[numComments][commentSize] = 0;
  numComments_++;
  return true;
}

TileCodingParams::TileCodingParams(CodingParams* cp) : cp_(cp)
{
  for(auto i = 0; i < maxCompressLayersGRK; ++i)
    rates_[i] = 0.0;
  for(auto i = 0; i < maxCompressLayersGRK; ++i)
    distortion_[i] = 0;
  for(auto i = 0; i < 32; ++i)
    memset(progressionOrderChange_ + i, 0, sizeof(grk_progression));
}
TileCodingParams::TileCodingParams(const TileCodingParams& rhs)
    : cp_(rhs.cp_), wholeTileDecompress_(rhs.wholeTileDecompress_), csty_(rhs.csty_),
      prg_(rhs.prg_), numLayers_(rhs.numLayers_), layersToDecompress_(rhs.layersToDecompress_),
      mct_(rhs.mct_), numpocs_(rhs.numpocs_), pptMarkersCount_(rhs.pptMarkersCount_),
      pptMarkers_(nullptr), // will be deep copied if needed
      pptData_(nullptr), pptBuffer_(nullptr), pptLength_(rhs.pptLength_),
      mainQcdQntsty(rhs.mainQcdQntsty), mainQcdNumStepSizes(rhs.mainQcdNumStepSizes),
      tccps_(nullptr), tilePartCounter_(rhs.tilePartCounter_),
      signalledNumTileParts_(rhs.signalledNumTileParts_),
      packets_(nullptr), // assume deep copy if needed, or set appropriately
      mct_norms_(nullptr), mctDecodingMatrix_(nullptr), mctCodingMatrix_(nullptr),
      mctRecords_(nullptr), numMctRecords_(rhs.numMctRecords_), numMaxMctRecords_(0),
      mccRecords_(nullptr), numMccRecords_(rhs.numMccRecords_), numMaxMccRecords_(0),
      cod_(rhs.cod_), ppt_(rhs.ppt_), qcd_(nullptr), // set to nullptr or clone if possible
      numComps_(rhs.numComps_), ht_(rhs.ht_)
{
  memcpy(rates_, rhs.rates_, sizeof(rates_));
  memcpy(distortion_, rhs.distortion_, sizeof(distortion_));
  memcpy(progressionOrderChange_, rhs.progressionOrderChange_, sizeof(progressionOrderChange_));

  uint32_t tccp_size = numComps_ * sizeof(TileComponentCodingParams);
  uint64_t mct_size = static_cast<uint64_t>(numComps_) * numComps_ * sizeof(float);

  // Initialize some values
  cod_ = 0;
  ppt_ = false;
  pptData_ = nullptr;

  // Deep copy tccps_
  tccps_ = new TileComponentCodingParams[numComps_];
  memcpy(tccps_, rhs.tccps_, tccp_size);

  // Deep copy mctDecodingMatrix_
  if(rhs.mctDecodingMatrix_)
  {
    mctDecodingMatrix_ = static_cast<float*>(grk_malloc(mct_size));
    if(!mctDecodingMatrix_)
    {
      // Handle error, perhaps throw exception
    }
    memcpy(mctDecodingMatrix_, rhs.mctDecodingMatrix_, mct_size);
  }

  // Deep copy mctRecords_
  uint32_t mct_records_size = rhs.numMaxMctRecords_ * sizeof(grk_mct_data);
  mctRecords_ = static_cast<grk_mct_data*>(grk_malloc(mct_records_size));
  if(!mctRecords_)
  {
    // Handle error
  }
  memcpy(mctRecords_, rhs.mctRecords_, mct_records_size);

  for(uint32_t j = 0; j < rhs.numMctRecords_; ++j)
  {
    auto src_mct_rec = rhs.mctRecords_ + j;
    auto dest_mct_rec = mctRecords_ + j;
    if(src_mct_rec->data_)
    {
      dest_mct_rec->data_ = static_cast<uint8_t*>(grk_malloc(src_mct_rec->data_size_));
      if(!dest_mct_rec->data_)
      {
        // Handle error
      }
      memcpy(dest_mct_rec->data_, src_mct_rec->data_, src_mct_rec->data_size_);
    }
    numMaxMctRecords_ += 1;
  }

  // Deep copy mccRecords_
  uint32_t mcc_records_size = rhs.numMaxMccRecords_ * sizeof(grk_simple_mcc_decorrelation_data);
  mccRecords_ = static_cast<grk_simple_mcc_decorrelation_data*>(grk_malloc(mcc_records_size));
  if(!mccRecords_)
  {
    // Handle error
  }
  memcpy(mccRecords_, rhs.mccRecords_, mcc_records_size);
  numMaxMccRecords_ = rhs.numMaxMccRecords_;

  for(uint32_t j = 0; j < rhs.numMaxMccRecords_; ++j)
  {
    auto src_mcc_rec = rhs.mccRecords_ + j;
    auto dest_mcc_rec = mccRecords_ + j;
    if(src_mcc_rec->decorrelation_array_)
    {
      uint32_t offset = static_cast<uint32_t>(src_mcc_rec->decorrelation_array_ - rhs.mctRecords_);
      dest_mcc_rec->decorrelation_array_ = mctRecords_ + offset;
    }
    if(src_mcc_rec->offset_array_)
    {
      uint32_t offset = static_cast<uint32_t>(src_mcc_rec->offset_array_ - rhs.mctRecords_);
      dest_mcc_rec->offset_array_ = mctRecords_ + offset;
    }
  }

  // For qcd_, since the original copyFrom keeps the old one, but for copy ctor, we clone it if
  // possible Assuming Quantizer has a copy constructor or clone method; otherwise, recreate based
  // on parameters
  if(rhs.qcd_)
  {
    // Example: qcd_ = new Quantizer(*rhs.qcd_);
    // Or recreate
    bool reversible = (rhs.tccps_[0].qmfbid_ == 1);
    uint8_t guardBits = rhs.tccps_[0].numgbits_;
    qcd_ = QuantizerFactory::makeQuantizer(rhs.ht_, reversible, guardBits);
  }

  // Deep copy other pointers if necessary, e.g. packets_, mct_norms_, mctCodingMatrix_,
  // pptMarkers_, pptBuffer_ For example, if they need deep copy, implement similarly For now,
  // assuming they are set to null or handled elsewhere If needed, add deep copy logic for them as
  // well
}
TileCodingParams::~TileCodingParams()
{
  if(pptMarkers_ != nullptr)
  {
    for(uint32_t i = 0U; i < pptMarkersCount_; ++i)
      grk_free(pptMarkers_[i].data_);
    grk_free(pptMarkers_);
  }

  delete[] pptBuffer_;
  delete[] tccps_;
  grk_free(mctCodingMatrix_);
  grk_free(mctDecodingMatrix_);
  if(mccRecords_)
    grk_free(mccRecords_);
  if(mctRecords_)
  {
    auto mct_data = mctRecords_;
    for(uint32_t i = 0; i < numMctRecords_; ++i)
    {
      grk_free(mct_data->data_);
      ++mct_data;
    }
    grk_free(mctRecords_);
  }
  grk_free(mct_norms_);
  delete packets_;
  delete qcd_;
}

bool TileCodingParams::advanceTilePartCounter(uint16_t tile_index, uint8_t tilePartIndex)
{
  /* We must avoid reading the same tile part number twice for a given tile */
  /* to avoid various issues, like grk_merge_ppt being called several times. */
  /* ISO 15444-1 A.4.2 Start of tile-part (SOT) mandates that tile parts */
  /* should appear in increasing order. */
  if(tilePartCounter_ != tilePartIndex)
  {
    grklog.error("Invalid tile part index for tile number %u. "
                 "Got %u, expected %u",
                 tile_index, tilePartIndex, tilePartCounter_);
    return false;
  }
  tilePartCounter_++;

  return true;
}

bool TileCodingParams::initDefault(GrkImage* headerImage)
{
  tccps_ = new TileComponentCodingParams[numComps_];
  mctRecords_ = (grk_mct_data*)grk_calloc(default_number_mct_records, sizeof(grk_mct_data));
  if(!mctRecords_)
  {
    grklog.error("Not enough memory for SIZ marker");
    return false;
  }
  numMaxMctRecords_ = default_number_mct_records;
  mccRecords_ = (grk_simple_mcc_decorrelation_data*)grk_calloc(
      default_number_mcc_records, sizeof(grk_simple_mcc_decorrelation_data));
  if(!mccRecords_)
  {
    grklog.error("Not enough memory for SIZ marker");
    return false;
  }
  numMaxMccRecords_ = default_number_mcc_records;
  /* set up default dc level shift */
  for(uint16_t i = 0; i < numComps_; ++i)
  {
    if(!headerImage->comps[i].sgnd)
      tccps_[i].dcLevelShift_ = 1 << (headerImage->comps[i].prec - 1);
  }
  return true;
}

bool TileCodingParams::readQcd(bool fromTileHeader, uint8_t* headerData, uint16_t headerSize)
{
  assert(headerData != nullptr);
  if(!readSQcdSQcc(fromTileHeader, false, 0, headerData, &headerSize))
    return false;
  if(headerSize != 0)
  {
    grklog.error("Error reading QCD marker");
    return false;
  }

  // Apply the quantization parameters to the other components
  // of the current tile or default_tcp_
  auto src = tccps_;
  assert(src);
  for(uint32_t i = 1; i < numComps_; ++i)
  {
    auto dest = src + i;
    // respect the QCD/QCC scoping rules
    bool ignore = false;
    if(dest->fromQCC_)
    {
      if(!src->fromTileHeader_ || dest->fromTileHeader_)
        ignore = true;
    }
    if(!ignore)
    {
      dest->qntsty_ = src->qntsty_;
      dest->numgbits_ = src->numgbits_;
      auto size = GRK_MAXBANDS * sizeof(grk_stepsize);
      memcpy(dest->stepsizes_, src->stepsizes_, size);
    }
  }
  return true;
}

bool TileCodingParams::readQcc(bool fromTileHeader, uint8_t* headerData, uint16_t headerSize)
{
  assert(headerData != nullptr);
  uint16_t comp_no;
  if(numComps_ <= 256)
  {
    if(headerSize < 1)
    {
      grklog.error("Error reading QCC marker");
      return false;
    }
    grk_read(headerData++, &comp_no, 1);
    --headerSize;
  }
  else
  {
    if(headerSize < 2)
    {
      grklog.error("Error reading QCC marker");
      return false;
    }
    grk_read(&headerData, &comp_no);
    headerSize = (uint16_t)(headerSize - 2);
  }

  if(comp_no >= numComps_)
  {
    grklog.error("QCC component: component number: %u must be less than"
                 " total number of components: %u",
                 comp_no, numComps_);
    return false;
  }

  if(!readSQcdSQcc(fromTileHeader, true, comp_no, headerData, &headerSize))
    return false;

  if(headerSize != 0)
  {
    grklog.error("Error reading QCC marker");
    return false;
  }

  return true;
}

bool TileCodingParams::readCoc(uint8_t* headerData, uint16_t headerSize)
{
  uint16_t comp_room;
  uint16_t comp_no;
  assert(headerData != nullptr);
  comp_room = numComps_ <= 256 ? 1 : 2;

  /* make sure room is sufficient*/
  if(headerSize < comp_room + 1)
  {
    grklog.error("Error reading COC marker");
    return false;
  }
  headerSize = (uint16_t)(headerSize - (comp_room + 1));

  grk_read(headerData, &comp_no, comp_room); /* Ccoc */
  headerData += comp_room;
  if(comp_no >= numComps_)
  {
    grklog.error("Error reading COC marker : invalid component number %u", comp_no);
    return false;
  }

  tccps_[comp_no].csty_ = *headerData++; /* Scoc */

  if(!readSPCodSPCoc(comp_no, headerData, &headerSize))
    return false;

  if(headerSize != 0)
  {
    grklog.error("Error reading COC marker");
    return false;
  }
  return true;
}

bool TileCodingParams::readCod(uint8_t* headerData, uint16_t headerSize)
{
  uint32_t i;
  assert(headerData != nullptr);

  /* Only one COD per tile */
  std::atomic_ref<uint32_t> ref(cod_);
  uint32_t codVal = ++ref;
  if(codVal > 1)
  {
    grklog.warn("Multiple COD markers detected."
                " The JPEG 2000 standard does not allow more than one COD marker per tile.");
    return true;
  }

  /* Make sure room is sufficient */
  if(headerSize < codSocLen)
  {
    grklog.error("Error reading COD marker");
    return false;
  }
  grk_read(&headerData, &csty_); /* Scod */
  /* Make sure we know how to decompress this */
  if((csty_ & ~(uint32_t)(CP_CSTY_PRT | CP_CSTY_SOP | CP_CSTY_EPH)) != 0U)
  {
    grklog.error("Unknown Scod value 0x%x in COD marker", csty_);
    return false;
  }
  uint8_t tmp;
  grk_read(&headerData, &tmp); /* SGcod (A) */
  /* Make sure progression order is valid */
  if(tmp >= GRK_NUM_PROGRESSION_ORDERS)
  {
    grklog.error("Unknown progression order %u in COD marker", tmp);
    return false;
  }
  prg_ = (GRK_PROG_ORDER)tmp;
  grk_read(&headerData, &numLayers_); /* SGcod (B) */
  if(numLayers_ == 0)
  {
    grklog.error("Number of layers must be positive");
    return false;
  }
  updateLayersToDecompress();

  grk_read(&headerData, &mct_); /* SGcod (C) */
  if(mct_ > 1)
  {
    grklog.error("Invalid MCT value : %u. Should be either 0 or 1", mct_);
    return false;
  }
  headerSize = (uint16_t)(headerSize - codSocLen);
  for(i = 0; i < numComps_; ++i)
    tccps_[i].csty_ = csty_ & CCP_CSTY_PRECINCT;

  if(!readSPCodSPCoc(0, headerData, &headerSize))
    return false;

  if(headerSize != 0)
  {
    grklog.error("Error reading COD marker");
    return false;
  }
  /* Apply the coding style to other components of the current tile or the default_tcp_*/
  /* loop */
  uint32_t prc_size;
  auto ref_tccp = &tccps_[0];
  prc_size = ref_tccp->numresolutions_ * sizeof(uint8_t);

  for(i = 1; i < numComps_; ++i)
  {
    auto copied_tccp = ref_tccp + i;
    copied_tccp->numresolutions_ = ref_tccp->numresolutions_;
    copied_tccp->cblkw_expn_ = ref_tccp->cblkw_expn_;
    copied_tccp->cblkh_expn_ = ref_tccp->cblkh_expn_;
    copied_tccp->cblkStyle_ = ref_tccp->cblkStyle_;
    copied_tccp->qmfbid_ = ref_tccp->qmfbid_;
    memcpy(copied_tccp->precWidthExp_, ref_tccp->precWidthExp_, prc_size);
    memcpy(copied_tccp->precHeightExp_, ref_tccp->precHeightExp_, prc_size);
  }

  return true;
}

bool TileCodingParams::readRgn(uint8_t* headerData, uint16_t headerSize)
{
  uint16_t comp_no;
  uint8_t roi_sty;
  assert(headerData != nullptr);
  uint16_t comp_room = (numComps_ <= 256) ? 1 : 2;

  if(headerSize != 2 + comp_room)
  {
    grklog.error("Error reading RGN marker");
    return false;
  }

  /* Crgn */
  grk_read(headerData, &comp_no, comp_room);
  headerData += comp_room;
  /* Srgn */
  grk_read(&headerData, &roi_sty);
  if(roi_sty != 0)
  {
    grklog.error("RGN marker RS value of %u is not supported by JPEG 2000 Part 1", roi_sty);
    return false;
  }
  if(comp_no >= numComps_)
  {
    grklog.error("bad component number in RGN (%u is >= number of components %u)", comp_no,
                 numComps_);
    return false;
  }

  /* SPrgn */
  grk_read(&headerData, &tccps_[comp_no].roishift_);
  if(tccps_[comp_no].roishift_ >= 32)
  {
    grklog.error("Unsupported ROI shift : %u", tccps_[comp_no].roishift_);
    return false;
  }

  return true;
}

bool TileCodingParams::readPoc(uint8_t* headerData, uint16_t headerSize, int tilePartIndex)
{
  assert(headerData != nullptr);

  uint16_t numComps = numComps_;
  uint32_t componentRoom = (numComps <= 256) ? 1 : 2;
  uint32_t chunkSize = 5 + 2 * componentRoom;
  uint32_t currentNumProgressions = headerSize / chunkSize;
  uint32_t currentRemainingProgressions = headerSize % chunkSize;

  if((currentNumProgressions == 0) || (currentRemainingProgressions != 0))
  {
    grklog.error("Error reading POC marker");
    return false;
  }

  std::vector<grk_progression> newPocs;
  newPocs.reserve(currentNumProgressions);

  for(uint32_t i = 0; i < currentNumProgressions; ++i)
  {
    grk_progression prog;
    /* RSpoc_i */
    grk_read(headerData, &prog.res_s);
    headerData += 1;
    /* CSpoc_i */
    grk_read(headerData, &prog.comp_s, componentRoom);
    headerData += componentRoom;
    /* LYEpoc_i */
    grk_read(headerData, &prog.lay_e);
    headerData += 2;
    /* REpoc_i */
    grk_read(headerData, &prog.res_e);
    headerData += 1;
    /* CEpoc_i */
    grk_read(headerData, &prog.comp_e, componentRoom);
    headerData += componentRoom;
    /* Ppoc_i */
    uint8_t tmp;
    grk_read(headerData, &tmp);
    headerData += 1;
    if(tmp >= GRK_NUM_PROGRESSION_ORDERS)
    {
      grklog.error("readPoc: unknown POC progression order %u", tmp);
      return false;
    }
    prog.progression = (GRK_PROG_ORDER)tmp;

    newPocs.push_back(prog);
  }

  {
    std::lock_guard<std::mutex> lock(pocMutex_);
    if(tilePartIndex == -1)
    {
      uint32_t oldNum = numpocs_ + 1;
      if(oldNum + currentNumProgressions > GRK_MAXRLVLS)
      {
        grklog.error("read_poc: number of progressions %u exceeds Grok maximum number %u",
                     oldNum + currentNumProgressions, GRK_MAXRLVLS);
        return false;
      }
      for(uint32_t i = 0; i < currentNumProgressions; ++i)
      {
        progressionOrderChange_[oldNum + i] = newPocs[i];
      }
      numpocs_ = oldNum + currentNumProgressions - 1;
    }
    else
    {
      if(static_cast<size_t>(tilePartIndex) >= pocLists_.size())
      {
        pocLists_.resize(static_cast<size_t>(tilePartIndex) + 1);
      }
      pocLists_[(uint8_t)tilePartIndex] = std::move(newPocs);
    }
  }
  return true;
}

void TileCodingParams::finalizePocs(void)
{
  std::lock_guard<std::mutex> lock(pocMutex_);

  uint8_t maxNumResLevels = 0;
  for(uint16_t i = 0; i < numComps_; ++i)
  {
    maxNumResLevels = std::max(maxNumResLevels, tccps_[i].numresolutions_);
  }

  // Validate main header POCs if any
  for(uint32_t i = 0; i < numpocs_; ++i)
  {
    auto& prog = progressionOrderChange_[i];
    prog.lay_e = std::min<uint16_t>(prog.lay_e, numLayers_);
    prog.res_e = std::min<uint8_t>(prog.res_e, maxNumResLevels);
    prog.comp_e = std::min<uint16_t>(prog.comp_e, numComps_);

    if(prog.res_s >= maxNumResLevels)
    {
      grklog.error("finalizePocs: invalid POC start resolution number %u", prog.res_s);
      // handle error, e.g., throw or set error state
      return;
    }
    if(prog.res_e <= prog.res_s)
    {
      grklog.error("finalizePocs: invalid POC end resolution %u", prog.res_e);
      return;
    }
    if(prog.comp_s >= numComps_)
    {
      grklog.error("finalizePocs: invalid POC start component %u", prog.comp_s);
      return;
    }
    if(prog.comp_e <= prog.comp_s)
    {
      grklog.error("finalizePocs: invalid POC end component (%u) : end component is "
                   "less than or equal to POC start component (%u)",
                   prog.comp_e, prog.comp_s);
      return;
    }
    if(prog.lay_e == 0)
    {
      grklog.error("finalizePocs: invalid POC end layer 0");
      return;
    }
  }

  uint32_t pos = numpocs_ + 1;
  for(uint8_t tp = 0; tp < signalledNumTileParts_; ++tp)
  {
    if(tp >= pocLists_.size())
      continue;
    auto& list = pocLists_[tp];
    for(auto& prog : list)
    {
      prog.lay_e = std::min<uint16_t>(prog.lay_e, numLayers_);
      prog.res_e = std::min<uint8_t>(prog.res_e, maxNumResLevels);
      prog.comp_e = std::min<uint16_t>(prog.comp_e, numComps_);

      if(prog.res_s >= maxNumResLevels)
      {
        grklog.error("finalizePocs: invalid POC start resolution number %u", prog.res_s);
        return;
      }
      if(prog.res_e <= prog.res_s)
      {
        grklog.error("finalizePocs: invalid POC end resolution %u", prog.res_e);
        return;
      }
      if(prog.comp_s >= numComps_)
      {
        grklog.error("finalizePocs: invalid POC start component %u", prog.comp_s);
        return;
      }
      if(prog.comp_e <= prog.comp_s)
      {
        grklog.error("finalizePocs: invalid POC end component (%u) : end component is "
                     "less than or equal to POC start component (%u)",
                     prog.comp_e, prog.comp_s);
        return;
      }
      if(prog.lay_e == 0)
      {
        grklog.error("finalizePocs: invalid POC end layer 0");
        return;
      }

      if(pos >= GRK_MAXRLVLS)
      {
        grklog.error("finalizePocs: number of progressions %u exceeds Grok maximum number %u",
                     pos + 1, GRK_MAXRLVLS);
        return;
      }
      progressionOrderChange_[pos] = prog;
      ++pos;
    }
  }
  numpocs_ = pos - 1;
  pocLists_.clear();
}

bool TileCodingParams::readMct(uint8_t* headerData, uint16_t headerSize)
{
  uint32_t i;
  uint16_t tmp;
  uint16_t indix;
  assert(headerData != nullptr);
  if(headerSize < 2)
  {
    grklog.error("Error reading MCT marker");
    return false;
  }
  /* first marker */
  /* Zmct */
  grk_read(&headerData, &tmp);
  if(tmp != 0)
  {
    grklog.warn("mct data within multiple MCT records not supported.");
    return true;
  }

  /* Imct -> no need for other values, take the first,
   * type is double with decorrelation x0000 1101 0000 0000*/
  grk_read(&headerData, &tmp); /* Imct */

  indix = tmp;
  auto mct_data = mctRecords_;

  for(i = 0; i < numMctRecords_; ++i)
  {
    if(mct_data->index_ == indix)
      break;
    ++mct_data;
  }

  bool newmct = false;
  // NOT FOUND
  if(i == numMctRecords_)
  {
    if(numMctRecords_ == numMaxMctRecords_)
    {
      grk_mct_data* new_mct_records;
      numMaxMctRecords_ += default_number_mct_records;

      new_mct_records =
          (grk_mct_data*)grk_realloc(mctRecords_, numMaxMctRecords_ * sizeof(grk_mct_data));
      if(!new_mct_records)
      {
        grk_free(mctRecords_);
        mctRecords_ = nullptr;
        numMaxMctRecords_ = 0;
        numMctRecords_ = 0;
        grklog.error("Not enough memory to read MCT marker");
        return false;
      }

      /* Update mcc_records_[].offset_array_ and decorrelation_array_
       * to point to the new addresses */
      if(new_mct_records != mctRecords_)
      {
        for(i = 0; i < numMccRecords_; ++i)
        {
          grk_simple_mcc_decorrelation_data* mcc_record = &(mccRecords_[i]);
          if(mcc_record->decorrelation_array_)
          {
            mcc_record->decorrelation_array_ =
                new_mct_records + (mcc_record->decorrelation_array_ - mctRecords_);
          }
          if(mcc_record->offset_array_)
          {
            mcc_record->offset_array_ = new_mct_records + (mcc_record->offset_array_ - mctRecords_);
          }
        }
      }

      mctRecords_ = new_mct_records;
      mct_data = mctRecords_ + numMctRecords_;
      memset(mct_data, 0, (numMaxMctRecords_ - numMctRecords_) * sizeof(grk_mct_data));
    }

    mct_data = mctRecords_ + numMctRecords_;
    newmct = true;
  }
  if(mct_data->data_)
  {
    grk_free(mct_data->data_);
    mct_data->data_ = nullptr;
    mct_data->data_size_ = 0;
  }
  mct_data->index_ = indix;
  mct_data->array_type_ = (MCT_ARRAY_TYPE)((tmp >> 8) & 3);
  mct_data->element_type_ = (MCT_ELEMENT_TYPE)((tmp >> 10) & 3);
  /* Ymct */
  grk_read(&headerData, &tmp);
  if(tmp != 0)
  {
    grklog.warn("multiple MCT markers not supported");
    return true;
  }
  if(headerSize <= 6)
  {
    grklog.error("Error reading MCT marker");
    return false;
  }
  headerSize = (uint16_t)(headerSize - 6);

  mct_data->data_ = (uint8_t*)grk_malloc(headerSize);
  if(!mct_data->data_)
  {
    grklog.error("Error reading MCT marker");
    return false;
  }
  memcpy(mct_data->data_, headerData, headerSize);
  mct_data->data_size_ = headerSize;
  if(newmct)
    ++numMctRecords_;

  return true;
}

bool TileCodingParams::readMco(uint8_t* headerData, uint16_t headerSize)
{
  uint32_t tmp, i;
  uint32_t nb_stages;
  assert(headerData != nullptr);

  if(headerSize < 1)
  {
    grklog.error("Error reading MCO marker");
    return false;
  }
  /* Nmco : only one transform stage*/
  grk_read(headerData, &nb_stages, 1);
  ++headerData;

  if(nb_stages > 1)
  {
    grklog.warn("Multiple transformation stages not supported.");
    return true;
  }

  if(headerSize != nb_stages + 1)
  {
    grklog.warn("Error reading MCO marker");
    return false;
  }
  for(i = 0; i < numComps_; ++i)
  {
    auto tccp = tccps_ + i;
    tccp->dcLevelShift_ = 0;
  }
  grk_free(mctDecodingMatrix_);
  mctDecodingMatrix_ = nullptr;

  for(i = 0; i < nb_stages; ++i)
  {
    grk_read(headerData, &tmp, 1);
    ++headerData;

    if(!addMct(tmp))
      return false;
  }

  return true;
}

bool TileCodingParams::readMcc(uint8_t* headerData, uint16_t headerSize)
{
  uint32_t i, j;
  uint32_t tmp;
  uint32_t index;
  uint32_t nb_collections;
  uint16_t nb_comps;

  assert(headerData != nullptr);

  if(headerSize < 2)
  {
    grklog.error("Error reading MCC marker");
    return false;
  }

  /* first marker */
  /* Zmcc */
  grk_read(headerData, &tmp, 2);
  headerData += sizeof(uint16_t);
  if(tmp != 0)
  {
    grklog.warn("Multiple data spanning not supported");
    return true;
  }
  if(headerSize < 7)
  {
    grklog.error("Error reading MCC marker");
    return false;
  }

  grk_read(headerData, &index, 1); /* Imcc -> no need for other values, take the first */
  ++headerData;

  auto mcc_record = mccRecords_;

  for(i = 0; i < numMccRecords_; ++i)
  {
    if(mcc_record->index_ == index)
      break;
    ++mcc_record;
  }

  /** NOT FOUND */
  bool newmcc = false;
  if(i == numMccRecords_)
  {
    // resize nb_mcc_records_ if necessary
    if(numMccRecords_ == numMaxMccRecords_)
    {
      grk_simple_mcc_decorrelation_data* new_mcc_records;
      numMaxMccRecords_ += default_number_mcc_records;

      new_mcc_records = (grk_simple_mcc_decorrelation_data*)grk_realloc(
          mccRecords_, numMaxMccRecords_ * sizeof(grk_simple_mcc_decorrelation_data));
      if(!new_mcc_records)
      {
        grk_free(mccRecords_);
        mccRecords_ = nullptr;
        numMaxMccRecords_ = 0;
        numMccRecords_ = 0;
        grklog.error("Not enough memory to read MCC marker");
        return false;
      }
      mccRecords_ = new_mcc_records;
      mcc_record = mccRecords_ + numMccRecords_;
      memset(mcc_record, 0,
             (numMaxMccRecords_ - numMccRecords_) * sizeof(grk_simple_mcc_decorrelation_data));
    }
    // set pointer to prospective new mcc record
    mcc_record = mccRecords_ + numMccRecords_;
    newmcc = true;
  }
  mcc_record->index_ = index;

  /* only one marker atm */
  /* Ymcc */
  grk_read(headerData, &tmp, 2);
  headerData += sizeof(uint16_t);
  if(tmp != 0)
  {
    grklog.warn("Multiple data spanning not supported");
    return true;
  }

  /* Qmcc -> number of collections -> 1 */
  grk_read(headerData, &nb_collections, 2);
  headerData += 2;

  if(nb_collections > 1)
  {
    grklog.warn("Multiple collections not supported");
    return true;
  }
  headerSize = (uint16_t)(headerSize - 7);

  for(i = 0; i < nb_collections; ++i)
  {
    if(headerSize < 3)
    {
      grklog.error("Error reading MCC marker");
      return false;
    }
    grk_read(headerData++, &tmp,
             1); /* Xmcci type of component transformation -> array based decorrelation */

    if(tmp != 1)
    {
      grklog.warn("Collections other than array decorrelations not supported");
      return true;
    }
    grk_read(&headerData, &nb_comps);
    headerSize = (uint16_t)(headerSize - 3);

    uint32_t nb_bytes_by_comp = 1 + (nb_comps >> 15);
    mcc_record->nb_comps_ = nb_comps & 0x7fff;

    if(headerSize < (nb_bytes_by_comp * mcc_record->nb_comps_ + 2))
    {
      grklog.error("Error reading MCC marker");
      return false;
    }

    headerSize = (uint16_t)(headerSize - (nb_bytes_by_comp * mcc_record->nb_comps_ + 2));

    for(j = 0; j < mcc_record->nb_comps_; ++j)
    {
      /* Cmccij Component offset*/
      grk_read(headerData, &tmp, nb_bytes_by_comp);
      headerData += nb_bytes_by_comp;

      if(tmp != j)
      {
        grklog.warn("Collections with index shuffle are not supported");
        return true;
      }
    }

    grk_read(&headerData, &nb_comps);

    nb_bytes_by_comp = 1 + (nb_comps >> 15);
    nb_comps &= 0x7fff;

    if(nb_comps != mcc_record->nb_comps_)
    {
      grklog.warn("Collections with differing number of indices are not supported");
      return true;
    }

    if(headerSize < (nb_bytes_by_comp * mcc_record->nb_comps_ + 3))
    {
      grklog.error("Error reading MCC marker");
      return false;
    }

    headerSize = (uint16_t)(headerSize - (nb_bytes_by_comp * mcc_record->nb_comps_ + 3));

    for(j = 0; j < mcc_record->nb_comps_; ++j)
    {
      /* Wmccij Component offset*/
      grk_read(headerData, &tmp, nb_bytes_by_comp);
      headerData += nb_bytes_by_comp;

      if(tmp != j)
      {
        grklog.warn("Collections with index shuffle not supported");
        return true;
      }
    }
    /* Wmccij Component offset*/
    grk_read(headerData, &tmp, 3);
    headerData += 3;

    mcc_record->is_irreversible_ = !((tmp >> 16) & 1);
    mcc_record->decorrelation_array_ = nullptr;
    mcc_record->offset_array_ = nullptr;

    index = tmp & 0xff;
    if(index != 0)
    {
      for(j = 0; j < numMctRecords_; ++j)
      {
        auto mct_data = mctRecords_ + j;
        if(mct_data->index_ == index)
        {
          mcc_record->decorrelation_array_ = mct_data;
          break;
        }
      }

      if(mcc_record->decorrelation_array_ == nullptr)
      {
        grklog.error("Error reading MCC marker");
        return false;
      }
    }

    index = (tmp >> 8) & 0xff;
    if(index != 0)
    {
      for(j = 0; j < numMctRecords_; ++j)
      {
        auto mct_data = mctRecords_ + j;
        if(mct_data->index_ == index)
        {
          mcc_record->offset_array_ = mct_data;
          break;
        }
      }

      if(mcc_record->offset_array_ == nullptr)
      {
        grklog.error("Error reading MCC marker");
        return false;
      }
    }
  }

  if(headerSize != 0)
  {
    grklog.error("Error reading MCC marker");
    return false;
  }

  // only increment mcc record count if we are working on a new mcc
  // and everything succeeded
  if(newmcc)
    ++numMccRecords_;

  return true;
}

bool TileCodingParams::addMct(uint32_t index)
{
  uint32_t i;
  auto mcc_record = mccRecords_;

  for(i = 0; i < numMccRecords_; ++i)
  {
    if(mcc_record->index_ == index)
      break;
  }

  if(i == numMccRecords_)
  {
    /** element discarded **/
    return true;
  }

  if(mcc_record->nb_comps_ != numComps_)
  {
    return true;
  }
  auto deco_array = mcc_record->decorrelation_array_;
  if(deco_array)
  {
    uint32_t data_size = MCT_ELEMENT_SIZE[deco_array->element_type_] * numComps_ * numComps_;
    if(deco_array->data_size_ != data_size)
      return false;

    uint32_t nb_elem = (uint32_t)numComps_ * numComps_;
    uint32_t mct_size = nb_elem * (uint32_t)sizeof(float);
    mctDecodingMatrix_ = (float*)grk_malloc(mct_size);

    if(!mctDecodingMatrix_)
      return false;

    mct_read_functions_to_float[deco_array->element_type_](deco_array->data_, mctDecodingMatrix_,
                                                           nb_elem);
  }

  auto offset_array = mcc_record->offset_array_;

  if(offset_array)
  {
    uint32_t data_size = MCT_ELEMENT_SIZE[offset_array->element_type_] * numComps_;
    if(offset_array->data_size_ != data_size)
      return false;

    uint32_t nb_elem = numComps_;
    uint32_t offset_size = nb_elem * (uint32_t)sizeof(uint32_t);
    auto offset_data = (uint32_t*)grk_malloc(offset_size);

    if(!offset_data)
      return false;

    mct_read_functions_to_int32[offset_array->element_type_](offset_array->data_, offset_data,
                                                             nb_elem);

    auto current_offset_data = offset_data;
    for(i = 0; i < numComps_; ++i)
    {
      auto tccp = tccps_ + i;
      tccp->dcLevelShift_ = (int32_t)*(current_offset_data++);
    }
    grk_free(offset_data);
  }

  return true;
}

void TileCodingParams::updateLayersToDecompress(void)
{
  uint16_t max = std::max(layersToDecompress_, cp_->codingParams_.dec_.layersToDecompress_);
  layersToDecompress_ = max ? max : numLayers_;
}

bool TileCodingParams::readSQcdSQcc(bool fromTileHeader, bool fromQCC, uint16_t comp_no,
                                    uint8_t* headerData, uint16_t* headerSize)
{
  assert(headerData != nullptr);
  assert(comp_no < numComps_);
  auto tccp = tccps_ + comp_no;

  if(*headerSize < 1)
  {
    grklog.error("Error reading SQcd or SQcc element");
    return false;
  }
  /* Sqcx */
  uint32_t tmp = 0;
  auto current_ptr = headerData;
  grk_read(current_ptr++, &tmp, 1);
  uint8_t qntsty = tmp & 0x1f;
  *headerSize = (uint16_t)(*headerSize - 1);
  if(qntsty > CCP_QNTSTY_SEQNT)
  {
    grklog.error("Undefined quantization style %u", qntsty);
    return false;
  }

  // scoping rules
  bool ignore = false;
  bool mainQCD = !fromQCC && !fromTileHeader;

  if(tccp->quantizationMarkerSet_)
  {
    bool tileHeaderQCC = fromQCC && fromTileHeader;
    bool setMainQCD = !tccp->fromQCC_ && !tccp->fromTileHeader_;
    bool setMainQCC = tccp->fromQCC_ && !tccp->fromTileHeader_;
    bool setTileHeaderQCD = !tccp->fromQCC_ && tccp->fromTileHeader_;
    bool setTileHeaderQCC = tccp->fromQCC_ && tccp->fromTileHeader_;

    if(!fromTileHeader)
    {
      if(setMainQCC || (mainQCD && setMainQCD))
        ignore = true;
    }
    else
    {
      if(setTileHeaderQCC)
        ignore = true;
      else if(setTileHeaderQCD && !tileHeaderQCC)
        ignore = true;
    }
  }

  if(!ignore)
  {
    tccp->quantizationMarkerSet_ = true;
    tccp->fromQCC_ = fromQCC;
    tccp->fromTileHeader_ = fromTileHeader;
    tccp->qntsty_ = qntsty;
    if(mainQCD)
      mainQcdQntsty = tccp->qntsty_;
    tccp->numgbits_ = (uint8_t)(tmp >> 5);
    if(tccp->qntsty_ == CCP_QNTSTY_SIQNT)
    {
      tccp->numStepSizes_ = 1;
    }
    else
    {
      tccp->numStepSizes_ = (tccp->qntsty_ == CCP_QNTSTY_NOQNT) ? (uint8_t)(*headerSize)
                                                                : (uint8_t)((*headerSize) / 2);
      if(tccp->numStepSizes_ > GRK_MAXBANDS)
      {
        grklog.warn("While reading QCD or QCC marker segment, "
                    "number of step sizes (%u) is greater"
                    " than GRK_MAXBANDS (%u).\n"
                    "So, number of elements stored is limited to "
                    "GRK_MAXBANDS (%u) and the rest are skipped.",
                    tccp->numStepSizes_, GRK_MAXBANDS, GRK_MAXBANDS);
      }
    }
    if(mainQCD)
      mainQcdNumStepSizes = tccp->numStepSizes_;
  }
  if(qntsty == CCP_QNTSTY_NOQNT)
  {
    if(*headerSize < tccp->numStepSizes_)
    {
      grklog.error("Error reading SQcd_SQcc marker");
      return false;
    }
    for(uint32_t band_no = 0; band_no < tccp->numStepSizes_; band_no++)
    {
      /* SPqcx_i */
      grk_read(current_ptr++, &tmp, 1);
      if(!ignore)
      {
        if(band_no < GRK_MAXBANDS)
        {
          // top 5 bits for exponent
          tccp->stepsizes_[band_no].expn = (uint8_t)(tmp >> 3);
          // mantissa = 0
          tccp->stepsizes_[band_no].mant = 0;
        }
      }
    }
    *headerSize = (uint16_t)(*headerSize - tccp->numStepSizes_);
  }
  else
  {
    if(*headerSize < 2 * tccp->numStepSizes_)
    {
      grklog.error("Error reading SQcd_SQcc marker");
      return false;
    }
    for(uint32_t band_no = 0; band_no < tccp->numStepSizes_; band_no++)
    {
      /* SPqcx_i */
      grk_read(current_ptr, &tmp, 2);
      current_ptr += 2;
      if(!ignore)
      {
        if(band_no < GRK_MAXBANDS)
        {
          // top 5 bits for exponent
          tccp->stepsizes_[band_no].expn = (uint8_t)(tmp >> 11);
          // bottom 11 bits for mantissa
          tccp->stepsizes_[band_no].mant = (uint16_t)(tmp & 0x7ff);
        }
      }
    }
    *headerSize = (uint16_t)(*headerSize - 2 * tccp->numStepSizes_);
  }
  if(!ignore)
  {
    /* if scalar derived, then compute other stepsizes */
    if(tccp->qntsty_ == CCP_QNTSTY_SIQNT)
    {
      for(uint32_t band_no = 1; band_no < GRK_MAXBANDS; band_no++)
      {
        uint8_t bandDividedBy3 = (uint8_t)((band_no - 1) / 3);
        tccp->stepsizes_[band_no].expn = 0;
        if(tccp->stepsizes_[0].expn > bandDividedBy3)
          tccp->stepsizes_[band_no].expn = (uint8_t)(tccp->stepsizes_[0].expn - bandDividedBy3);
        tccp->stepsizes_[band_no].mant = tccp->stepsizes_[0].mant;
      }
    }
  }
  return true;
}

bool TileCodingParams::readSPCodSPCoc(uint16_t compno, uint8_t* headerData, uint16_t* headerSize)
{
  uint32_t i;
  assert(headerData != nullptr);
  auto tccp = tccps_ + compno;
  auto current_ptr = headerData;

  /* make sure room is sufficient */
  if(*headerSize < SPCodSPCocLen)
  {
    grklog.error("Error reading SPCod SPCoc element");
    return false;
  }
  /* SPcox (D) */
  // note: we actually read the number of decompositions
  grk_read(&current_ptr, &tccp->numresolutions_);
  if(tccp->numresolutions_ > GRK_MAX_DECOMP_LVLS)
  {
    grklog.error("Invalid number of decomposition levels : %u. The JPEG 2000 standard\n"
                 "allows a maximum number of %u decomposition levels.",
                 tccp->numresolutions_, GRK_MAX_DECOMP_LVLS);
    return false;
  }
  ++tccp->numresolutions_;

  /* If user wants to remove more resolutions than the code stream contains, return error */
  if(cp_->codingParams_.dec_.reduce_ >= tccp->numresolutions_)
  {
    grklog.error("Error decoding component %u.\nThe number of resolutions "
                 " to remove (%u) must be strictly less than the number "
                 "of resolutions (%u) of this component.\n"
                 "Please decrease the reduce parameter.",
                 compno, cp_->codingParams_.dec_.reduce_, tccp->numresolutions_);
    return false;
  }
  /* SPcoc (E) */
  grk_read(&current_ptr, &tccp->cblkw_expn_);
  /* SPcoc (F) */
  grk_read(&current_ptr, &tccp->cblkh_expn_);

  if(tccp->cblkw_expn_ > 8 || tccp->cblkh_expn_ > 8 || (tccp->cblkw_expn_ + tccp->cblkh_expn_) > 8)
  {
    grklog.error("Illegal code-block width/height (2^%u, 2^%u) found in COD/COC marker segment.\n"
                 "Code-block dimensions must be powers of 2, must be in the range 4-1024, and "
                 "their product must "
                 "lie in the range 16-4096.",
                 (uint32_t)tccp->cblkw_expn_ + 2, (uint32_t)tccp->cblkh_expn_ + 2);
    return false;
  }

  tccp->cblkw_expn_ = (uint8_t)(tccp->cblkw_expn_ + 2U);
  tccp->cblkh_expn_ = (uint8_t)(tccp->cblkh_expn_ + 2U);

  /* SPcoc (G) */
  tccp->cblkStyle_ = *current_ptr++;
  uint8_t high_bits = (uint8_t)(tccp->cblkStyle_ >> 6U);
  if((tccp->cblkStyle_ & GRK_CBLKSTY_HT_ONLY) == GRK_CBLKSTY_HT_ONLY)
  {
    uint8_t lower_6 = tccp->cblkStyle_ & 0x3f;
    uint8_t non_vsc_modes = (lower_6 & GRK_CBLKSTY_LAZY) | (lower_6 & GRK_CBLKSTY_RESET);
    if(non_vsc_modes != 0)
    {
      grklog.error(
          "Unrecognized code-block style byte 0x%x found in COD/COC marker segment.\nWith bit-6 "
          "set and bit-7 not set i.e all blocks are HT blocks, only vertically causal context "
          "mode "
          "is supported.",
          non_vsc_modes);
      return false;
    }
  }
  if(high_bits == 2)
  {
    grklog.error("Unrecognized code-block style byte 0x%x found in COD/COC marker segment. "
                 "Most significant 2 bits can be 00, 01 or 11, but not 10",
                 tccp->cblkStyle_);
    return false;
  }

  /* SPcoc (H) */
  tccp->qmfbid_ = *current_ptr++;
  if(tccp->qmfbid_ > 1)
  {
    grklog.error("Invalid qmfbid : %u. "
                 "Should be either 0 or 1",
                 tccp->qmfbid_);
    return false;
  }
  *headerSize = (uint16_t)(*headerSize - SPCodSPCocLen);

  /* use custom precinct size ? */
  if(tccp->csty_ & CCP_CSTY_PRECINCT)
  {
    if(*headerSize < tccp->numresolutions_)
    {
      grklog.error("Error reading SPCod SPCoc element");
      return false;
    }

    for(i = 0; i < tccp->numresolutions_; ++i)
    {
      uint8_t tmp;
      /* SPcoc (I_i) */
      grk_read(&current_ptr, &tmp);
      /* Precinct exponent 0 is only allowed for lowest resolution level (Table A.21) */
      if((i != 0) && (((tmp & 0xf) == 0) || ((tmp >> 4) == 0)))
      {
        grklog.error("Invalid precinct size");
        return false;
      }
      tccp->precWidthExp_[i] = tmp & 0xf;
      tccp->precHeightExp_[i] = tmp >> 4U;
    }

    *headerSize = (uint16_t)(*headerSize - tccp->numresolutions_);
  }
  else
  {
    /* set default size for the precinct width and height */
    for(i = 0; i < tccp->numresolutions_; ++i)
    {
      tccp->precWidthExp_[i] = 15;
      tccp->precHeightExp_[i] = 15;
    }
  }

  return true;
}

bool TileCodingParams::readPpt(uint8_t* headerData, uint16_t headerSize)
{
  assert(headerData != nullptr);

  if(cp_->ppmMarkers_)
  {
    grklog.error("Error reading PPT marker: packet header have been previously found in the main "
                 "header (PPM marker).");
    return false;
  }

  /* We need to have the Z_ppt element + 1 byte of Ippt at minimum */
  if(headerSize < 2)
  {
    grklog.error("Error reading PPT marker");
    return false;
  }

  ppt_ = true;

  /* Z_ppt */
  uint8_t Z_ppt;
  grk_read(&headerData, &Z_ppt);
  --headerSize;

  /* check allocation needed */
  if(!pptMarkers_)
  { /* first PPT marker */
    uint32_t newCount = Z_ppt + 1U; /* can't overflow, Z_ppt is UINT8 */
    assert(pptMarkersCount_ == 0U);

    pptMarkers_ = (grk_ppx*)grk_calloc(newCount, sizeof(grk_ppx));
    if(pptMarkers_ == nullptr)
    {
      grklog.error("Not enough memory to read PPT marker");
      return false;
    }
    pptMarkersCount_ = newCount;
  }
  else if(pptMarkersCount_ <= Z_ppt)
  {
    uint32_t newCount = Z_ppt + 1U; /* can't overflow, Z_ppt is UINT8 */
    auto new_ppt_markers = (grk_ppx*)grk_realloc(pptMarkers_, newCount * sizeof(grk_ppx));

    if(new_ppt_markers == nullptr)
    {
      /* clean up to be done on tcp destruction */
      grklog.error("Not enough memory to read PPT marker");
      return false;
    }
    pptMarkers_ = new_ppt_markers;
    memset(pptMarkers_ + pptMarkersCount_, 0, (newCount - pptMarkersCount_) * sizeof(grk_ppx));
    pptMarkersCount_ = newCount;
  }

  if(pptMarkers_[Z_ppt].data_ != nullptr)
  {
    /* clean up to be done on tcp destruction */
    grklog.error("Zppt %u already read", Z_ppt);
    return false;
  }

  pptMarkers_[Z_ppt].data_ = (uint8_t*)grk_malloc(headerSize);
  if(pptMarkers_[Z_ppt].data_ == nullptr)
  {
    /* clean up to be done on tcp destruction */
    grklog.error("Not enough memory to read PPT marker");
    return false;
  }
  pptMarkers_[Z_ppt].data_size_ = headerSize;
  memcpy(pptMarkers_[Z_ppt].data_, headerData, headerSize);
  return true;
}

bool TileCodingParams::mergePpt(void)
{
  assert(pptBuffer_ == nullptr);
  if(!ppt_)
    return true;

  if(pptBuffer_ != nullptr)
  {
    grklog.error("multiple calls to CodeStreamDecompress::merge_ppt()");
    return false;
  }

  uint32_t pptDataSize = 0U;
  for(uint32_t i = 0U; i < pptMarkersCount_; ++i)
  {
    pptDataSize +=
        pptMarkers_[i].data_size_; /* can't overflow, max 256 markers of max 65536 bytes */
  }

  pptBuffer_ = new uint8_t[pptDataSize];
  pptLength_ = pptDataSize;
  pptDataSize = 0U;
  for(uint32_t i = 0U; i < pptMarkersCount_; ++i)
  {
    if(pptMarkers_[i].data_ != nullptr)
    { /* standard doesn't seem to require contiguous Zppt */
      memcpy(pptBuffer_ + pptDataSize, pptMarkers_[i].data_, pptMarkers_[i].data_size_);
      pptDataSize +=
          pptMarkers_[i].data_size_; /* can't overflow, max 256 markers of max 65536 bytes */

      grk_free(pptMarkers_[i].data_);
      pptMarkers_[i].data_ = nullptr;
      pptMarkers_[i].data_size_ = 0U;
    }
  }

  pptMarkersCount_ = 0U;
  grk_free(pptMarkers_);
  pptMarkers_ = nullptr;
  pptData_ = pptBuffer_;

  return true;
}

bool TileCodingParams::validateQuantization(void)
{
  // ensure lossy wavelet has quantization set
  for(uint16_t k = 0; k < numComps_; ++k)
  {
    auto tccp = tccps_ + k;
    if(tccp->qmfbid_ == 0 && tccp->qntsty_ == CCP_QNTSTY_NOQNT)
    {
      grklog.error(
          "Tile-components compressed using the irreversible processing path\n"
          "must have quantization parameters specified in the QCD/QCC marker segments,\n"
          "either explicitly, or through implicit derivation from the quantization\n"
          "parameters for the LL subband, as explained in the JPEG2000 standard, ISO/IEC\n"
          "15444-1.  The present set of code-stream parameters for component %d is not legal.",
          k);
      return false;
    }
  }
  // do QCD marker quantization step size sanity check
  // see page 553 of Taubman and Marcellin for more details on this check
  if(mainQcdQntsty != CCP_QNTSTY_SIQNT)
  {
    // 1. Check main QCD
    uint8_t maxDecompositions = 0;
    for(uint16_t k = 0; k < numComps_; ++k)
    {
      auto tccp = tccps_ + k;
      if(tccp->numresolutions_ == 0)
        continue;
      // only consider number of resolutions from a component
      // whose scope is covered by main QCD;
      // ignore components that are out of scope
      // i.e. under main QCC scope, or tile QCD/QCC scope
      if(tccp->fromQCC_ || tccp->fromTileHeader_)
        continue;
      auto decomps = (uint8_t)(tccp->numresolutions_ - 1);
      if(maxDecompositions < decomps)
        maxDecompositions = decomps;
    }
    if((mainQcdNumStepSizes < 3 * (uint32_t)maxDecompositions + 1))
    {
      grklog.error("From Main QCD marker, "
                   "number of step sizes (%u) is less than "
                   "3* (maximum decompositions) + 1, "
                   "where maximum decompositions = %u ",
                   mainQcdNumStepSizes, maxDecompositions);
      return false;
    }
    // 2. Check Tile QCD
    TileComponentCodingParams* qcd_comp = nullptr;
    for(uint16_t k = 0; k < numComps_; ++k)
    {
      auto tccp = tccps_ + k;
      if(tccp->fromTileHeader_ && !tccp->fromQCC_)
      {
        qcd_comp = tccp;
        break;
      }
    }
    if(qcd_comp && (qcd_comp->qntsty_ != CCP_QNTSTY_SIQNT))
    {
      uint8_t maxTileDecompositions = 0;
      for(uint16_t k = 0; k < numComps_; ++k)
      {
        auto tccp = tccps_ + k;
        if(tccp->numresolutions_ == 0)
          continue;
        // only consider number of resolutions from a component
        // whose scope is covered by Tile QCD;
        // ignore components that are out of scope
        // i.e. under Tile QCC scope
        if(tccp->fromQCC_ && tccp->fromTileHeader_)
          continue;
        auto decomps = (uint8_t)(tccp->numresolutions_ - 1);
        if(maxTileDecompositions < decomps)
          maxTileDecompositions = decomps;
      }
      if((qcd_comp->numStepSizes_ < 3 * maxTileDecompositions + 1))
      {
        grklog.error("From Tile QCD marker, "
                     "number of step sizes (%u) is less than"
                     " 3* (maximum tile decompositions) + 1, "
                     "where maximum tile decompositions = %u ",
                     qcd_comp->numStepSizes_, maxTileDecompositions);

        return false;
      }
    }
  }
  return true;
}

void TileCodingParams::setIsHT(bool ht, bool reversible, uint8_t guardBits)
{
  ht_ = ht;
  if(!qcd_)
    qcd_ = QuantizerFactory::makeQuantizer(ht, reversible, guardBits);
}

bool TileCodingParams::isHT(void)
{
  return ht_;
}
uint32_t TileCodingParams::getNumProgressions()
{
  return numpocs_ + 1;
}
bool TileCodingParams::hasPoc(void)
{
  return numpocs_ > 0;
}
TileComponentCodingParams::TileComponentCodingParams()
    : csty_(0), numresolutions_(0), cblkw_expn_(0), cblkh_expn_(0), cblkStyle_(0), qmfbid_(0),
      quantizationMarkerSet_(false), fromQCC_(false), fromTileHeader_(false), qntsty_(0),
      numStepSizes_(0), numgbits_(0), roishift_(0), dcLevelShift_(0)
{
  for(uint32_t i = 0; i < GRK_MAXRLVLS; ++i)
  {
    precWidthExp_[i] = 0;
    precHeightExp_[i] = 0;
  }
}

} // namespace grk
