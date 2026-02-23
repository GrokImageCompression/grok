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

#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "grk_includes.h"

namespace grk
{

mj2_tk::mj2_tk()
    : track_type_(0), creation_time_(0), modification_time_(0), duration_(0), timescale_(0),
      layer_(0), volume_(0), language_(0), balance_(0), maxPDUsize_(0), avgPDUsize_(0),
      maxbitrate_(0), avgbitrate_(0), slidingavgbitrate_(0), graphicsmode_(0), w_(0), h_(0),
      visual_w_(0), visual_h_(0), CbCr_subsampling_dx_(0), CbCr_subsampling_dy_(0),
      samples_rate_(0), samples_description_(0), horizresolution_(0), vertresolution_(0), depth_(0),
      fieldcount_(1), fieldorder_(0), or_fieldcount_(1), or_fieldorder_(0), num_jp2x_(0),
      jp2xdata_(nullptr), hsub_(0), vsub_(0), hoff_(0), voff_(0), num_samples_(0), transorm_(0),
      handler_type_(0), name_size_(0), same_sample_size_(0)
{
  for(uint32_t i = 0; i < 9; ++i)
    trans_matrix_[i] = 0;
  for(uint32_t i = 0; i < 3; ++i)
    opcolor_[i] = 0;
  for(uint32_t i = 0; i < 2; ++i)
    Dim_[i] = 0;
}
mj2_tk::~mj2_tk()
{
  delete[] jp2xdata_;
}

FileFormatMJ2::FileFormatMJ2(IStream* stream)
    : FileFormatJP2Family(stream), headerImage_(nullptr), creation_time_(0), modification_time_(0),
      timescale_(0), duration_(0), rate_(0), num_vtk_(0), num_stk_(0), num_htk_(0), volume_(0),
      next_tk_id_(0), current_track_(nullptr)
{
  for(uint32_t i = 0; i < 9; ++i)
    trans_matrix_[i] = 0;
}

FileFormatMJ2::~FileFormatMJ2()
{
  grk_unref(headerImage_);
  for(const auto& t : tracks_)
    delete t.second;
}

GrkImage* FileFormatMJ2::getHeaderImage(void)
{
  return headerImage_;
}

} // namespace grk
