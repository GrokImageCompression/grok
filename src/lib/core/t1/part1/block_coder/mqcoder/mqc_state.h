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

#include <cstdint>

namespace grk::t1
{

/**
 * @struct mq_state
 * @brief Stores current MQ coder state
 */
struct mqc_state
{
  /**
   * @brief Probability of the Least Probable Symbol (0.75->0x8000, 1.5->0xffff)
   */
  uint32_t qeval;
  /**
   *  @brief Most Probable Symbol (0 or 1)
   */
  uint8_t mps;
  /**
   *  @brief Next state if next encoded symbol is MPS
   */
  const mqc_state* nmps;
  /**
   *  @brief Next state if next encoded symbol is LPS
   */
  const mqc_state* nlps;

  bool operator==(const mqc_state& other) const
  {
    return qeval == other.qeval && mps == other.mps && nmps == other.nmps && nlps == other.nlps;
  }
};

extern const mqc_state mqc_states[47 * 2];

} // namespace grk::t1