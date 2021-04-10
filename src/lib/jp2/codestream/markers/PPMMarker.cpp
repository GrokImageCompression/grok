/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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
	if(markers != nullptr)
	{
		for(uint32_t i = 0U; i < markers_count; ++i)
		{
			if(markers[i].m_data != nullptr)
				grkFree(markers[i].m_data);
		}
		markers_count = 0U;
		grkFree(markers);
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
		GRK_ERROR("Error reading PPM marker");
		return false;
	}

	/* i_ppm */
	grk_read<uint8_t>(headerData++, &i_ppm);
	--header_size;

	/* check allocation needed */
	if(!markers)
	{ /* first PPM marker */
		uint32_t newCount = i_ppm + 1U;
		assert(markers_count == 0U);

		markers = (grk_ppx*)grkCalloc(newCount, sizeof(grk_ppx));
		if(markers == nullptr)
		{
			GRK_ERROR("Not enough memory to read PPM marker");
			return false;
		}
		markers_count = newCount;
	}
	else if(markers_count <= i_ppm)
	{
		uint32_t newCount = i_ppm + 1U;
		auto new_ppm_markers = (grk_ppx*)grkRealloc(markers, newCount * sizeof(grk_ppx));
		if(new_ppm_markers == nullptr)
		{
			/* clean up to be done on cp destruction */
			GRK_ERROR("Not enough memory to read PPM marker");
			return false;
		}
		markers = new_ppm_markers;
		memset(markers + markers_count, 0, (newCount - markers_count) * sizeof(grk_ppx));
		markers_count = newCount;
	}

	if(markers[i_ppm].m_data != nullptr)
	{
		/* clean up to be done on cp destruction */
		GRK_ERROR("ippm %u already read", i_ppm);
		return false;
	}

	markers[i_ppm].m_data = (uint8_t*)grkMalloc(header_size);
	if(markers[i_ppm].m_data == nullptr)
	{
		/* clean up to be done on cp destruction */
		GRK_ERROR("Not enough memory to read PPM marker");
		return false;
	}
	markers[i_ppm].m_data_size = header_size;
	memcpy(markers[i_ppm].m_data, headerData, header_size);

	return true;
}
bool PPMMarker::merge()
{
	assert(buffer == nullptr);
	if(!markers)
		return true;

	uint32_t total_data_size = 0U;
	uint32_t N_ppm_remaining = 0U;
	for(uint32_t i = 0U; i < markers_count; ++i)
	{
		if(markers[i].m_data != nullptr)
		{ /* standard doesn't seem to require contiguous Zppm */
			uint32_t data_size = markers[i].m_data_size;
			const uint8_t* data = markers[i].m_data;

			if(N_ppm_remaining >= data_size)
			{
				N_ppm_remaining -= data_size;
				data_size = 0U;
			}
			else
			{
				data += N_ppm_remaining;
				data_size -= N_ppm_remaining;
				N_ppm_remaining = 0U;
			}
			if(data_size > 0U)
			{
				do
				{
					/* read Nppm */
					if(data_size < 4U)
					{
						/* clean up to be done on cp destruction */
						GRK_ERROR("Not enough bytes to read Nppm");
						return false;
					}
					uint32_t N_ppm;
					grk_read<uint32_t>(data, &N_ppm);
					data += 4;
					data_size -= 4;
					m_tile_packet_headers.push_back(
						grkBufferU8(nullptr, total_data_size, N_ppm, false));
					total_data_size += N_ppm; /* can't overflow, max 256 markers of max 65536 bytes,
												 that is when PPM markers are not corrupted
												  which is checked elsewhere */
					if(data_size >= N_ppm)
					{
						data_size -= N_ppm;
						data += N_ppm;
					}
					else
					{
						N_ppm_remaining = N_ppm - data_size;
						data_size = 0U;
					}
				} while(data_size > 0U);
			}
		}
	}
	if(N_ppm_remaining != 0U)
	{
		/* clean up to be done on cp destruction */
		GRK_ERROR("Corrupted PPM markers");
		return false;
	}
	buffer = new uint8_t[total_data_size];
	for(auto& b : m_tile_packet_headers)
	{
		b.buf = buffer + b.offset;
		b.offset = 0;
	}

	total_data_size = 0U;
	N_ppm_remaining = 0U;
	for(uint32_t i = 0U; i < markers_count; ++i)
	{
		if(markers[i].m_data != nullptr)
		{ /* standard doesn't seem to require contiguous Zppm */
			uint32_t data_size = markers[i].m_data_size;
			const uint8_t* data = markers[i].m_data;

			if(N_ppm_remaining >= data_size)
			{
				memcpy(buffer + total_data_size, data, data_size);
				total_data_size += data_size;
				N_ppm_remaining -= data_size;
				data_size = 0U;
			}
			else
			{
				memcpy(buffer + total_data_size, data, N_ppm_remaining);
				total_data_size += N_ppm_remaining;
				data += N_ppm_remaining;
				data_size -= N_ppm_remaining;
				N_ppm_remaining = 0U;
			}

			if(data_size > 0U)
			{
				do
				{
					/* read Nppm */
					if(data_size < 4U)
					{
						/* clean up to be done on cp destruction */
						GRK_ERROR("Not enough bytes to read Nppm");
						return false;
					}
					uint32_t N_ppm;
					grk_read<uint32_t>(data, &N_ppm);
					data += 4;
					data_size -= 4;

					if(data_size >= N_ppm)
					{
						memcpy(buffer + total_data_size, data, N_ppm);
						total_data_size += N_ppm;
						data_size -= N_ppm;
						data += N_ppm;
					}
					else
					{
						memcpy(buffer + total_data_size, data, data_size);
						total_data_size += data_size;
						N_ppm_remaining = N_ppm - data_size;
						data_size = 0U;
					}
				} while(data_size > 0U);
			}
			grkFree(markers[i].m_data);
			markers[i].m_data = nullptr;
			markers[i].m_data_size = 0U;
		}
	}
	markers_count = 0U;
	grkFree(markers);
	markers = nullptr;

	return true;
}

} /* namespace grk */
