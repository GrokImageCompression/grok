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

#include <memory>
#include <stdexcept>

#include "CodeblockImpl.h"
#include "CodeStreamLimits.h"
#include "BitIO.h"
#include "grk_exceptions.h"
#include "t1_common.h"
#include "mqc.h"
#include "debug_context.h"

namespace grk
{

/**
 * @struct Segment
 * @brief Stores information for a code block segment
 *
 * A segment can be split over multiple packets, and a packet
 * can contain multiple segments
 *
 */
struct Segment
{
  /**
   * @brief Constructs a Segment
   */
  Segment(uint16_t numlayers)
      : numLayers_(numlayers), calculatedPassesInLayer_(new uint8_t[numLayers_]),
        signalledBytesInLayer_(new uint16_t[numLayers_])
  {
    clear();
  }

  /**
   * @brief Destroys a Segment
   */
  ~Segment(void)
  {
    delete[] calculatedPassesInLayer_;
    delete[] signalledBytesInLayer_;
  }

  /**
   * @brief Clears a Segment
   */
  void clear()
  {
    totalPasses_ = 0;
    totalBytes_ = 0;
    maxPasses_ = 0;
    memset(calculatedPassesInLayer_, 0, numLayers_ * sizeof(uint8_t));
    memset(signalledBytesInLayer_, 0, numLayers_ * sizeof(uint16_t));
    for(auto& b : data_chunks_)
      delete b;
    data_chunks_.clear();
  }

  void print(uint16_t layno)
  {
    grklog.info(
        "Segment %p: total passes: %d, max passes: %d, calculated passes in layer: %d total "
        "bytes: %d signalled bytes in layer: %d",
        this, totalPasses_, maxPasses_, calculatedPassesInLayer_[layno], totalBytes_,
        signalledBytesInLayer_[layno]);
  }

  /**
   * @brief Gets combined length of all data chunks
   * @return total length
   */
  size_t getDataChunksLength() const
  {
    return std::accumulate(data_chunks_.begin(), data_chunks_.end(), (size_t)0,
                           [](const size_t s, Buffer8* a) { return s + a->num_elts(); });
  }

  /**
   * @brief Copies data chunks into a single contiguous buffer
   * @param buffer contiguous buffer
   * @return number of bytes copied
   */
  size_t copyDataChunksToContiguous(uint8_t* buffer) const
  {
    size_t offset = 0;
    for(auto& buf : data_chunks_)
    {
      if(buf->num_elts())
      {
        memcpy(buffer + offset, buf->buf(), buf->num_elts());
        offset += buf->num_elts();
      }
    }
    return offset;
  }

  /**
   * @brief Number of layers for this code block
   */
  uint16_t numLayers_ = 0;

  /**
   * @brief Running total of number of passes across multiple layers
   */
  uint8_t totalPasses_ = 0;
  /**
   * @brief Maximum number of passes in this code block
   * This is determined by the code block style i.e. mode switch
   */
  uint8_t maxPasses_ = 0;
  /**
   * @brief Number of passes contributed by layer, calculated by decompress codeblock
   * when parsing packet header
   */
  uint8_t* calculatedPassesInLayer_;

  /**
   * @brief Total number of bytes in segment
   */
  uint32_t totalBytes_ = 0;

  /**
   * @brief Number of bytes signalled by layer
   */
  uint16_t* signalledBytesInLayer_;

  std::vector<Buffer8*> data_chunks_;
};

/**
 * @struct CodeblockDecompressImpl
 * @brief Decompression code block implementation
 */
struct CodeblockDecompressImpl : public CodeblockImpl
{
  /**
   * @brief Constructs a CodeblockDecompressImpl
   * @param numLayers total number of layers for code block
   */
  CodeblockDecompressImpl(uint16_t numLayers)
      : CodeblockImpl(numLayers), numDataParsedSegments_(0), numDecompressedSegments_(0),
        buffers_(nullptr), buffer_lengths_(nullptr), num_buffers_(0), bitPlanesToDecompress_(0),
        passtype_(2), compressDataOffset_(0), passno_(0), needsSegInit_(true),
        needsSegUpdate_(false), dataParsedLayers_(0)
  {}
  /**
   * @brief Destroys a CodeblockDecompressImpl
   */
  ~CodeblockDecompressImpl()
  {
    release();
    delete[] buffers_;
    delete[] buffer_lengths_;
  }
  /**
   * @brief Initializes the code block
   */
  void init()
  {
    CodeblockImpl::init();
  }
  /**
   * @brief Iterator pointing to the beginning
   * of the segments yet to be decompressed.
   * @return std::vector<Segment>::iterator
   */
  std::vector<Segment*>::iterator toBeDecompressedBegin()
  {
    return segs_.begin() + numDecompressedSegments_;
  }
  /**
   * @brief Iterator pointing one location past
   * the last Segment whose data has been parsed
   * @return std::vector<Segment>::iterator
   */
  std::vector<Segment*>::iterator toBeDecompressedEnd()
  {
    return segs_.begin() + numDataParsedSegments_;
  }
  /**
   * @brief Sets number of bit planes to be decompressed
   * @param bps number of bit planes
   */
  void setNumBps(uint8_t bps)
  {
    numbps_ = bps;
    bitPlanesToDecompress_ = bps;
  }
  /**
   * @brief Gets segment for the specified index
   * If index equals number of segments, then a new
   * segment will be added
   * @param pointer to @ref Segment
   */
  Segment* getSegment(uint16_t index)
  {
    if(index == segs_.size())
      segs_.push_back(new Segment(numLayers_));

    return segs_[index];
  }
  /**
   * @brief Reads packet header
   * @param bio shared pointer for @ref BitIO
   * @param signalledDataBytes reference to variable holding number of signalled data bytes
   * @param layno layer number
   * @param cblk_sty code block style
   */
  void readPacketHeader(std::shared_ptr<BitIO> bio, uint32_t& signalledLayerDataBytes,
                        uint16_t layno, uint8_t cblk_sty)
  {
    // 1. read signalled passes in layer
    uint8_t remainingPassesInLayer;
    bio->getnumpasses(&remainingPassesInLayer);
    assert(signalledPassesByLayer_[layno] == 0);
    signalledPassesByLayer_[layno] = remainingPassesInLayer;

    // 2. read signalled length bits
    uint8_t increment = bio->getcommacode();
    setNumLenBits(numlenbits() + increment);

    // 3. prepare to parse segments:
    // create new segment if there are currently none
    // or the current segment has maxed out its passes
    auto seg = currHeaderParsedSegment();
    if(seg == segs_.end() || (*seg)->totalPasses_ == (*seg)->maxPasses_)
      newSegment(cblk_sty);

    // 4. parse all segments in this layer
    do
    {
      // 1. set seg->calculatedPassesInLayer_
      auto seg = segs_.back();
      if(seg->maxPasses_ == maxPassesPerSegmentJ2K)
      {
        /* sanity check when there is no mode switch */
        if(remainingPassesInLayer > maxPassesPerSegmentJ2K)
        {
          grklog.warn("Number of code block passes (%u) in packet is "
                      "suspiciously large.",
                      remainingPassesInLayer);
          throw CorruptPacketHeaderException();
        }
        else
        {
          seg->calculatedPassesInLayer_[layno] = remainingPassesInLayer;
        }
      }
      else
      {
        assert(seg->maxPasses_ >= seg->totalPasses_);
        seg->calculatedPassesInLayer_[layno] = std::min<uint8_t>(
            (uint8_t)(seg->maxPasses_ - seg->totalPasses_), remainingPassesInLayer);
      }
      if(seg->calculatedPassesInLayer_[layno] > remainingPassesInLayer)
      {
        grklog.warn("readHeader: number of segment passes %d in packet"
                    "is greater than total layer passes in packet %d ",
                    seg->calculatedPassesInLayer_[layno], remainingPassesInLayer);
        throw CorruptPacketHeaderException();
      }

      // 2. read signalled number of bytes in this layer for current segment
      uint8_t bits_to_read = numlenbits() + floorlog2(seg->calculatedPassesInLayer_[layno]);
      if(bits_to_read > 16)
      {
        grklog.warn("readPacketHeader: signalled bits (%d) for layer bytes must be <= 16",
                    bits_to_read);
        throw CorruptPacketHeaderException();
      }
      bio->read(seg->signalledBytesInLayer_ + layno, bits_to_read);
      signalledLayerDataBytes += seg->signalledBytesInLayer_[layno];
      assert(remainingPassesInLayer >= seg->calculatedPassesInLayer_[layno]);
      remainingPassesInLayer -= seg->calculatedPassesInLayer_[layno];

      // 3. create next segment if this layer spans multiple segments
      if(remainingPassesInLayer > 0)
        newSegment(cblk_sty);

    } while(remainingPassesInLayer > 0);
  }

  /**
   * @brief Parses packet data based on packet header
   *
   * No data is actually ready, rather the information read from packet header
   * is used to take segment offsets and lengths store them in data chunk array for that
   * segment.
   *
   * As segments can span layers, there may be multiple chunks
   * for different layers.
   *
   * As layers can span segments, there may be multiple segments for a given layer
   *
   * @param layno layer number for this packet
   * @param remainingTilePartBytes remaining tile part bytes (passed by reference)
   * @param isHT true if this is a HTJ2K code block
   * @param layerData pointer to packet's layer data (this is always contiguous)
   * @param layerDataOffset reference to layer data offset
   */
  void parsePacketData(uint16_t layno, size_t& remainingTilePartBytes, bool isHT,
                       uint8_t* layerData, uint32_t& layerDataOffset)
  {
    if(!signalledPassesByLayer_[layno])
      return;

    // 1. prepare to parse data for segments:
    // move to next segment if there are currently no data-parsed segments
    // or the current data-parsed segment has maxed out its passes
    auto dSeg = currDataParsedSegment();
    if(dSeg == segs_.end() || ((*dSeg)->totalPasses_ == (*dSeg)->maxPasses_))
      dSeg = nextDataParsedSegment();
    if(dSeg == segs_.end() || !*dSeg)
      return;

    dataParsedLayers_ = std::max(dataParsedLayers_, (uint16_t)(layno + 1));
    uint8_t signalledPassesInLayer = signalledPassesByLayer_[layno];

    // 2.run through all signalled passes in layer for all code block segments, generating
    // segment buffers as we go
    do
    {
      // 1. parse number of bytes and push segment buffer
      if(((*dSeg)->signalledBytesInLayer_[layno] > remainingTilePartBytes))
      {
        // HT doesn't tolerate truncated code blocks since decoding runs both forward
        // and reverse. So, in this case, we ignore the entire code block
        if(isHT)
          release();
        (*dSeg)->signalledBytesInLayer_[layno] = 0;
        (*dSeg)->totalPasses_ = 0;
        return;
      }
      if((*dSeg)->signalledBytesInLayer_[layno])
      {
        // 1. sanity check on (*seg)->signalledBytesInLayer_
        if(UINT_MAX - (*dSeg)->signalledBytesInLayer_[layno] < (*dSeg)->totalBytes_)
          throw CorruptPacketDataException();

        // 2. correct for truncated packet
        if((*dSeg)->signalledBytesInLayer_[layno] > remainingTilePartBytes)
        {
          grklog.warn("CodeblockDecompress: signalled segment bytes in layer %d exceeds remaining"
                      "tile part bytes %d. Packet is trancated.",
                      (*dSeg)->signalledBytesInLayer_[layno], remainingTilePartBytes);

          (*dSeg)->signalledBytesInLayer_[layno] = (uint16_t)remainingTilePartBytes;
        }

        // 3. push segment buffer
        (*dSeg)->data_chunks_.push_back(new Buffer8(layerData + layerDataOffset,
                                                    (*dSeg)->signalledBytesInLayer_[layno], false));
        layerDataOffset += (*dSeg)->signalledBytesInLayer_[layno];

        // 4. update total bytes in segment
        (*dSeg)->totalBytes_ += (*dSeg)->signalledBytesInLayer_[layno];
        assert(remainingTilePartBytes >= (*dSeg)->signalledBytesInLayer_[layno]);
        remainingTilePartBytes -= (*dSeg)->signalledBytesInLayer_[layno];
      }

      // 2. update total passes in segment
      (*dSeg)->totalPasses_ += (*dSeg)->calculatedPassesInLayer_[layno];
      assert(signalledPassesInLayer >= (*dSeg)->calculatedPassesInLayer_[layno]);
      signalledPassesInLayer -= (*dSeg)->calculatedPassesInLayer_[layno];

      // 3. this layer spans multiple segments, so move to next segment
      if(signalledPassesInLayer > 0)
        dSeg = nextDataParsedSegment();

    } while(dSeg != segs_.end() && *dSeg && signalledPassesInLayer > 0);
  }

  /**
   * @brief Increments iterator for next segment to be decompressed
   * @param segIter reference to std::vector<Segment>::iterator
   */
  void nextToBeDecompressedSegment(std::vector<Segment*>::iterator& s)
  {
    compressDataOffset_ += (*s)->totalBytes_;
    passno_ = 0;
    needsSegInit_ = true;
    s++;
    numDecompressedSegments_++;
  }
#define T1_TYPE_MQ 0 /** Normal coding using entropy coder */
#define T1_TYPE_RAW 1 /** Raw compressing*/

  void prepareBufferList(std::vector<Segment*>::iterator seg)
  {
    delete[] buffers_;
    buffers_ = nullptr;
    delete[] buffer_lengths_;
    buffer_lengths_ = nullptr;
    num_buffers_ = (uint16_t)(*seg)->data_chunks_.size();
    if(num_buffers_)
    {
      buffers_ = new uint8_t*[num_buffers_];
      buffer_lengths_ = new uint32_t[num_buffers_];
      uint16_t i = 0;
      for(auto& bc : (*seg)->data_chunks_)
      {
        buffers_[i] = bc->buf();
        buffer_lengths_[i] = (uint32_t)bc->num_elts();
        i++;
      }
    }
  }

  bool canDecompress(void)
  {
    return bitPlanesToDecompress_ != 0 && numDecompressedSegments_ != numDataParsedSegments_ &&
           !dataChunksEmpty();
  }

  /**
   * @brief Decompresses all layers specified so far
   * @tparam T type of coder
   * @param coder pointer to block decoder
   * @param compressed_data, pointer to compressed data
   * @param orientation code block band's orientation
   * @param cblksty code block style
   * @return true if successful
   */
  template<typename T>
  bool decompress(T* coder, uint8_t orientation, uint32_t cblksty)
  {
    if(bitPlanesToDecompress_ > (int8_t)maxBitPlanesJ2K)
    {
      grk::grklog.error("unsupported number of bit planes: %u > %u", bitPlanesToDecompress_,
                        maxBitPlanesJ2K);
      return false;
    }
    if(!canDecompress())
      return true;

    auto segBegin = segs_.begin();
    auto seg = toBeDecompressedBegin();

    // restore from cache if needed
    coder->decompressRestore(&passno_, &passtype_, &bitPlanesToDecompress_);

    //  IF we have maxed out segment passes
    //  AND this is the final layer so no more passes are possible
    //  OR there are now more data parsed segments
    //  THEN we can deduce that the previous decode reached end of segment
    if(passno_ == (*seg)->totalPasses_ &&
       (dataParsedLayers_ == numLayers_ || seg != segBegin + numDataParsedSegments_ - 1))
      nextToBeDecompressedSegment(seg);

    coder->decompressInitOrientation(orientation);
    auto segEnd = toBeDecompressedEnd();
    while(bitPlanesToDecompress_ > 0 && seg != segEnd)
    {
      /* BYPASS mode */
      uint8_t type = ((cblksty & GRK_CBLKSTY_LAZY) && numbps_ >= 4 &&
                      (bitPlanesToDecompress_ <= ((int8_t)(numbps_)) - 4) && passtype_ < 2)
                         ? T1_TYPE_RAW
                         : T1_TYPE_MQ;

      // if we need a segment init, then there is no point in also performing
      // a segment update. Either way, we must toggle needsSegUpdate_
      // to false, below
      if(needsSegInit_ || needsSegUpdate_)
        prepareBufferList(seg);
      if(needsSegInit_)
      {
        coder->decompressInitSegment(type, buffers_, buffer_lengths_, num_buffers_);
        needsSegInit_ = false;
      }
      else if(needsSegUpdate_)
      {
        coder->decompressUpdateSegment(buffers_, buffer_lengths_, num_buffers_);
      }
      needsSegUpdate_ = false;

      while(passno_ < (*seg)->totalPasses_ && bitPlanesToDecompress_ > 0)
      {
        coder->decompressPass(passno_, passtype_, bitPlanesToDecompress_, type, cblksty);
        if(++passtype_ == 3)
        {
          passtype_ = 0;
          bitPlanesToDecompress_--;
        }
        passno_++;
      }

      // we don't know yet whether this segment has ended
      if(passno_ == (*seg)->totalPasses_ && dataParsedLayers_ != numLayers_ &&
         seg == segBegin + numDataParsedSegments_ - 1)
        break;

      // force end of segment when bitPlanesToDecompress_ reaches zero
      if((passno_ == (*seg)->totalPasses_ || bitPlanesToDecompress_ == 0))
        nextToBeDecompressedSegment(seg);
    }
    coder->decompressFinish(cblksty, dataParsedLayers_ == numLayers_);
    needsSegUpdate_ = true;

    return true;
  }
  /**
   * @brief Gets number of segments whose layer data has been parsed
   * @return number of segments
   */
  uint16_t getNumDataParsedSegments(void)
  {
    return numDataParsedSegments_;
  }
  /**
   * @brief Gets iterator pointing to current segment whose layer data is being parsed
   * @return std::vector<Segment>::iterator
   */
  std::vector<Segment*>::iterator currDataParsedSegment(void)
  {
    return numDataParsedSegments_ ? segs_.begin() + numDataParsedSegments_ - 1 : segs_.end();
  }
  /**
   * @brief Increments to next segment whose data is going to be parsed
   * @return std::vector<Segment>::iterator
   */
  std::vector<Segment*>::iterator nextDataParsedSegment(void)
  {
    numDataParsedSegments_++;
    return currDataParsedSegment();
  }
  /**
   * @brief Gets iterator pointing to current segment being populated by packet header
   * @return std::vector<Segment>::iterator
   */
  std::vector<Segment*>::iterator currHeaderParsedSegment(void)
  {
    return segs_.empty() ? segs_.end() : segs_.end() - 1;
  }

  /**
   * @brief Checks whether all segments' data chunks are empty
   * @return true if there aren't any data chunks in any segment
   */
  bool dataChunksEmpty()
  {
    for(const auto& segment : segs_)
    {
      if(!segment->data_chunks_.empty())
      {
        return false;
      }
    }
    return true;
  }

  /**
   * @brief Gets combined length of all data chunks across all uncompressed segments
   * whose datg has been parsed
   * @return total length
   */
  size_t getDataChunksLength() const
  {
    size_t total_length = 0;
    for(auto s = segs_.begin(); s != segs_.end(); ++s)
      total_length += (*s)->getDataChunksLength();

    return total_length;
  }

  /**
   * @brief Copies all segment data chunk buffers for all uncompressed segments
   * whose data has been parsed, into a single contiguous buffer
   * @param buffer contiguous buffer
   * @return true if successful
   */
  bool copyDataChunksToContiguous(uint8_t* buffer) const
  {
    if(!buffer)
      return false;

    size_t offset = 0;
    for(auto s = segs_.begin(); s != segs_.end(); ++s)
      offset += (*s)->copyDataChunksToContiguous(buffer + offset);

    return true;
  }

private:
  /**
   * @brief Releases resources
   */
  void release()
  {
    for(auto s : segs_)
    {
      s->clear();
      delete s;
    }
    segs_.clear();
    numDataParsedSegments_ = 0;
    numDecompressedSegments_ = 0;
  }

  /**
   * @brief Creates a new @ref Segment
   * @param cblk_sty code block stule
   *
   */
  void newSegment(uint8_t cblk_sty)
  {
    segs_.push_back(new Segment(numLayers_));
    auto seg = segs_.back();
    if(cblk_sty & GRK_CBLKSTY_TERMALL)
    {
      seg->maxPasses_ = 1;
    }
    else if(cblk_sty & GRK_CBLKSTY_LAZY)
    {
      // first segment
      if(segs_.size() == 1)
      {
        seg->maxPasses_ = 10;
      }
      else
      {
        auto prev_segment = segs_.end() - 2;
        seg->maxPasses_ =
            (((*prev_segment)->maxPasses_ == 1) || ((*prev_segment)->maxPasses_ == 10)) ? 2 : 1;
      }
    }
    else
    {
      seg->maxPasses_ = maxPassesPerSegmentJ2K;
    }
  }

  /**
   * @brief Number of segments whose data has been
   * read from their respective layers
   *
   * Layers whose data has been read always form a
   * contiguous set - there are never any layers whose data is
   * skipped in the middle
   *
   */
  uint8_t numDataParsedSegments_;
  /**
   * @brief Number of decompressed segments
   */
  uint8_t numDecompressedSegments_;
  /**
   * @brief Collection of @ref Segment
   */
  std::vector<Segment*> segs_;

  /**
   * @brief Array of pointers to buffers
   */
  uint8_t** buffers_;

  /**
   * @brief Array of buffer lengths
   */
  uint32_t* buffer_lengths_;

  /**
   * @brief Number of buffers
   */
  uint16_t num_buffers_;

  /**
   * @brief Remaining bit planes to decompress
   */
  uint8_t bitPlanesToDecompress_;
  /**
   * @brief Type of pass: cleanup, magnitude refinement or significance propagation
   */
  uint8_t passtype_;
  /**
   * @brief offset into contiguous buffer of compressed data
   */
  uint32_t compressDataOffset_;
  /**
   * @brief decompression: current pass number
   */
  uint8_t passno_;
  /**
   * @brief decompression: segment needs initialization
   */
  bool needsSegInit_;

  /**
   * @brief decompression: segment needs update, as more packets have been parsed
   */
  bool needsSegUpdate_;
  /**
   * @brief number of layers whose data has been parsed
   */
  uint16_t dataParsedLayers_;
};

} // namespace grk
