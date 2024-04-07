/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
 *
 */

#pragma once

namespace grk
{
struct TileProcessor;

/**
 Tier-2 decoding
 */
struct T2Decompress
{
   T2Decompress(TileProcessor* tileProc);
   virtual ~T2Decompress(void) = default;
   void decompressPackets(uint16_t tileno, SparseBuffer* src, bool* truncated);

 private:
   TileProcessor* tileProcessor;
   void decompressPacket(PacketParser* parser, bool skipData);
   bool processPacket(uint16_t compno, uint8_t resno, uint64_t precinctIndex, uint16_t layno,
					  SparseBuffer* src);
   void readPacketData(Resolution* res, PacketParser* parser, uint64_t precinctIndex, bool defer);
};

} // namespace grk
