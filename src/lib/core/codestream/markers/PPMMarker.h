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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once

#include <vector>

namespace grk
{
struct grk_ppx
{
   uint8_t* data_; /* data_ == nullptr => Zppx not read yet */
   uint32_t data_size_;
};

class PPMMarker
{
 public:
   PPMMarker();
   ~PPMMarker();

   /**
	* Read a PPM marker (Packed headers, main header)
	*
	* @param       headerData   the data contained in the POC box.
	* @param       header_size   the size of the data contained in the POC marker.

	*/
   bool read(uint8_t* headerData, uint16_t header_size);

   /**
	* Merges all PPM markers read (Packed headers, main header)
	*
	*/
   bool merge(void);

   std::vector<grk_buf8> packetHeaders;

 private:
   /** number of ppm markers (reserved size) */
   uint32_t markers_count;
   /** ppm markers data (table indexed by Zppm) */
   grk_ppx* markers;

   /** packet header storage original buffer */
   uint8_t* buffer;
};

} /* namespace grk */
