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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once
namespace grk {

/**
 @file image.h
 @brief Implementation of operations on images (IMAGE)

 The functions in IMAGE.C have for goal to realize operations on images.
 */

struct CodingParams;

/** @defgroup IMAGE IMAGE - Implementation of operations on images */
/*@{*/

/**
 * Create an empty image
 *
 * @return returns an empty image if successful, returns nullptr otherwise
 */
grk_image *  grk_image_create0(void);

/**
 * Update image components with coding parameters.
 *
 * @param image		image to update.
 * @param p_cp		coding parameters from which to update the image.
 */
void grk_image_comp_header_update(grk_image *image, const CodingParams *p_cp);

void grk_copy_image_header(const grk_image *p_image_src,
		grk_image *p_image_dest);

bool update_image_dimensions(grk_image* p_image, uint32_t reduce);

/**
 Transfer data from src to dest for each component, and null out src data.
 Assumption:  src and dest have the same number of components
 */
void transfer_image_data(grk_image *src, grk_image *dest);

/*@}*/

}
