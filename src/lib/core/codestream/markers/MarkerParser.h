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

namespace grk
{

typedef std::function<bool(uint8_t* headerData, uint16_t headerSize)> MARKER_CALLBACK;

struct IMarkerProcessor
{
  IMarkerProcessor(uint16_t ID);
  virtual ~IMarkerProcessor() = default;

  virtual bool process(uint8_t* headerData, uint16_t headerSize) const = 0;
  uint16_t id;
};

struct MarkerProcessor : public IMarkerProcessor
{
  MarkerProcessor(uint16_t ID, MARKER_CALLBACK f);
  bool process(uint8_t* headerData, uint16_t headerSize) const override;

private:
  MARKER_CALLBACK callback_;
};

struct MarkerScratch
{
  MarkerScratch(void);
  ~MarkerScratch(void);
  bool process(const IMarkerProcessor* handler, uint16_t markerSize);
  void setStream(IStream* stream);

private:
  uint8_t* buff_ = nullptr;
  uint16_t len_ = 0;
  IStream* stream_ = nullptr;
};

struct MarkerParser
{
  MarkerParser() = default;
  ~MarkerParser();
  void add(const uint16_t id, IMarkerProcessor* processor);
  void add(const std::initializer_list<std::pair<const uint16_t, IMarkerProcessor*>>& newMarkers);
  void clearProcessors(void);
  void setStream(IStream* stream, bool ownsStream);

  IMarkerProcessor* currentProcessor(void);
  uint16_t currId(void);
  void synch(uint16_t markerId);

  /**
   * @brief Check for corrupt images with extra tile parts
   *
   * @return true if image is corrupt
   */
  bool checkForIllegalTilePart(void);
  void setSOT(void);

  /**
   * @brief Reads next marker, which should be either SOT or EOC
   *
   * @return true if SOT or EOC was read
   */
  bool readSOTorEOC(void);

  /**
   * @brief Checks if end of code stream has been reached
   *
   * @return true if end of code stream has been reached
   */
  bool endOfCodeStream(void);

  /**
   * @brief Reads next SOT after SOD
   * The marker could also be EOC
   *
   * @return true if successful
   */
  bool readSOTafterSOD(void);

  bool readId(bool suppressWarning);
  std::pair<bool, uint16_t> processMarker();
  static bool readShort(IStream* stream, uint16_t* val);
  bool process(const IMarkerProcessor* processor, uint16_t markerBodyLength);

  IStream* getStream();

private:
  static std::string markerString(uint16_t marker);

  std::unordered_map<uint16_t, IMarkerProcessor*> processors_;
  uint16_t currMarkerId_ = 0;
  IStream* stream_ = nullptr;
  bool ownsStream_ = false;
  MarkerScratch scratch_;
  bool foundEOC_ = false;
};

} // namespace grk
