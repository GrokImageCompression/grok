/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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


namespace grk {

class T1Interface {
public:
	virtual ~T1Interface() {
	}

	virtual void preCompress(CompressBlockExec *block, grk_tile *tile,
			uint32_t &max) = 0;
	virtual double compress(CompressBlockExec *block, grk_tile *tile,
			uint32_t max, bool doRateControl)=0;

	virtual bool decompress(DecompressBlockExec *block)=0;
	virtual bool postDecompress(DecompressBlockExec *block)=0;
};

}
