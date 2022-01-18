/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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
CodeStream::CodeStream(IBufferedStream* stream)
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
IBufferedStream* CodeStream::getStream()
{
	return stream_;
}

} // namespace grk
