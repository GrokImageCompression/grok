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

#include <cmath>
#include <vector>

#include "Qfactor.h"

namespace grk
{

namespace
{
  // quality factor thresholds of the JPEG style quality curve
  const uint8_t qualityKnee = 65;
  const uint8_t qualityTop = 97;
  const double alphaAtKnee = 0.04;
  const double alphaAtTop = 0.10;
  const double mantissaScale = 2048.0;
  const uint8_t maxExponent = 31;
  const uint16_t maxMantissa = 2047;

  // the weight tables cover five decomposition levels, coarser bands weigh 1
  const size_t weightedBands = 15;
  // Zeng et al. Table 2, square root domain, finest level first as HH, LH, HL
  const double lumaWeights[weightedBands] = {0.0901, 0.2758, 0.2758, 0.7018, 0.8378,
                                             0.8378, 1.0000, 1.0000, 1.0000, 1.0000,
                                             1.0000, 1.0000, 1.0000, 1.0000, 1.0000};
  const double cbWeights444[weightedBands] = {0.0263, 0.0863, 0.0863, 0.1362, 0.2564,
                                              0.2564, 0.3346, 0.4691, 0.4691, 0.5444,
                                              0.6523, 0.6523, 0.7078, 0.7797, 0.7797};
  const double crWeights444[weightedBands] = {0.0773, 0.1835, 0.1835, 0.2598, 0.4130,
                                              0.4130, 0.5040, 0.6464, 0.6464, 0.7220,
                                              0.8254, 0.8254, 0.8769, 0.9424, 0.9424};
  const double cbWeights420[weightedBands] = {0.1362, 0.2564, 0.2564, 0.3346, 0.4691,
                                              0.4691, 0.5444, 0.6523, 0.6523, 0.7078,
                                              0.7797, 0.7797, 1.0000, 1.0000, 1.0000};
  const double crWeights420[weightedBands] = {0.2598, 0.4130, 0.4130, 0.5040, 0.6464,
                                              0.6464, 0.7220, 0.8254, 0.8254, 0.8769,
                                              0.9424, 0.9424, 1.0000, 1.0000, 1.0000};
  const double cbWeights422[weightedBands] = {0.0863, 0.0863, 0.2564, 0.2564, 0.2564,
                                              0.4691, 0.4691, 0.4691, 0.6523, 0.6523,
                                              0.6523, 0.7797, 0.7797, 0.7797, 1.0000};
  const double crWeights422[weightedBands] = {0.1835, 0.1835, 0.4130, 0.4130, 0.4130,
                                              0.6464, 0.6464, 0.6464, 0.8254, 0.8254,
                                              0.8254, 0.9424, 0.9424, 0.9424, 1.0000};

  // inverse ICT column norms, rounded as the reference encoder stores them
  const double ictGains[3] = {1.7321, 1.8051, 1.5734};

  const std::vector<double> synthesisLow97 = {
      -0.091271763114250, -0.057543526228500, 0.591271763114250, 1.115087052457000,
      0.5912717631142500, -0.05754352622850,  -0.091271763114250};
  const std::vector<double> synthesisHigh97 = {
      0.053497514821622,  0.033728236885750, -0.156446533057980,
      -0.533728236885750, 1.205898036472720, -0.533728236885750,
      -0.156446533057980, 0.033728236885750, 0.053497514821622};

  struct QualityScaling
  {
    double referenceStep;
    double weightPower;
  };

  QualityScaling qualityScaling(uint8_t qfactor, uint8_t bitDepth)
  {
    const double mAtKnee = 2.0 * (1.0 - qualityKnee / 100.0);
    const double mAtTop = 2.0 * (1.0 - qualityTop / 100.0);
    const double m = (qfactor < 50) ? 50.0 / qfactor : 2.0 * (1.0 - qfactor / 100.0);
    double alpha = alphaAtKnee;
    double weightPower = 1.0;
    if(qfactor >= qualityTop)
    {
      weightPower = 0.0;
      alpha = alphaAtTop;
    }
    else if(qfactor > qualityKnee)
    {
      weightPower = (std::log(mAtTop) - std::log(m)) / (std::log(mAtTop) - std::log(mAtKnee));
      alpha = alphaAtTop * std::pow(alphaAtKnee / alphaAtTop, weightPower);
    }
    const double halfLsb = std::sqrt(0.5) * std::ldexp(1.0, -(int)bitDepth);
    return {alpha * m + halfLsb, weightPower};
  }

  std::vector<double> upsampleAndFilter(const std::vector<double>& filter)
  {
    std::vector<double> upsampled;
    for(auto tap : filter)
    {
      upsampled.push_back(tap);
      upsampled.push_back(0.0);
    }
    std::vector<double> result(synthesisLow97.size() + upsampled.size() - 1, 0.0);
    for(size_t i = 0; i < synthesisLow97.size(); ++i)
      for(size_t j = 0; j < upsampled.size(); ++j)
        result[i + j] += synthesisLow97[i] * upsampled[j];
    return result;
  }

  double energy(const std::vector<double>& filter)
  {
    double sum = 0.0;
    for(auto tap : filter)
      sum += tap * tap;
    return sum;
  }

  // energy gain of the 9/7 synthesis basis per band, finest level first as HH, LH, HL,
  // with LL last
  std::vector<double> synthesisGains(uint8_t numDecompositions)
  {
    if(numDecompositions == 0)
      return {1.0};
    std::vector<double> gains;
    auto low = synthesisLow97;
    auto high = synthesisHigh97;
    double lowGain = 0.0;
    for(uint8_t level = 0; level < numDecompositions; ++level)
    {
      lowGain = energy(low);
      double highGain = energy(high);
      gains.push_back(highGain * highGain);
      gains.push_back(lowGain * highGain);
      gains.push_back(highGain * lowGain);
      low = upsampleAndFilter(low);
      high = upsampleAndFilter(high);
    }
    gains.push_back(lowGain * lowGain);
    return gains;
  }

  const double* visualWeights(uint16_t compno, ChromaSubsampling subsampling)
  {
    if(compno == 0)
      return lumaWeights;
    bool cb = compno == 1;
    switch(subsampling)
    {
      case ChromaSubsampling::halfBoth:
        return cb ? cbWeights420 : crWeights420;
      case ChromaSubsampling::halfHorizontal:
        return cb ? cbWeights422 : crWeights422;
      default:
        return cb ? cbWeights444 : crWeights444;
    }
  }

  void packStepsize(double step, grk_stepsize& out)
  {
    int32_t exponent = 0;
    for(; step < 1.0; exponent++)
      step *= 2.0;
    int32_t mantissa = (int32_t)std::floor((step - 1.0) * mantissaScale + 0.5);
    if(mantissa > maxMantissa)
    {
      mantissa = 0;
      exponent--;
    }
    if(exponent > maxExponent)
    {
      exponent = maxExponent;
      mantissa = 0;
    }
    if(exponent < 0)
    {
      exponent = 0;
      mantissa = maxMantissa;
    }
    out.expn = (uint8_t)exponent;
    out.mant = (uint16_t)mantissa;
  }
} // namespace

ChromaSubsampling chromaSubsampling(const grk_image* image)
{
  if(image->numcomps != 3)
    return ChromaSubsampling::full;
  auto cb = image->comps[1];
  auto cr = image->comps[2];
  if(cb.dx != 2 || cr.dx != 2)
    return ChromaSubsampling::full;
  if(cb.dy == 2 && cr.dy == 2)
    return ChromaSubsampling::halfBoth;
  if(cb.dy == 1 && cr.dy == 1)
    return ChromaSubsampling::halfHorizontal;
  return ChromaSubsampling::full;
}

void generateQfactorStepsizes(uint8_t qfactor, uint8_t numDecompositions, uint8_t bitDepth,
                              uint16_t compno, ChromaSubsampling subsampling,
                              grk_stepsize* stepsizes)
{
  auto scaling = qualityScaling(qfactor, bitDepth);
  double componentGain = ictGains[compno < 3 ? compno : 0];
  double referenceStep = scaling.referenceStep * ictGains[0];
  auto gains = synthesisGains(numDecompositions);
  auto weights = visualWeights(compno, subsampling);
  size_t numBands = gains.size();
  for(size_t band = 0; band < numBands; ++band)
  {
    bool isLL = band == numBands - 1;
    double weight =
        (isLL || band >= weightedBands) ? 1.0 : std::pow(weights[band], scaling.weightPower);
    double step = referenceStep / (std::sqrt(gains[band]) * weight * componentGain);
    packStepsize(step, stepsizes[numBands - 1 - band]);
  }
}

} // namespace grk
