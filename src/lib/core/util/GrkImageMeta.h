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

#pragma once

namespace grk
{

class GrkImageMeta : public grk_image_meta
{
public:
  GrkImageMeta()
  {
    obj.wrapper = new GrkObjectWrapperImpl(this);
    iptc_buf = nullptr;
    iptc_len = 0;
    xmp_buf = nullptr;
    xmp_len = 0;
    color = {};
  }

  virtual ~GrkImageMeta()
  {
    releaseColor();
    delete[] iptc_buf;
    delete[] xmp_buf;
  }
  void allocPalette(uint8_t num_channels, uint16_t num_entries)
  {
    assert(num_channels);
    assert(num_entries);

    if(!num_channels || !num_entries)
      return;

    releaseColorPalatte();
    auto jp2_pclr = new grk_palette_data();
    jp2_pclr->channel_sign = new bool[num_channels];
    jp2_pclr->channel_prec = new uint8_t[num_channels];
    jp2_pclr->lut = new int32_t[num_channels * num_entries];
    jp2_pclr->num_entries = num_entries;
    jp2_pclr->num_channels = num_channels;
    jp2_pclr->component_mapping = nullptr;
    color.palette = jp2_pclr;
  }
  void releaseColorPalatte()
  {
    if(color.palette)
    {
      delete[] color.palette->channel_sign;
      delete[] color.palette->channel_prec;
      delete[] color.palette->lut;
      delete[] color.palette->component_mapping;
      delete color.palette;
      color.palette = nullptr;
    }
  }
  void releaseColor()
  {
    releaseColorPalatte();
    delete[] color.icc_profile_buf;
    color.icc_profile_buf = nullptr;
    color.icc_profile_len = 0;
    delete[] color.icc_profile_name;
    color.icc_profile_name = nullptr;
    if(color.channel_definition)
    {
      delete[] color.channel_definition->descriptions;
      delete color.channel_definition;
      color.channel_definition = nullptr;
    }
  }
};

} // namespace grk
