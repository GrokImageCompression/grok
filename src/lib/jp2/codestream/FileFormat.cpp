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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */
#include "grk_includes.h"
#include <string>

namespace grk {

FileFormat::FileFormat(void) :
						m_validation_list(new std::vector<PROCEDURE_FUNC>()),
						m_procedure_list(new std::vector<PROCEDURE_FUNC>()),
						w(0),
						h(0),
						numcomps(0),
						bpc(0),
						C(0),
						UnkC(0),
						IPR(0),
						meth(0),
						approx(0),
						enumcs(GRK_ENUM_CLRSPC_UNKNOWN),
						precedence(0),
						brand(0),
						minversion(0),
						numcl(0),
						cl(nullptr),
						comps(nullptr),
						has_capture_resolution(false),
						has_display_resolution(false),
						numUuids(0) {
	for (uint32_t i = 0; i < 2; ++i) {
		capture_resolution[i] = 0;
		display_resolution[i] = 0;
	}

	/* Color structure */
	color.icc_profile_buf = nullptr;
	color.icc_profile_len = 0;
	color.channel_definition = nullptr;
	color.palette = nullptr;
	color.has_colour_specification_box = false;

}
FileFormat::~FileFormat() {
	delete[] comps;
	grkFree(cl);
	FileFormatDecompress::free_color(&color);
	xml.dealloc();
	for (uint32_t i = 0; i < numUuids; ++i)
		(uuids + i)->dealloc();
	delete m_validation_list;
	delete m_procedure_list;
}

bool FileFormat::exec( std::vector<PROCEDURE_FUNC> *procs) {
	bool result = true;
	assert(procs);

	for (auto it = procs->begin(); it != procs->end(); ++it) {
		auto p = *it;
		result = result && (p)();
	}
	procs->clear();

	return result;
}

}
