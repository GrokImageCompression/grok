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

/**
 * @struct Marker
 * @brief Stores individual marker information
 */
struct Marker
{
  /**
   * @brief Constructs a Marker
   * @param id identifier
   * @param pos position in code stream
   * @param len length of marker (marker id included)
   */
  Marker(uint16_t id = 0, uint64_t pos = 0, uint16_t len = 0) : id_(id), pos_(pos), len_(len) {}

  /**
   * @brief serializes to disk
   * @param outputFileStream FILE to serialize to
   */
  void dump(FILE* outputFileStream)
  {
    fprintf(outputFileStream, "\t\t type=%#x, pos=%" PRIu64 ", len=%u\n", id_, pos_, len_);
  }

  /**
   * @brief marker id
   */
  uint16_t id_;

  /**
   * @brief position in code stream
   */
  uint64_t pos_;

  /**
   * @brief marker length (marker id included)
   */
  uint16_t len_;
};

/**
 * @struct MarkerCache
 * @brief Stores markers
 */
struct MarkerCache
{
  MarkerCache();
  /**
   * @brief Destroys a MarkerCache
   */
  virtual ~MarkerCache() = default;

  /**
   * @brief Serializes markers to disk
   * @param out FILE to serialize to
   */
  void dump(FILE* out);

  /**
   * @brief Adds a marker to the cache
   * @param id marker id
   * @param pos marker position in code stream
   * @param len marker length
   */
  void add(uint16_t id, uint64_t pos, uint16_t len);

  /**
   * @brief Gets start of tile stream
   * @return start of tile stream
   */
  uint64_t getTileStreamStart();

  /**
   * @brief Sets start of tile stream
   * @param start start of tile stream
   */
  void setTileStreamStart(uint64_t start);

private:
  /**
   * @brief main header start position (SOC position)
   */
  uint64_t mainHeaderStart_;

  /**
   * @brief start of tile stream
   */
  uint64_t tileStreamStart_;

  /**
   * @brief collection of @ref Marker
   */
  std::vector<std::unique_ptr<Marker>> markers_;
};

} // namespace grk