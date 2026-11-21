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

// packed state: qeval in bits 16 to 31, mqc_states index in bits 0 to 6, mps is the index's low bit
extern uint32_t mqc_packed_next[2][47 * 2];

inline uint32_t mqc_pack(const mqc_state* state)
{
  return (state->qeval << 16) | (uint32_t)(state - mqc_states);
}
inline uint32_t mqc_qeval(uint32_t state)
{
  return state >> 16;
}
inline uint8_t mqc_mps(uint32_t state)
{
  return (uint8_t)(state & 1);
}
inline uint32_t mqc_next(uint32_t state, uint32_t lps)
{
  return mqc_packed_next[lps][state & 0x7f];
}

} // namespace grk::t1