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
#include "TileProcessorCompress.h"

namespace grk
{
T2Compress::T2Compress(TileProcessorCompress* tileProc) : tileProcessor(tileProc) {}

bool T2Compress::compressPacketsSimulate(uint16_t tile_no, uint16_t max_layers,
                                         uint32_t* allPacketBytes, uint32_t maxBytes,
                                         uint8_t newTilePartProgressionPosition, PLMarker* markers,
                                         bool isFinal, bool debug)
{
  assert(allPacketBytes);
  auto cp = tileProcessor->getCodingParams();
  auto image = tileProcessor->getHeaderImage();
  auto tcp = tileProcessor->getTCP();
  uint32_t pocno = (cp->rsiz_ == GRK_PROFILE_CINEMA_4K) ? 2 : 1;

  // Cinema profile has CPRL progression and maximum component size specification,
  // so in this case, we set max_comp to the number of components, so we can ensure that
  // each component length meets spec. Otherwise, set to 1.
  uint32_t max_comp = cp->codingParams_.enc_.maxComponentRate_ > 0 ? image->numcomps : 1;

  PacketManager packetManager(true, image, cp, tile_no, THRESH_CALC, tileProcessor);
  *allPacketBytes = 0;
  tileProcessor->getPacketTracker()->clear();
  if(markers)
    markers->pushInit(isFinal);
  for(uint16_t compno = 0; compno < max_comp; ++compno)
  {
    uint64_t componentBytes = 0;
    for(uint32_t poc = 0; poc < pocno; ++poc)
    {
      auto current_pi = packetManager.getPacketIter(poc);
      packetManager.enable_tile_part_generation(poc, (compno == 0), newTilePartProgressionPosition);

      if(current_pi->getProgression() == GRK_PROG_UNKNOWN)
      {
        grklog.error("decompress_packets_simulate: Unknown progression order");
        return false;
      }
      while(current_pi->next(nullptr))
      {
        if(current_pi->getLayno() < max_layers)
        {
          uint32_t bytesInLayer = 0;
          if(!compressPacketSimulate(tcp, current_pi, &bytesInLayer, maxBytes, markers, debug))
            return false;

          componentBytes += bytesInLayer;
          if(maxBytes != UINT_MAX)
          {
            if(maxBytes < bytesInLayer)
            {
              grklog.error(
                  "compressPacketsSimulate: max bytes %d is smaller than bytes in layer %d",
                  maxBytes, bytesInLayer);
              return false;
            }
            maxBytes -= bytesInLayer;
          }
          *allPacketBytes += bytesInLayer;
          if(cp->codingParams_.enc_.maxComponentRate_ &&
             componentBytes > cp->codingParams_.enc_.maxComponentRate_)
            return false;
        }
      }
    }
  }

  return true;
}
bool T2Compress::compressPacketSimulate(TileCodingParams* tcp, PacketIter* pi,
                                        uint32_t* packet_bytes_written,
                                        uint32_t max_bytes_available, PLMarker* markers,
                                        [[maybe_unused]] bool debug)
{
  uint16_t compno = pi->getCompno();
  uint8_t resno = pi->getResno();
  uint64_t precinctIndex = pi->getPrecinctIndex();
  uint16_t layno = pi->getLayno();
  auto tile = tileProcessor->getTile();
  auto tilec = tile->comps_ + compno;
  auto res = tilec->resolutions_ + resno;
  uint64_t byteCount = 0;
  *packet_bytes_written = 0;

  if(compno >= tile->numcomps_)
  {
    grklog.error("compress packet simulate: component number %u must be less than total number "
                 "of components %u",
                 compno, tile->numcomps_);
    return false;
  }
  if(tileProcessor->getPacketTracker()->is_packet_encoded(compno, resno, precinctIndex, layno))
    return true;
  tileProcessor->getPacketTracker()->packet_encoded(compno, resno, precinctIndex, layno);
  if(tcp->csty_ & CP_CSTY_SOP)
  {
    if(max_bytes_available < 6)
      return false;
    if(max_bytes_available != UINT_MAX)
      max_bytes_available -= 6;
    byteCount += 6;
  }
  std::unique_ptr<BitIO> bio(new BitIO(nullptr, max_bytes_available, true));
  if(!compressHeader(bio.get(), res, layno, precinctIndex))
    return false;

  byteCount += (uint32_t)bio->numBytes();
  if(max_bytes_available != UINT_MAX)
    max_bytes_available -= (uint32_t)bio->numBytes();
  if(tcp->csty_ & CP_CSTY_EPH)
  {
    if(max_bytes_available < 2)
      return false;
    if(max_bytes_available != UINT_MAX)
      max_bytes_available -= 2;
    byteCount += 2;
  }
  /* Writing the packet body */
  for(auto bandIndex = 0U; bandIndex < res->numBands_; bandIndex++)
  {
    auto band = res->band + bandIndex;
    auto prc = band->precincts_[precinctIndex];

    auto nb_blocks = prc->getNumCblks();
    for(auto cblkno = 0U; cblkno < nb_blocks; ++cblkno)
    {
      auto cblk = prc->getCompressedBlock(cblkno);
      auto layer = cblk->getLayer(layno);

      if(!layer->totalPasses_)
        continue;
      if(layer->len > max_bytes_available)
        return false;
      cblk->incNumPassesInLayer(0, (uint8_t)layer->totalPasses_);
      byteCount += layer->len;
      if(max_bytes_available != UINT_MAX)
        max_bytes_available -= layer->len;
    }
  }
  if(byteCount > UINT_MAX)
  {
    grklog.error("Tile part size exceeds standard maximum value of %u."
                 "Please enable tile part generation to keep tile part size below max",
                 UINT_MAX);
    return false;
  }
  *packet_bytes_written = (uint32_t)byteCount;
  // if (debug)
  //     printf("c=%d p=%d r=%d l=%d bytes=%d available=%d\n",
  //     compno,precinctIndex,resno,layno,byteCount, max_bytes_available);
  if(markers)
  {
    if(!markers->pushPL(*packet_bytes_written))
      return false;
  }

  return true;
}
///////////////////////////////////////////////////////////////////////////////////////////////

bool T2Compress::compressPackets(uint16_t tile_no, uint16_t max_layers, IStream* stream,
                                 uint32_t* tileBytesWritten, bool first_poc_tile_part,
                                 uint8_t newTilePartProgressionPosition, uint32_t prog_iter_num)
{
  auto cp = tileProcessor->getCodingParams();
  auto image = tileProcessor->getHeaderImage();
  auto tcp = tileProcessor->getTCP();
  PacketManager packetManager(true, image, cp, tile_no, FINAL_PASS, tileProcessor);
  packetManager.enable_tile_part_generation(prog_iter_num, first_poc_tile_part,
                                            newTilePartProgressionPosition);
  auto current_pi = packetManager.getPacketIter(prog_iter_num);
  if(current_pi->getProgression() == GRK_PROG_UNKNOWN)
  {
    grklog.error("compressPackets: Unknown progression order");
    return false;
  }
  while(current_pi->next(nullptr))
  {
    if(current_pi->getLayno() < max_layers)
    {
      uint32_t numBytes = 0;
      if(!compressPacket(tcp, current_pi, stream, &numBytes))
        return false;
      *tileBytesWritten += numBytes;
    }
  }

  return true;
}
bool T2Compress::compressHeader(BitIO* bio, Resolution* res, uint16_t layno, uint64_t precinctIndex)
{
  if(layno == 0)
  {
    for(auto bandIndex = 0U; bandIndex < res->numBands_; ++bandIndex)
    {
      auto band = res->band + bandIndex;
      if(precinctIndex >= band->precincts_.size())
      {
        grklog.error("compress packet simulate: precinct index %u must be less than total "
                     "number of precincts %u",
                     precinctIndex, band->precincts_.size());
        return false;
      }
      auto prc = band->precincts_[precinctIndex];
      uint32_t nb_blocks = prc->getNumCblks();

      if(band->empty() || !nb_blocks)
        continue;

      if(prc->getInclTree())
        prc->getInclTree()->reset();
      if(prc->getImsbTree())
        prc->getImsbTree()->reset();
      for(auto cblkno = 0U; cblkno < nb_blocks; ++cblkno)
      {
        auto cblk = prc->getCompressedBlock(cblkno);
        cblk->setNumPassesInLayer(0, 0);
        assert(band->maxBitPlanes_ >= cblk->numbps());
        if(cblk->numbps() > band->maxBitPlanes_)
        {
          grklog.warn("Code block %u bps %u greater than band bps %u. Skipping.", cblkno,
                      cblk->numbps(), band->maxBitPlanes_);
        }
        else
          prc->getImsbTree()->set(cblkno, band->maxBitPlanes_ - cblk->numbps());
      }
    }
  }

  // Empty header bit. Grok always sets this to 1,
  // even though there is also an option to set it to zero.
  if(!bio->write(1))
    return false;

  /* Writing Packet header */
  for(auto bandIndex = 0U; bandIndex < res->numBands_; ++bandIndex)
  {
    auto band = res->band + bandIndex;
    auto prc = band->precincts_[precinctIndex];
    uint32_t nb_blocks = prc->getNumCblks();

    if(band->empty() || !nb_blocks)
      continue;
    for(auto cblkno = 0U; cblkno < nb_blocks; ++cblkno)
    {
      auto cblk = prc->getCompressedBlock(cblkno);
      auto layer = cblk->getLayer(layno);
      if(!cblk->getNumPassesInLayer(0) && layer->totalPasses_)
        prc->getInclTree()->set(cblkno, layno);
    }
    for(uint32_t cblkno = 0; cblkno < nb_blocks; cblkno++)
    {
      auto cblk = prc->getCompressedBlock(cblkno);
      auto layer = cblk->getLayer(layno);
      uint8_t increment = 0;
      uint32_t nump = 0;
      uint32_t len = 0;

      /* cblk inclusion bits */
      if(!cblk->getNumPassesInLayer(0))
      {
        if(!prc->getInclTree()->encode(bio, cblkno, layno + 1))
          return false;
      }
      else
      {
        if(!bio->write(layer->totalPasses_ != 0))
          return false;
      }
      /* if cblk not included, go to next cblk  */
      if(!layer->totalPasses_)
        continue;
      /* if first instance of cblk --> zero bit-planes information */
      if(!cblk->getNumPassesInLayer(0))
      {
        cblk->setNumLenBits(3);
        if(!prc->getImsbTree()->encode(bio, cblkno, prc->getImsbTree()->getUninitializedValue()))
          return false;
      }
      /* number of coding passes included */
      if(!bio->putnumpasses(layer->totalPasses_))
        return false;
      uint32_t nb_passes = cblk->getNumPassesInLayer(0) + layer->totalPasses_;
      auto pass = cblk->getPass(cblk->getNumPassesInLayer(0));

      /* computation of the increase of the length indicator and insertion in the header */
      for(uint32_t passno = cblk->getNumPassesInLayer(0); passno < nb_passes; ++passno)
      {
        ++nump;
        len += pass->len_;

        if(pass->term_ || passno == nb_passes - 1)
        {
          increment = (uint8_t)std::max<int8_t>(
              (int8_t)increment,
              int8_t(floorlog2(len) + 1 - (cblk->numlenbits() + floorlog2(nump))));
          len = 0;
          nump = 0;
        }
        ++pass;
      }
      if(!bio->putcommacode(increment))
        return false;
      /* computation of the new Length indicator */
      cblk->setNumLenBits(cblk->numlenbits() + increment);

      pass = cblk->getPass(cblk->getNumPassesInLayer(0));
      /* insertion of the codeword segment length */
      for(uint32_t passno = cblk->getNumPassesInLayer(0); passno < nb_passes; ++passno)
      {
        nump++;
        len += pass->len_;
        if(pass->term_ || passno == nb_passes - 1)
        {
          if(!bio->write(len, cblk->numlenbits() + floorlog2(nump)))
            return false;
          len = 0;
          nump = 0;
        }
        ++pass;
      }
    }
  }
  if(!bio->flush())
    return false;

  return true;
}
bool T2Compress::compressPacket(TileCodingParams* tcp, PacketIter* pi, IStream* stream,
                                uint32_t* packet_bytes_written)
{
  assert(stream);

  uint16_t compno = pi->getCompno();
  uint8_t resno = pi->getResno();
  uint64_t precinctIndex = pi->getPrecinctIndex();
  uint16_t layno = pi->getLayno();
  auto tile = tileProcessor->getTile();
  auto tilec = tile->comps_ + compno;
  size_t stream_start = stream->tell();

  if(compno >= tile->numcomps_)
  {
    grklog.error("compress packet simulate: component number %u must be less than total number "
                 "of components %u",
                 compno, tile->numcomps_);
    return false;
  }
  if(tileProcessor->getPacketTracker()->is_packet_encoded(compno, resno, precinctIndex, layno))
    return true;
  tileProcessor->getPacketTracker()->packet_encoded(compno, resno, precinctIndex, layno);

  // SOP marker
  if(tcp->csty_ & CP_CSTY_SOP)
  {
    if(!stream->write8u(SOP >> 8))
      return false;
    if(!stream->write8u(SOP & 0xff))
      return false;
    if(!stream->write8u(0))
      return false;
    if(!stream->write8u(4))
      return false;
    /* numProcessedPackets is uint64_t modulo 65536, in big endian format */
    // note - when compressing, numProcessedPackets in fact equals packet index,
    // i.e. one less than number of processed packets
    auto numProcessedPackets = (uint16_t)(tileProcessor->getNumProcessedPackets() & 0xFFFF);
    grklog.debug("SOP: compressed packet %d", numProcessedPackets);
    if(!stream->write8u((uint8_t)(numProcessedPackets >> 8)))
      return false;
    if(!stream->write8u((uint8_t)(numProcessedPackets & 0xff)))
      return false;
  }
  auto bio = std::unique_ptr<BitIO>(new BitIO(stream, true));

  // initialize precinct and code blocks if this is the first layer
  auto res = tilec->resolutions_ + resno;
  if(!compressHeader(bio.get(), res, layno, precinctIndex))
    return false;

  // EPH marker
  if(tcp->csty_ & CP_CSTY_EPH)
  {
    if(!stream->write8u(EPH >> 8))
      return false;
    if(!stream->write8u(EPH & 0xff))
      return false;
  }

  /* Writing the packet body */
  for(uint8_t bandIndex = 0; bandIndex < res->numBands_; bandIndex++)
  {
    auto band = res->band + bandIndex;
    auto prc = band->precincts_[precinctIndex];
    uint32_t nb_blocks = prc->getNumCblks();

    if(band->empty() || !nb_blocks)
      continue;
    for(auto cblkno = 0U; cblkno < nb_blocks; ++cblkno)
    {
      auto cblk = prc->getCompressedBlock(cblkno);
      auto cblk_layer = cblk->getLayer(layno);
      if(!cblk_layer->totalPasses_)
        continue;
      if(cblk_layer->len && !stream->writeBytes(cblk_layer->data, cblk_layer->len))
        return false;
      cblk->incNumPassesInLayer(0, (uint8_t)cblk_layer->totalPasses_);
    }
  }
  *packet_bytes_written += (uint32_t)(stream->tell() - stream_start);
  tileProcessor->incNumProcessedPackets(1);

  return true;
}

} // namespace grk
