/*
*    Copyright (C) 2016-2018 Grok Image Compression Inc.
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
*    This source code incorporates work covered by the following copyright and
*    permission notice:
*
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2003-2007, Francois-Olivier Devaux
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2008, 2011-2012, Centre National d'Etudes Spatiales (CNES), FR
 * Copyright (c) 2012, CS Systemes d'Information, France
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "grok_includes.h"




namespace grk {

template<typename TYPE> void grok_write(uint8_t * p_buffer, TYPE p_value, uint32_t p_nb_bytes);
template<typename TYPE> void grok_read(const uint8_t* p_buffer, TYPE* p_value, uint32_t p_nb_bytes);

GrokStream::GrokStream(size_t p_buffer_size, bool l_is_input) : m_user_data(nullptr),
	m_free_user_data_fn(nullptr),
	m_user_data_length(0),
	m_read_fn(nullptr),
	m_zero_copy_read_fn(nullptr),
	m_write_fn(nullptr),
	m_seek_fn(nullptr),
	m_status(0),
	m_buffer(nullptr),
	m_buffer_size(0),
	m_stream_offset(0),
	m_buffer_current_ptr(nullptr),
	m_bytes_in_buffer(0),
	isBufferStream(false)
{

	m_buffer_size = p_buffer_size;
	m_buffer = new uint8_t[p_buffer_size];
	m_buffer_current_ptr = m_buffer;


	if (l_is_input) {
		m_status |= GROK_STREAM_STATUS_INPUT;
	}
	else {
		m_status |= GROK_STREAM_STATUS_OUTPUT;
	}
}

GrokStream::GrokStream(uint8_t* buffer, size_t p_buffer_size, bool l_is_input) : m_user_data(nullptr),
																m_free_user_data_fn(nullptr),
																m_user_data_length(0),
																m_read_fn(nullptr),
																m_zero_copy_read_fn(nullptr),
																m_write_fn(nullptr),
																m_seek_fn(nullptr),
																m_status(0),
																m_buffer(nullptr),
																m_buffer_size(0),
																m_stream_offset(0),
																m_buffer_current_ptr(nullptr),
																m_bytes_in_buffer(0),
																isBufferStream(true)
{

	m_buffer_size = p_buffer_size;
	m_buffer = buffer;
	m_buffer_current_ptr = buffer;
	if (l_is_input) {
		m_status |= GROK_STREAM_STATUS_INPUT;
	}
	else {
		m_status |= GROK_STREAM_STATUS_OUTPUT;
	}
}

GrokStream::~GrokStream() {
	if (m_free_user_data_fn) {
		m_free_user_data_fn(m_user_data);
	}
	if (!isBufferStream) {
		delete[] m_buffer;
	}
}

//note: passing in nullptr for p_buffer will execute a zero-copy read
size_t GrokStream::read(uint8_t * p_buffer,
	size_t p_size,
	event_mgr_t * p_event_mgr)
{
	ARG_NOT_USED(p_event_mgr);
	if (!p_buffer && !supportsZeroCopy())
		throw new std::exception();
	size_t l_read_nb_bytes = 0;
	if (m_bytes_in_buffer >= p_size) {
		if (p_buffer)
			memcpy(p_buffer, m_buffer_current_ptr, p_size);
		m_buffer_current_ptr += p_size;
		m_bytes_in_buffer -= p_size;
		l_read_nb_bytes += p_size;
		m_stream_offset += p_size;
		return l_read_nb_bytes;
	}

	/* if we get here, the remaining data in buffer is not sufficient */
	if (m_status & GROK_STREAM_STATUS_END) {
		l_read_nb_bytes += m_bytes_in_buffer;
		if (p_buffer)
			memcpy(p_buffer, m_buffer_current_ptr, m_bytes_in_buffer);
		m_stream_offset += m_bytes_in_buffer;
		m_buffer_current_ptr += m_bytes_in_buffer;
		m_bytes_in_buffer = 0;
		return l_read_nb_bytes ? l_read_nb_bytes : (size_t)-1;
	}

	/* the flag is not set, we copy data and then do an actual read on the stream */
	if (m_bytes_in_buffer) {
		l_read_nb_bytes += m_bytes_in_buffer;
		if (p_buffer)
			memcpy(p_buffer, m_buffer_current_ptr, m_bytes_in_buffer);
		p_buffer += m_bytes_in_buffer;
		p_size -= m_bytes_in_buffer;
		m_stream_offset += m_bytes_in_buffer;
		m_buffer_current_ptr = m_buffer;
		m_bytes_in_buffer = 0;
	}
	else {
		/* case where we are already at the end of the buffer
			so reset the m_buffer_current_ptr to point to the start of the
			stored buffer to get ready to read from disk*/
		m_buffer_current_ptr = m_buffer;
	}

	for (;;) {
		/* we should read less than a chunk -> read a chunk */
		if (p_size < m_buffer_size) {
			/* we should do an actual read on the media */
			m_bytes_in_buffer = m_read_fn(m_buffer, m_buffer_size, m_user_data);

			if (m_bytes_in_buffer == (size_t)-1) {
				m_bytes_in_buffer = 0;
				m_status |= GROK_STREAM_STATUS_END;
				/* end of stream */
				return l_read_nb_bytes ? l_read_nb_bytes : (size_t)-1;
			}
			else if (m_bytes_in_buffer < p_size) {
				/* not enough data */
				l_read_nb_bytes += m_bytes_in_buffer;
				if (p_buffer)
					memcpy(p_buffer, m_buffer_current_ptr, m_bytes_in_buffer);
				p_buffer += m_bytes_in_buffer;
				p_size -= m_bytes_in_buffer;
				m_stream_offset += m_bytes_in_buffer;
				m_buffer_current_ptr = m_buffer;
				m_bytes_in_buffer = 0;
			}
			else {
				l_read_nb_bytes += p_size;
				if (p_buffer)
					memcpy(p_buffer, m_buffer_current_ptr, p_size);
				m_buffer_current_ptr += p_size;
				m_bytes_in_buffer -= p_size;
				m_stream_offset += p_size;
				return l_read_nb_bytes;
			}
		}
		else {
			/* direct read on the dest buffer */
			m_bytes_in_buffer = m_read_fn(p_buffer, p_size, m_user_data);

			if (m_bytes_in_buffer == (size_t)-1) {
				m_bytes_in_buffer = 0;
				m_status |= GROK_STREAM_STATUS_END;
				/* end of stream */
				return l_read_nb_bytes ? l_read_nb_bytes : (size_t)-1;
			}
			else if (m_bytes_in_buffer < p_size) {
				/* not enough data */
				l_read_nb_bytes += m_bytes_in_buffer;
				if (p_buffer)
					p_buffer += m_bytes_in_buffer;
				p_size -= m_bytes_in_buffer;
				m_stream_offset += m_bytes_in_buffer;
				m_buffer_current_ptr = m_buffer;
				m_bytes_in_buffer = 0;
			}
			else {
				/* we have read the exact size */
				l_read_nb_bytes += m_bytes_in_buffer;
				m_stream_offset += m_bytes_in_buffer;
				m_buffer_current_ptr = m_buffer;
				m_bytes_in_buffer = 0;
				return l_read_nb_bytes;
			}
		}
	}
}

size_t GrokStream::read_data_zero_copy(uint8_t ** p_buffer,
	size_t p_size,
	event_mgr_t * p_event_mgr)
{
	ARG_NOT_USED(p_event_mgr);
	size_t l_read_nb_bytes = m_zero_copy_read_fn((void**)p_buffer, p_size, m_user_data);

	if (l_read_nb_bytes == (size_t)-1) {
		m_status |= GROK_STREAM_STATUS_END;
		return (size_t)-1;
	}
	else {
		m_stream_offset += l_read_nb_bytes;
		return l_read_nb_bytes;
	}

}

bool GrokStream::write_byte(uint8_t p_value, event_mgr_t * p_event_mgr) {
	return write_bytes(&p_value, 1, p_event_mgr) == 1;
}

bool GrokStream::write_short(uint16_t p_value, event_mgr_t * p_event_mgr) {
	return write<uint16_t>(p_value, sizeof(uint16_t), p_event_mgr);
}

bool GrokStream::write_24(uint32_t p_value, event_mgr_t * p_event_mgr) {
	return write<uint32_t>(p_value, 3, p_event_mgr);
}

bool GrokStream::write_int(uint32_t p_value, event_mgr_t * p_event_mgr) {
	return write<uint32_t>(p_value, sizeof(uint32_t), p_event_mgr);
}

bool GrokStream::write_64(uint64_t p_value, event_mgr_t * p_event_mgr) {
	return write<uint64_t>(p_value, sizeof(uint64_t), p_event_mgr);
}

template<typename TYPE> bool GrokStream::write(TYPE p_value, uint8_t numBytes, event_mgr_t * p_event_mgr) {

	if (m_status & GROK_STREAM_STATUS_ERROR) {
		return false;
	}
	if (numBytes > sizeof(TYPE))
		return false;

	// handle case where there is no internal buffer (buffer stream)
	if (isBufferStream) {
		// skip first to make sure that we are not at the end of the stream
		if (!m_seek_fn(m_stream_offset + numBytes, m_user_data))
			return false;
		grok_write(m_buffer_current_ptr, p_value, numBytes);
		write_increment(numBytes);
		return true;
	}
	size_t l_remaining_bytes = m_buffer_size - m_bytes_in_buffer;
	if (l_remaining_bytes < numBytes) {
		if (!flush(p_event_mgr)) {
			return false;
		}
	}
	grok_write(m_buffer_current_ptr, p_value, numBytes);
	write_increment(numBytes);
	return true;
}


size_t GrokStream::write_bytes(const uint8_t * p_buffer,
	size_t p_size,
	event_mgr_t * p_event_mgr)
{
	if (m_status & GROK_STREAM_STATUS_ERROR) {
		return (size_t)-1;
	}
	// handle case where there is no internal buffer (buffer stream)
	if (isBufferStream) {
		/* we should do an actual write on the media */
		auto l_current_write_nb_bytes = m_write_fn((uint8_t*)p_buffer,
			p_size,
			m_user_data);
		write_increment(l_current_write_nb_bytes);
		return l_current_write_nb_bytes;
	}
	size_t l_write_nb_bytes = 0;
	for (;;) {
		size_t l_remaining_bytes = m_buffer_size - m_bytes_in_buffer;

		/* we have more memory than required */
		if (l_remaining_bytes >= p_size) {
			l_write_nb_bytes += p_size;
			memcpy(m_buffer_current_ptr, p_buffer, p_size);
			write_increment(p_size);
			return l_write_nb_bytes;
		}

		/* we copy part of data (if possible) and flush the stream */
		if (l_remaining_bytes) {
			l_write_nb_bytes += l_remaining_bytes;
			memcpy(m_buffer_current_ptr, p_buffer, l_remaining_bytes);
			m_buffer_current_ptr = m_buffer;
			m_bytes_in_buffer += l_remaining_bytes;
			m_stream_offset += l_remaining_bytes;
			p_buffer += l_remaining_bytes;
			p_size -= l_remaining_bytes;
		}
		if (!flush(p_event_mgr)) {
			return (size_t)-1;
		}
	}
}
void GrokStream::write_increment(size_t p_size) {
	m_buffer_current_ptr += p_size;
	if (!isBufferStream)
		m_bytes_in_buffer += p_size;
	else
		assert(m_bytes_in_buffer == 0);
	m_stream_offset += p_size;
}

// force write of any remaining bytes from double buffer
bool GrokStream::flush(event_mgr_t * p_event_mgr)
{
	if (isBufferStream) {
		return true;
	}
	/* the number of bytes written on the media. */
	size_t l_current_write_nb_bytes = 0;
	m_buffer_current_ptr = m_buffer;
	while (m_bytes_in_buffer) {
		/* we should do an actual write on the media */
		l_current_write_nb_bytes = m_write_fn(m_buffer_current_ptr,
			m_bytes_in_buffer,
			m_user_data);

		if (l_current_write_nb_bytes == (size_t)-1) {
			m_status |= GROK_STREAM_STATUS_ERROR;
			if (p_event_mgr)
				event_msg(p_event_mgr, EVT_ERROR, "Error on writing stream!\n");
			return false;
		}
		m_buffer_current_ptr += l_current_write_nb_bytes;
		m_bytes_in_buffer -= l_current_write_nb_bytes;
	}
	m_buffer_current_ptr = m_buffer;
	return true;
}

bool GrokStream::read_skip(int64_t p_size,
							event_mgr_t * p_event_mgr)
{
	int64_t offset = m_stream_offset + p_size;
	if (offset < 0)
		return false;
	return read_seek(offset, p_event_mgr);
}

bool GrokStream::write_skip(int64_t p_size,
	event_mgr_t * p_event_mgr)
{
	int64_t offset = m_stream_offset + p_size;
	if (offset < 0)
		return false;
	return write_seek(offset, p_event_mgr);
}

uint64_t GrokStream::tell() {
	return m_stream_offset;
}

int64_t GrokStream::get_number_byte_left(void)
{
	assert(m_user_data_length >= m_stream_offset);
	return m_user_data_length ?	(int64_t)(m_user_data_length)-m_stream_offset :	0;
}

bool GrokStream::skip(int64_t p_size,event_mgr_t * p_event_mgr)
{
	assert(p_size >= 0);
	if (m_status & GROK_STREAM_STATUS_INPUT)
		return read_skip(p_size, p_event_mgr);
	else {
		return write_skip(p_size, p_event_mgr);
	}
}

bool GrokStream::read_seek(uint64_t offset,event_mgr_t * p_event_mgr)
{
	ARG_NOT_USED(p_event_mgr);
	if (m_status & GROK_STREAM_STATUS_ERROR) {
		return false;
	}
	// 1. try to seek in buffer
	if (!(m_status & GROK_STREAM_STATUS_END)) {
		bool seekInBuffer = false;
		int64_t increment=0;
		if ((offset >= m_stream_offset && offset < m_stream_offset + m_bytes_in_buffer) ||
			(offset < m_stream_offset && offset > m_stream_offset - (m_buffer_size - m_bytes_in_buffer))) {
			increment = offset - m_stream_offset;
			seekInBuffer = true;
		}
		if (seekInBuffer) {
			m_status &= (~GROK_STREAM_STATUS_END);
			m_stream_offset = offset;
			m_buffer_current_ptr += increment;
			m_bytes_in_buffer -= increment;
			return true;
		}
	}

	//2. Since we can't seek in buffer, we must invalidate
	//  buffer contents and seek in media
	m_bytes_in_buffer = 0;
	m_buffer_current_ptr = m_buffer;
	if (!(m_seek_fn(offset, m_user_data))) {
		m_status |= GROK_STREAM_STATUS_END;
		return false;
	}
	else {
		m_status &= (~GROK_STREAM_STATUS_END);
		m_stream_offset = offset;
	}
	return true;
}

//absolute seek in stream
bool GrokStream::write_seek(uint64_t offset,event_mgr_t * p_event_mgr)
{
	if (m_status & GROK_STREAM_STATUS_ERROR) {
		return false;
	}
	if (!flush(p_event_mgr)) {
		m_status |= GROK_STREAM_STATUS_ERROR;
		return false;
	}
	m_buffer_current_ptr = m_buffer;
	m_bytes_in_buffer = 0;
	if (!m_seek_fn(offset, m_user_data)) {
		m_status |= GROK_STREAM_STATUS_ERROR;
		return false;
	}
	else {
		m_stream_offset = offset;
	}
	if (isBufferStream)
		m_buffer_current_ptr = m_buffer + offset;
	return true;
}
bool GrokStream::seek(uint64_t offset,event_mgr_t * p_event_mgr)
{
	if (m_status & GROK_STREAM_STATUS_INPUT)
		return read_seek(offset, p_event_mgr);
	else {
		return write_seek(offset, p_event_mgr);
	}
}

bool GrokStream::has_seek(void) {
	return m_seek_fn != nullptr;
}


template<typename TYPE> void grok_write(uint8_t * p_buffer, TYPE p_value, uint32_t p_nb_bytes) {
#if defined(GROK_BIG_ENDIAN)
	const uint8_t * l_data_ptr = ((const uint8_t *)&p_value) + sizeof(TYPE) - p_nb_bytes;
	assert(p_nb_bytes > 0 && p_nb_bytes <= sizeof(TYPE));
	memcpy(p_buffer, l_data_ptr, p_nb_bytes);
#else
	const uint8_t * l_data_ptr = ((const uint8_t *)&p_value) + p_nb_bytes - 1;
	assert(p_nb_bytes > 0 && p_nb_bytes <= sizeof(TYPE));
	for (uint32_t i = 0; i < p_nb_bytes; ++i) {
		*(p_buffer++) = *(l_data_ptr--);
	}
#endif
}
void grok_write_bytes(uint8_t * p_buffer, uint32_t p_value, uint32_t p_nb_bytes){
	grok_write<uint32_t>(p_buffer, p_value, p_nb_bytes);
}

void grok_write_64(uint8_t * p_buffer, uint64_t p_value, uint32_t p_nb_bytes) {
	grok_write<uint64_t>(p_buffer, p_value, p_nb_bytes);
}

void grok_write_float(uint8_t * p_buffer, float p_value){
	grok_write<float>(p_buffer, p_value, sizeof(float));
}

void grok_write_double(uint8_t * p_buffer, double p_value){
	grok_write<double>(p_buffer, p_value, sizeof(double));
}

template<typename TYPE> void grok_read(const uint8_t* p_buffer, TYPE* p_value, uint32_t p_nb_bytes) {
#if defined(GROK_BIG_ENDIAN)
	uint8_t * l_data_ptr = ((uint8_t *)p_value);
	assert(p_nb_bytes > 0 && p_nb_bytes <= sizeof(TYPE));
	*p_value = 0;
	memcpy(l_data_ptr + sizeof(TYPE) - p_nb_bytes, p_buffer, p_nb_bytes);
#else
	uint8_t * l_data_ptr = ((uint8_t *)p_value) + p_nb_bytes - 1;
	assert(p_nb_bytes > 0 && p_nb_bytes <= sizeof(TYPE));
	*p_value = 0;
	for (uint32_t i = 0; i < p_nb_bytes; ++i) {
		*(l_data_ptr--) = *(p_buffer++);
	}
#endif
}

void grok_read_bytes(const uint8_t * p_buffer, uint32_t * p_value, uint32_t p_nb_bytes){
	grok_read<uint32_t>(p_buffer, p_value, p_nb_bytes);
}

void grok_read_64(const uint8_t * p_buffer, uint64_t * p_value, uint32_t p_nb_bytes) {
	grok_read<uint64_t>(p_buffer, p_value, p_nb_bytes);
}

void grok_read_float(const uint8_t * p_buffer, float * p_value){
	grok_read<float>(p_buffer, p_value, sizeof(float));
}


void grok_read_double(const uint8_t * p_buffer, double * p_value){
	grok_read<double>(p_buffer, p_value, sizeof(double));
}

}

opj_stream_t* OPJ_CALLCONV opj_stream_create(size_t p_buffer_size, bool l_is_input)
{
	return (opj_stream_t*)(new grk::GrokStream(p_buffer_size, l_is_input));
}

opj_stream_t* OPJ_CALLCONV opj_stream_default_create(bool l_is_input)
{
	return opj_stream_create(grk::stream_chunk_size, l_is_input);
}

void OPJ_CALLCONV opj_stream_destroy(opj_stream_t* p_stream) {
	auto stream = (grk::GrokStream*)(p_stream);
	delete stream;
}

void OPJ_CALLCONV opj_stream_set_read_function(opj_stream_t* p_stream,
	opj_stream_read_fn p_function) {
	auto l_stream = (grk::GrokStream*)p_stream;
	if ((!l_stream) || (!(l_stream->m_status & GROK_STREAM_STATUS_INPUT))) {
		return;
	}
	l_stream->m_read_fn = p_function;
}

void OPJ_CALLCONV opj_stream_set_zero_copy_read_function(opj_stream_t* p_stream,
	opj_stream_zero_copy_read_fn p_function) {
	auto l_stream = (grk::GrokStream*)p_stream;
	if ((!l_stream) || (!(l_stream->m_status & GROK_STREAM_STATUS_INPUT))) {
		return;
	}
	l_stream->m_zero_copy_read_fn = p_function;
}

void OPJ_CALLCONV opj_stream_set_seek_function(opj_stream_t* p_stream,
	opj_stream_seek_fn p_function) {
	auto l_stream = (grk::GrokStream*)p_stream;
	if (!l_stream) {
		return;
	}
	l_stream->m_seek_fn = p_function;
}

void OPJ_CALLCONV opj_stream_set_write_function(opj_stream_t* p_stream,
	opj_stream_write_fn p_function) {
	auto l_stream = (grk::GrokStream*)p_stream;
	if ((!l_stream) || (!(l_stream->m_status & GROK_STREAM_STATUS_OUTPUT))) {
		return;
	}
	l_stream->m_write_fn = p_function;
}

void OPJ_CALLCONV opj_stream_set_skip_function(opj_stream_t* p_stream,	opj_stream_skip_fn p_function) {
	ARG_NOT_USED(p_stream);
	ARG_NOT_USED(p_function);
}

void OPJ_CALLCONV opj_stream_set_user_data(opj_stream_t* p_stream,
	void * p_data,
	opj_stream_free_user_data_fn p_function) {
	auto l_stream = (grk::GrokStream*)p_stream;
	if (!l_stream)
		return;
	l_stream->m_user_data = p_data;
	l_stream->m_free_user_data_fn = p_function;
}

void OPJ_CALLCONV opj_stream_set_user_data_length(opj_stream_t* p_stream,
	uint64_t data_length) {
	auto l_stream = (grk::GrokStream*)p_stream;
	if (!l_stream)
		return;
	l_stream->m_user_data_length = data_length;
}


