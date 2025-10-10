/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

namespace grk
{
class GrkImage;

class Profile
{
public:
  static void init4kPoc(grk_progression* prog, uint8_t numres);
  static void setCinemaParams(grk_cparameters* parameters, GrkImage* image);
  static bool isCinemaCompliant(GrkImage* image, uint16_t rsiz);
  static void setImfParams(grk_cparameters* parameters, GrkImage* image);
  static bool isImfCompliant(grk_cparameters* parameters, GrkImage* image);
  static void setBroadcastParams(grk_cparameters* parameters);
  static bool isBroadcastCompliant(grk_cparameters* parameters, GrkImage* image);

private:
  static int getImfMaxNumDecompLevels(grk_cparameters* parameters, GrkImage* image);
  static int getBroadcastMaxDecompLevels(grk_cparameters* parameters, GrkImage* image);
};

} // namespace grk
