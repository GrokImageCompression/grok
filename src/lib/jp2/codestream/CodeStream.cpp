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

CodeStream::CodeStream(BufferedStream *stream) : cstr_index(nullptr),
																m_headerImage(nullptr),
																m_tileProcessor(nullptr),
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
	if (cstr_index) {
		grkFree(cstr_index->marker);
		if (cstr_index->tile_index) {
			for (uint32_t i = 0; i < cstr_index->nb_of_tiles; i++) {
				grkFree(cstr_index->tile_index[i].tp_index);
				grkFree(cstr_index->tile_index[i].marker);
			}
			grkFree(cstr_index->tile_index);
		}
		grkFree(cstr_index);
	}
}
CodingParams* CodeStream::getCodingParams(void){
	return &m_cp;
}
GrkImage* CodeStream::getHeaderImage(void){
	return m_headerImage;
}
TileProcessor* CodeStream::currentProcessor(void){
	return m_tileProcessor;
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
