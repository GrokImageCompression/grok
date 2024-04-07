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
#include "grk_includes.h"
#include <string>

namespace grk
{
FileFormat::FileFormat(void)
	: validation_list_(new std::vector<PROCEDURE_FUNC>()),
	  procedure_list_(new std::vector<PROCEDURE_FUNC>()), w(0), h(0), numcomps(0), bpc(0), C(0),
	  UnkC(0), IPR(0), meth(0), approx(0), enumcs(GRK_ENUM_CLRSPC_UNKNOWN), precedence(0), brand(0),
	  minversion(0), numcl(0), cl(nullptr), comps(nullptr), has_capture_resolution(false),
	  has_display_resolution(false), numUuids(0)
{
   for(uint32_t i = 0; i < 2; ++i)
   {
	  capture_resolution[i] = 0;
	  display_resolution[i] = 0;
   }
}
FileFormat::~FileFormat()
{
   delete[] comps;
   grk_free(cl);
   xml.dealloc();
   for(uint32_t i = 0; i < numUuids; ++i)
	  (uuids + i)->dealloc();
   delete validation_list_;
   delete procedure_list_;
}

bool FileFormat::exec(std::vector<PROCEDURE_FUNC>* procs)
{
   assert(procs);

   for(auto it = procs->begin(); it != procs->end(); ++it)
   {
	  if(!(*it)())
		 return false;
   }
   procs->clear();

   return true;
}

} // namespace grk
