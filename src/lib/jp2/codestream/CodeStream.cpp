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
#include "ojph_arch.h"

namespace grk {

CodeStream::CodeStream(BufferedStream *stream) : codeStreamInfo(nullptr),
																m_headerImage(nullptr),
																m_currentTileProcessor(nullptr),
																m_stream(stream),
																m_multiTile(false),
																current_plugin_tile(nullptr)
{
    memset(&m_cp, 0 , sizeof(CodingParams));

}
CodeStream::~CodeStream(){
	if (m_headerImage)
		grk_object_unref(&m_headerImage->obj);
	m_cp.destroy();
	delete codeStreamInfo;
}
CodingParams* CodeStream::getCodingParams(void){
	return &m_cp;
}
GrkImage* CodeStream::getHeaderImage(void){
	return m_headerImage;
}
TileProcessor* CodeStream::currentProcessor(void){
	return m_currentTileProcessor;
}
bool CodeStream::exec(std::vector<PROCEDURE_FUNC> &procs) {
    bool result = std::all_of(procs.begin(), procs.end(),[](const PROCEDURE_FUNC &proc){
    	return proc();
    });
	procs.clear();

	return result;
}
grk_plugin_tile* CodeStream::getCurrentPluginTile(){
	return current_plugin_tile;
}
BufferedStream* CodeStream::getStream(){
	return m_stream;
}

}
