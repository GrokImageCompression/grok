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

namespace grk
{
class GrkImage;

class Profile
{
 public:
   static void initialise_4K_poc(grk_progression* POC, uint8_t numres);
   static void set_cinema_parameters(grk_cparameters* parameters, GrkImage* image);
   static bool is_cinema_compliant(GrkImage* image, uint16_t rsiz);
   static void set_imf_parameters(grk_cparameters* parameters, GrkImage* image);
   static bool is_imf_compliant(grk_cparameters* parameters, GrkImage* image);
   static void set_broadcast_parameters(grk_cparameters* parameters);
   static bool is_broadcast_compliant(grk_cparameters* parameters, GrkImage* image);

 private:
   static int get_imf_max_NL(grk_cparameters* parameters, GrkImage* image);
   static int get_broadcast_max_NL(grk_cparameters* parameters, GrkImage* image);
};

} // namespace grk
