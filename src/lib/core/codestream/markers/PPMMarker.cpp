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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_includes.h"

namespace grk
{
PPMMarker::PPMMarker() : markers_count(0), markers(nullptr), buffer(nullptr) {}

PPMMarker::~PPMMarker()
{
   if(markers)
   {
	  for(uint32_t i = 0U; i < markers_count; ++i)
		 grk_free(markers[i].data_);
	  markers_count = 0U;
	  grk_free(markers);
   }
   delete[] buffer;
}

bool PPMMarker::read(uint8_t* headerData, uint16_t header_size)
{
   uint8_t i_ppm;
   assert(headerData != nullptr);

   /* We need to have the i_ppm element + 1 byte of Nppm/Ippm at minimum */
   if(header_size < 2)
   {
	  Logger::logger_.error("Error reading PPM marker");
	  return false;
   }

   /* i_ppm */
   grk_read(headerData++, &i_ppm);
   --header_size;

   /* check allocation needed */
   if(!markers)
   { /* first PPM marker */
	  uint32_t newCount = i_ppm + 1U;
	  assert(markers_count == 0U);

	  markers = (grk_ppx*)grk_calloc(newCount, sizeof(grk_ppx));
	  if(markers == nullptr)
	  {
		 Logger::logger_.error("Not enough memory to read PPM marker");
		 return false;
	  }
	  markers_count = newCount;
   }
   else if(markers_count <= i_ppm)
   {
	  uint32_t newCount = i_ppm + 1U;
	  auto new_ppm_markers = (grk_ppx*)grk_realloc(markers, newCount * sizeof(grk_ppx));
	  if(new_ppm_markers == nullptr)
	  {
		 Logger::logger_.error("Not enough memory to read PPM marker");
		 return false;
	  }
	  markers = new_ppm_markers;
	  memset(markers + markers_count, 0, (newCount - markers_count) * sizeof(grk_ppx));
	  markers_count = newCount;
   }

   if(markers[i_ppm].data_ != nullptr)
   {
	  Logger::logger_.error("ippm %u already read", i_ppm);
	  return false;
   }

   markers[i_ppm].data_ = (uint8_t*)grk_malloc(header_size);
   if(markers[i_ppm].data_ == nullptr)
   {
	  Logger::logger_.error("Not enough memory to read PPM marker");
	  return false;
   }
   markers[i_ppm].data_size_ = header_size;
   memcpy(markers[i_ppm].data_, headerData, header_size);

   return true;
}
bool PPMMarker::merge()
{
   assert(buffer == nullptr);
   if(!markers)
	  return true;

   uint64_t total_data_size = 0U;
   uint32_t N_ppm_remaining = 0U;
   for(uint32_t i = 0U; i < markers_count; ++i)
   {
	  if(markers[i].data_ != nullptr)
	  { /* standard doesn't seem to require contiguous Zppm */
		 uint32_t bytes_remaining = markers[i].data_size_;
		 const uint8_t* data = markers[i].data_;

		 if(N_ppm_remaining >= bytes_remaining)
		 {
			N_ppm_remaining -= bytes_remaining;
			bytes_remaining = 0U;
		 }
		 else
		 {
			data += N_ppm_remaining;
			bytes_remaining -= N_ppm_remaining;
			N_ppm_remaining = 0U;
		 }
		 if(bytes_remaining > 0U)
		 {
			do
			{
			   /* read Nppm */
			   if(bytes_remaining < 4U)
			   {
				  Logger::logger_.error("Not enough bytes to read Nppm");
				  return false;
			   }
			   uint32_t N_ppm;
			   grk_read(data, &N_ppm);
			   data += 4;
			   bytes_remaining -= 4;
			   packetHeaders.push_back(grk_buf8(nullptr, total_data_size, N_ppm, false));
			   total_data_size += N_ppm;
			   if(bytes_remaining >= N_ppm)
			   {
				  bytes_remaining -= N_ppm;
				  data += N_ppm;
			   }
			   else
			   {
				  N_ppm_remaining = N_ppm - bytes_remaining;
				  bytes_remaining = 0U;
			   }
			} while(bytes_remaining > 0U);
		 }
	  }
   }
   if(N_ppm_remaining != 0U)
   {
	  Logger::logger_.error("Corrupted PPM markers");
	  return false;
   }
   buffer = new uint8_t[total_data_size];
   for(auto& b : packetHeaders)
   {
	  b.buf = buffer + b.offset;
	  b.offset = 0;
   }

   total_data_size = 0U;
   N_ppm_remaining = 0U;
   for(uint32_t i = 0U; i < markers_count; ++i)
   {
	  if(markers[i].data_)
	  { /* standard doesn't seem to require contiguous Zppm */
		 uint32_t bytes_remaining = markers[i].data_size_;
		 const uint8_t* data = markers[i].data_;

		 if(N_ppm_remaining >= bytes_remaining)
		 {
			memcpy(buffer + total_data_size, data, bytes_remaining);
			total_data_size += bytes_remaining;
			N_ppm_remaining -= bytes_remaining;
			bytes_remaining = 0U;
		 }
		 else
		 {
			memcpy(buffer + total_data_size, data, N_ppm_remaining);
			total_data_size += N_ppm_remaining;
			data += N_ppm_remaining;
			bytes_remaining -= N_ppm_remaining;
			N_ppm_remaining = 0U;
		 }

		 if(bytes_remaining > 0U)
		 {
			do
			{
			   /* read Nppm */
			   if(bytes_remaining < 4U)
			   {
				  Logger::logger_.error("Not enough bytes to read Nppm");
				  return false;
			   }
			   uint32_t N_ppm;
			   grk_read(data, &N_ppm);
			   data += 4;
			   bytes_remaining -= 4;

			   if(bytes_remaining >= N_ppm)
			   {
				  memcpy(buffer + total_data_size, data, N_ppm);
				  total_data_size += N_ppm;
				  bytes_remaining -= N_ppm;
				  data += N_ppm;
			   }
			   else
			   {
				  memcpy(buffer + total_data_size, data, bytes_remaining);
				  total_data_size += bytes_remaining;
				  N_ppm_remaining = N_ppm - bytes_remaining;
				  bytes_remaining = 0U;
			   }
			} while(bytes_remaining > 0U);
		 }
		 grk_free(markers[i].data_);
		 markers[i].data_ = nullptr;
		 markers[i].data_size_ = 0U;
	  }
   }
   markers_count = 0U;
   grk_free(markers);
   markers = nullptr;

   return true;
}

} /* namespace grk */
