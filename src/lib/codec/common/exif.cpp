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

// EXIF transfer is now handled automatically through the exif_buf/exif_len
// pipeline in grk_image_meta. The old Perl/ExifTool-based transfer_exif_tags
// function is no longer needed.

#include "exif.h"

namespace grk
{

void transfer_exif_tags([[maybe_unused]] const std::string& src,
                        [[maybe_unused]] const std::string& dest)
{
  // No-op: EXIF is now transferred automatically via exif_buf in grk_image_meta
}

} // namespace grk
