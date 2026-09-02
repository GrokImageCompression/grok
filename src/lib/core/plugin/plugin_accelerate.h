/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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

// the plugin runs T1 for one frame at a time, the host keeps the header, rate
// allocation and packets

#pragma once

#include <cstdint>
#include <mutex>
#include "grok.h"

namespace grk
{
bool pluginAccelerates(void);
// one frame on the device at a time, host T2 runs outside this lock
std::mutex& pluginFrameMutex(void);
void pluginCountAcceleratedFrame(void);
// 0: tile holds the image's compressed code blocks, 1: the plugin does not
// handle these parameters or this image, -1: the device failed
int32_t pluginEncodeImage(const grk_cparameters* parameters, grk_image* image,
                          grk_plugin_tile** tile, void** rawTile);
void pluginReleaseEncodedTile(grk_plugin_tile* tile, void* rawTile);
} // namespace grk
