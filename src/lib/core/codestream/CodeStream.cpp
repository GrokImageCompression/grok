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
CodeStream::CodeStream(BufferedStream* stream)
	: codeStreamInfo(nullptr), headerImage_(nullptr), currentTileProcessor_(nullptr),
	  stream_(stream), current_plugin_tile(nullptr)
{}
CodeStream::~CodeStream()
{
   if(headerImage_)
	  grk_object_unref(&headerImage_->obj);
   delete codeStreamInfo;
}
CodingParams* CodeStream::getCodingParams(void)
{
   return &cp_;
}
GrkImage* CodeStream::getHeaderImage(void)
{
   return headerImage_;
}
TileProcessor* CodeStream::currentProcessor(void)
{
   return currentTileProcessor_;
}
bool CodeStream::exec(std::vector<PROCEDURE_FUNC>& procs)
{
   bool result =
	   std::all_of(procs.begin(), procs.end(), [](const PROCEDURE_FUNC& proc) { return proc(); });
   procs.clear();

   return result;
}
grk_plugin_tile* CodeStream::getCurrentPluginTile()
{
   return current_plugin_tile;
}
BufferedStream* CodeStream::getStream()
{
   return stream_;
}

std::string CodeStream::markerString(uint16_t marker)
{
   switch(marker)
   {
	  case J2K_MS_SOC:
		 return "SOC";
	  case J2K_MS_SOT:
		 return "SOT";
	  case J2K_MS_SOD:
		 return "SOD";
	  case J2K_MS_EOC:
		 return "EOC";
	  case J2K_MS_CAP:
		 return "CAP";
	  case J2K_MS_SIZ:
		 return "SIZ";
	  case J2K_MS_COD:
		 return "COD";
	  case J2K_MS_COC:
		 return "COC";
	  case J2K_MS_RGN:
		 return "RGN";
	  case J2K_MS_QCD:
		 return "QCD";
	  case J2K_MS_QCC:
		 return "QCC";
	  case J2K_MS_POC:
		 return "POC";
	  case J2K_MS_TLM:
		 return "TLM";
	  case J2K_MS_PLM:
		 return "PLM";
	  case J2K_MS_PLT:
		 return "PLT";
	  case J2K_MS_PPM:
		 return "PPM";
	  case J2K_MS_PPT:
		 return "PPT";
	  case J2K_MS_SOP:
		 return "SOP";
	  case J2K_MS_EPH:
		 return "EPH";
	  case J2K_MS_CRG:
		 return "CRG";
	  case J2K_MS_COM:
		 return "COM";
	  case J2K_MS_CBD:
		 return "CBD";
	  case J2K_MS_MCC:
		 return "MCC";
	  case J2K_MS_MCT:
		 return "MCT";
	  case J2K_MS_MCO:
		 return "MCO";
	  case J2K_MS_UNK:
	  default:
		 return "Unknown";
   }
}

} // namespace grk
