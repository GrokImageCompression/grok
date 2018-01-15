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
*/
#pragma once

#include <cstdint>

namespace grk {

/**
Bit input/output
*/
class IBitIO {

public:

	virtual ~IBitIO() {}

	/**
	Number of bytes written.
	@return the number of bytes written
	*/
	virtual size_t numbytes()=0;

	/**
	Write bits
	@param v Value of bits
	@param n Number of bits to write
	*/
	virtual bool write( uint32_t v, uint32_t n)= 0;
	/**
	Read bits
	@param n Number of bits to read
	@return the corresponding read number
	*/
	virtual bool read(uint32_t* bits, uint32_t n)= 0;
	/**
	Flush bits
	@return true if successful, returns false otherwise
	*/
	virtual bool flush()= 0;
	/**
	Passes the ending bits (coming from flushing)
	@return true if successful, returns false otherwise
	*/
	virtual bool inalign()= 0;

};


}
	
