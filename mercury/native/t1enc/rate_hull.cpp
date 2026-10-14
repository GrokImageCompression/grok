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

#include <cassert>
#include <cmath>
#include <cstdint>

#include "t1_common.h"

namespace
{
// grok's t1::CodePass fields the hull reads and writes
struct HullPass
{
  uint16_t len_;
  double distortiondec_;
  uint16_t slope_;
};

uint16_t slopeToLog(double slope);

void convexHull(HullPass* pass, uint32_t num_passes)
{
  double slope_cache[mercury_native::t1enc::maxCodePassesPerBlock];
  assert(num_passes <= mercury_native::t1enc::maxCodePassesPerBlock);

  // search for feasible truncation points
  for(auto p = 0U; p < num_passes; p++)
  {
    auto current_pass = pass + p;
    double dd = 0;
    double dr = 0;

    int p_intermed = (int)p;
    auto intermed_pass = pass + p_intermed;

    // loop through all intermediate points from p-1 down to 0,
    // breaking when one of the following conditions occurs:
    // 1. current pass is rejected as non-feasible
    // 2. first intermediate point is found such that current pass meets
    //    feasible-truncation conditions relative to this point
    // 3. all intermediate points are processed, and pass is not rejected,
    //    in which case current pass meets feasible-truncation conditions
    while(1)
    {
      // calculate rate and distortion deltas
      dr += intermed_pass->len_;
      if(p_intermed == 0)
      {
        dd += intermed_pass->distortiondec_;
      }
      else
      {
        dd += intermed_pass->distortiondec_ - (intermed_pass - 1)->distortiondec_;
      }

      // reject current point: all intermediate distortion-rate slopes
      // for feasible truncation point must be strictly positive
      // (Corollary 8.3)
      if(dd <= 0)
      {
        current_pass->slope_ = 0;
        break;
      }

      // decrement p_intermed
      p_intermed--;
      intermed_pass--;

      // all intermediate points have been processed and current
      // point has survived: mark current point as feasible
      if(p_intermed == -1)
      {
        // update slope for current point (Equation 8.8)
        slope_cache[p] = dd / dr;
        current_pass->slope_ = slopeToLog(slope_cache[p]);
        break;
      }

      // skip previously-rejected intermediate point
      if(intermed_pass->slope_ == 0)
      {
        continue;
      }

      // reject intermediate point as non-feasible:
      // distortion-rate slope must be finite
      if(dr == 0)
      {
        intermed_pass->slope_ = 0;
      }
      // reject intermediate point as non-feasible:
      // distortion-rate slope must be strictly monotone decreasing
      // (Corollary 8.3)
      else if((slope_cache[p_intermed] * dr) <= dd)
      {
        intermed_pass->slope_ = 0;
      }
      // feasible truncation point
      else
      {
        // update slope for current point (Equation 8.8)
        slope_cache[p] = dd / dr;
        current_pass->slope_ = slopeToLog(slope_cache[p]);

        // switching to lower resolution log domain may break
        // strict monotone condition on slopes. In this case, reject
        // lower quality point
        if(current_pass->slope_ >= intermed_pass->slope_)
        {
          intermed_pass->slope_ = 0;
        }
        break;
      }
    }
  }
}

/*
 Convert slope to Q8.8 fixed point of ln(slope)

 Notes:

 1) Take slope upper cutoff to be ULLONG_MAX+1 == 2^64.

 2) To convert to Q8.8, we

 i)   divide by upper cutoff, mapping slope to the interval (0,1]

 ii)  take natural logarithm, mapping slope to (-INF, 0]

 iii) scale values based on following logic:
 when slope is doubled, log(slope) increases by ln(2).
 We want this increase to correspond to an increase of 1
 in the fixed point domain, which is equivalent to 256
 in fixed point. So, scaling factor is 256/ln(2)

 iv) Map range to 16 bit unsigned int, i.e.
 add (1<<16), then truncate to range [1,0xFFFF].
 Value of zero is reserved to indicate a non-feasible truncation point.


 Putting this all together:

 slopeCutoff = 2^64
 scale = 256/ln(2)
 shift = 1<<16

 logSlope = ln(slope/slopeCutoff)*scale + shift
 = ln(slope)*scale - ln(slopeCutoff)*scale + shift
 */

const double slopeCutoff = pow(2, 64);
const double scale = 256 / log(2);
const double invScale = log(2) / 256;
const double shift = 1 << 16;

uint16_t slopeToLog(double slope)
{
  if(slope > slopeCutoff)
    slope = slopeCutoff;
  static const double cutoffTerm = log(slopeCutoff) * scale - shift;
  double logSlope = log(slope) * scale - cutoffTerm;
  if(logSlope < 1)
    logSlope = 1;
  if(logSlope > 0xFFFF)
    logSlope = 0xFFFF;
  return (uint16_t)logSlope;
}

} // namespace

#if defined(_WIN32)
#define MERCURY_HULL_API __declspec(dllexport)
#else
#define MERCURY_HULL_API __attribute__((visibility("default")))
#endif

// grok's RateControl::convexHull: log-domain slopes, 0 for passes off the hull
extern "C" MERCURY_HULL_API void mercury_rate_convex_hull(const uint16_t* lens,
                                                          const double* distortions,
                                                          uint16_t* slopes, uint32_t num_passes)
{
  HullPass passes[mercury_native::t1enc::maxCodePassesPerBlock];
  assert(num_passes <= mercury_native::t1enc::maxCodePassesPerBlock);
  for(uint32_t p = 0; p < num_passes; ++p)
    passes[p] = {lens[p], distortions[p], 0};
  convexHull(passes, num_passes);
  for(uint32_t p = 0; p < num_passes; ++p)
    slopes[p] = passes[p].slope_;
}
