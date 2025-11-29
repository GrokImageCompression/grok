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

#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <CLI/CLI.hpp>
#include "grk_apps_config.h"
#include "grok.h"
#include "spdlogwrapper.h"
#include "BMPFormat.h"
#include "PGXFormat.h"
#include "PNMFormat.h"
#include "YUVFormat.h"
#include "common.h"
#ifdef GROK_HAVE_LIBPNG
#include "PNGFormat.h"
#endif
#ifdef GROK_HAVE_LIBTIFF
#include "TIFFFormat.h"
#include <tiffio.h>
#endif
#include "GrkCompareImages.h"

namespace grk
{

namespace fs = std::filesystem;

struct GrkImageDeleter
{
  void operator()(grk_image* img) const
  {
    if(img)
    {
      grk_object_unref(&img->obj);
    }
  }
};

using GrkImagePtr = std::unique_ptr<grk_image, GrkImageDeleter>;

std::vector<double> parseToleranceValues(const std::string& input, uint16_t numComponents)
{
  if(input.empty() || numComponents == 0)
  {
    return {};
  }

  std::vector<double> result;
  result.reserve(numComponents);

  std::string::size_type pos = 0;
  std::string::size_type prev = 0;
  uint16_t count = 0;

  while((pos = input.find(':', prev)) != std::string::npos && count < numComponents)
  {
    result.push_back(std::stod(input.substr(prev, pos - prev)));
    prev = pos + 1;
    ++count;
  }

  if(count < numComponents)
  {
    result.push_back(std::stod(input.substr(prev)));
    ++count;
  }

  return (count == numComponents) ? result : std::vector<double>{};
}

void compare_images_help_display()
{
  std::cout << "\nList of parameters for the compare_images utility\n\n"
            << "-b  REQUIRED  Reference/baseline PGX/TIF/PNM image file\n"
            << "-t  REQUIRED  Test PGX/TIF/PNM image file\n"
            << "-n  REQUIRED  Number of components in the image\n"
            << "-d  OPTIONAL  Run as non-regression test (default: conformance test)\n"
            << "-m  OPTIONAL  MSE tolerances (colon-separated, must match component count)\n"
            << "-p  OPTIONAL  PEAK tolerances (colon-separated, must match component count)\n"
            << "-s  OPTIONAL  Filename separator (1 or 2) for multi-component images\n"
            << "              Use 'b' or 't' prefix for base/test file separator\n"
            << "-R  OPTIONAL  Sub-region of base image (x0,y0,x1,y1)\n"
            << "              Test image dimensions must match sub-region\n\n";
}

std::string createMultiComponentsFilename(const std::string& filename, uint16_t index,
                                          const std::string& separator)
{
  auto lastDot = filename.find_last_of('.');
  if(lastDot == std::string::npos)
  {
    spdlog::error("createMultiComponentsFilename: missing file extension");
    return {};
  }

  std::string baseName = filename.substr(0, lastDot);
  std::string extension;
  auto format = grk_get_file_format(filename.c_str());
  if(format == GRK_FMT_PGX)
  {
    extension = ".pgx";
  }
  else if(format == GRK_FMT_PXM)
  {
    extension = ".pgm";
  }
  else
  {
    return {};
  }

  return baseName + separator + std::to_string(index) + extension;
}

template<typename T>
GrkImagePtr readImageFromFilePGX(const std::string& filename, uint16_t numFiles,
                                 const std::string& separator)
{
  auto numComponents = (separator.empty()) ? 1 : numFiles;
  if(numComponents == 0)
  {
    return GrkImagePtr(nullptr);
  }

  grk_cparameters parameters{};
  grk_compress_set_default_params(&parameters);
  parameters.decod_format = GRK_FMT_PGX;

  std::vector<grk_image_comp> components(static_cast<size_t>(numComponents));
  std::vector<std::vector<int32_t>> componentData(static_cast<size_t>(numComponents));

  for(uint16_t i = 0; i < numComponents; ++i)
  {
    std::string file =
        separator.empty() ? filename : createMultiComponentsFilename(filename, i, separator);

    PGXFormat<int32_t> pgx;
    GrkImagePtr src(pgx.decode(file.c_str(), &parameters));
    if(!src || !src->comps || !src->comps->h || !src->comps->w)
    {
      spdlog::error("Unable to load pgx file: {}", file);
      return GrkImagePtr(nullptr);
    }

    components[i] = *src->comps;
    componentData[i].resize(static_cast<size_t>(src->comps->h) * src->comps->stride);
    std::copy_n((T*)src->comps->data, componentData[i].size(), componentData[i].begin());
  }

  GrkImagePtr dest(grk_image_new(static_cast<uint16_t>(numComponents), components.data(),
                                 GRK_CLRSPC_UNKNOWN, true));
  if(!dest || !dest->comps)
  {
    return GrkImagePtr(nullptr);
  }

  for(uint16_t i = 0; i < numComponents; ++i)
  {
    std::copy_n(componentData[i].data(),
                static_cast<size_t>(dest->comps[i].h) * dest->comps[i].stride,
                (T*)dest->comps[i].data);
  }

  return dest;
}

GrkImagePtr readImageFromFileBMP(const std::string& filename)
{
  grk_cparameters parameters{};
  grk_compress_set_default_params(&parameters);
  parameters.decod_format = GRK_FMT_BMP;

  BMPFormat<int32_t> bmp;
  GrkImagePtr img(bmp.decode(filename.c_str(), &parameters));
  if(!img)
  {
    spdlog::error("Unable to load BMP file: {}", filename);
  }
  return img;
}

#ifdef GROK_HAVE_LIBPNG
GrkImagePtr readImageFromFilePNG(const std::string& filename)
{
  grk_cparameters parameters{};
  grk_compress_set_default_params(&parameters);
  parameters.decod_format = GRK_FMT_PNG;

  PNGFormat<int32_t> png;
  GrkImagePtr img(png.decode(filename.c_str(), &parameters));
  if(!img)
  {
    spdlog::error("Unable to load PNG file: {}", filename);
  }
  return img;
}
#else
GrkImagePtr readImageFromFilePNG(const std::string& filename)
{
  spdlog::error("PNG support not compiled in");
  return GrkImagePtr(nullptr);
}
#endif

#ifdef GROK_HAVE_LIBTIFF
GrkImagePtr readImageFromFileTIF(const std::string& filename, const std::string& /*separator*/)
{
  TIFFSetWarningHandler(nullptr);
  TIFFSetErrorHandler(nullptr);

  grk_cparameters parameters{};
  grk_compress_set_default_params(&parameters);
  parameters.decod_format = GRK_FMT_TIF;

  TIFFFormat<int32_t> tif;
  GrkImagePtr img(tif.decode(filename.c_str(), &parameters));
  if(!img)
  {
    spdlog::error("Unable to load TIF file: {}", filename);
  }
  return img;
}
#else
GrkImagePtr readImageFromFileTIF(const std::string& filename, const std::string& /*separator*/)
{
  spdlog::error("TIFF support not compiled in");
  return GrkImagePtr(nullptr);
}
#endif

template<typename T>
GrkImagePtr readImageFromFilePPM(const std::string& filename, uint16_t numFiles,
                                 const std::string& separator)
{
  auto numComponents = (separator.empty()) ? 1 : numFiles;
  if(numComponents == 0)
  {
    return GrkImagePtr(nullptr);
  }

  grk_cparameters parameters{};
  grk_compress_set_default_params(&parameters);
  parameters.decod_format = GRK_FMT_PXM;

  std::vector<grk_image_comp> components(static_cast<size_t>(numComponents));
  std::vector<std::vector<int32_t>> componentData(static_cast<size_t>(numComponents));

  for(uint16_t i = 0; i < numComponents; ++i)
  {
    std::string file =
        separator.empty() ? filename : createMultiComponentsFilename(filename, i, separator);

    PNMFormat<int32_t> pnm(false);
    GrkImagePtr src(pnm.decode(file.c_str(), &parameters));
    if(!src || !src->comps || !src->comps->h || !src->comps->w)
    {
      spdlog::error("Unable to load ppm file: {}", file);
      return GrkImagePtr(nullptr);
    }
    components[i] = *src->comps;
    componentData[i].resize(static_cast<size_t>(src->comps->h) * src->comps->stride);
    std::copy_n((T*)src->comps->data, componentData[i].size(), componentData[i].begin());
  }

  GrkImagePtr dest(grk_image_new(static_cast<uint16_t>(numComponents), components.data(),
                                 GRK_CLRSPC_UNKNOWN, true));
  if(!dest || !dest->comps)
  {
    return GrkImagePtr(nullptr);
  }

  for(uint16_t i = 0; i < numComponents; ++i)
  {
    std::copy_n(componentData[i].data(),
                static_cast<size_t>(dest->comps[i].h) * dest->comps[i].stride,
                (T*)dest->comps[i].data);
  }

  return dest;
}

struct TestCmpParameters
{
  std::string base_filename;
  std::string test_filename;
  uint16_t num_components{0};
  std::vector<double> mse_values;
  std::vector<double> peak_values;
  bool non_regression{false};
  std::string separator_base;
  std::string separator_test;
  std::array<double, 4> region{};
  bool region_set{false};
};

int parse_cmdline_cmp(int argc, const char* argv[], TestCmpParameters& param)
{
  CLI::App app{"compare_images command line"};

  std::string mse, psnr, separator_list, region;

  app.add_option("-b,--Base", param.base_filename, "Base Image")->required();
  app.add_option("-t,--Test", param.test_filename, "Test Image")->required();
  app.add_option("-n,--NumComponents", param.num_components, "Number of components")->required();
  app.add_option("-m,--MSE", mse, "Mean Square Energy");
  app.add_option("-p,--PSNR", psnr, "Peak Signal To Noise Ratio");
  app.add_flag("-d,--NonRegression", param.non_regression, "Non regression");
  app.add_option("-s,--Separator", separator_list, "Separator");
  app.add_option("-R,--SubRegion", region,
                 "Base image region to compare with. Must equal test image dimensions.");

  try
  {
    app.parse(argc, argv);
  }
  catch(const CLI::ParseError& e)
  {
    return app.exit(e);
  }

  if(param.num_components == 0)
  {
    spdlog::error("Need to indicate the number of components!");
    return 1;
  }

  if(!mse.empty() && !psnr.empty())
  {
    param.mse_values = parseToleranceValues(mse, param.num_components);
    param.peak_values = parseToleranceValues(psnr, param.num_components);
    if(param.mse_values.empty() || param.peak_values.empty())
    {
      spdlog::error("MSE and PEAK values are not correct (need {} values)", param.num_components);
      return 1;
    }
  }

  if(!separator_list.empty())
  {
    if(separator_list.size() == 2 || separator_list.size() == 4)
    {
      if(separator_list[0] == 'b')
      {
        param.separator_base = separator_list.substr(1, 1);
        if(separator_list.size() == 4 && separator_list[2] == 't')
        {
          param.separator_test = separator_list.substr(3, 1);
        }
      }
      else if(separator_list[0] == 't')
      {
        param.separator_test = separator_list.substr(1, 1);
        if(separator_list.size() == 4 && separator_list[2] == 'b')
        {
          param.separator_base = separator_list.substr(3, 1);
        }
      }
      else
      {
        return 1;
      }
    }
    else
    {
      return 1;
    }
  }
  else if(param.num_components > 1)
  {
    auto baseFormat = grk_get_file_format(param.base_filename.c_str());
    auto testFormat = grk_get_file_format(param.test_filename.c_str());
    if((baseFormat == GRK_FMT_PGX || baseFormat == GRK_FMT_PXM || testFormat == GRK_FMT_PGX ||
        testFormat == GRK_FMT_PXM))
    {
      spdlog::error("If number of components is > 1, we need separator for PGX/PNM files");
      return 1;
    }
  }

  if(!region.empty())
  {
    double x0{}, y0{}, x1{}, y1{};
    std::string regionCopy = region;
    if(parseWindowBounds(regionCopy.data(), &x0, &y0, &x1, &y1))
    {
      param.region = {x0, y0, x1, y1};
      param.region_set = true;
    }
  }

  if(param.non_regression && (!mse.empty() || !psnr.empty()))
  {
    spdlog::error("Non-regression flag cannot be used with PEAK or MSE tolerance");
    return 1;
  }
  if(!param.non_regression && mse.empty() && psnr.empty())
  {
    spdlog::info("Setting non-regression flag as no PEAK or MSE tolerance specified");
    param.non_regression = true;
  }

  return 0;
}

GrkImagePtr loadImage(const std::string& filename, uint16_t numComponents,
                      const std::string& separator)
{
  switch(grk_get_file_format(filename.c_str()))
  {
    case GRK_FMT_PGX:
      return readImageFromFilePGX<int32_t>(filename, numComponents, separator);
    case GRK_FMT_TIF:
      return readImageFromFileTIF(filename, separator);
    case GRK_FMT_PXM:
      return readImageFromFilePPM<int32_t>(filename, numComponents, separator);
    case GRK_FMT_PNG:
      return readImageFromFilePNG(filename);
    case GRK_FMT_BMP:
      return readImageFromFileBMP(filename);
    default:
      spdlog::error("Unsupported file format: {}", filename);
      return GrkImagePtr(nullptr);
  }
}

int GrkCompareImages::main(int argc, const char* argv[])
{
  TestCmpParameters params;
  if(parse_cmdline_cmp(argc, argv, params))
  {
    compare_images_help_display();
    return EXIT_FAILURE;
  }

  spdlog::info("******Parameters*********");
  spdlog::info("Base_filename = {}", params.base_filename);
  spdlog::info("Test_filename = {}", params.test_filename);
  spdlog::info("Number of components = {}", params.num_components);
  spdlog::info("Non-regression test = {}", params.non_regression);
  spdlog::info("Separator Base = {}", params.separator_base);
  spdlog::info("Separator Test = {}", params.separator_test);

  if(!params.mse_values.empty() && !params.peak_values.empty())
  {
    spdlog::info("MSE values = [");
    for(const auto& val : params.mse_values)
    {
      spdlog::info(" {} ", val);
    }
    spdlog::info("PEAK values = [");
    for(const auto& val : params.peak_values)
    {
      spdlog::info(" {} ", val);
    }
  }

  auto imageBase = loadImage(params.base_filename, params.num_components, params.separator_base);
  auto imageTest = loadImage(params.test_filename, params.num_components, params.separator_test);

  if(!imageBase || !imageTest)
  {
    return EXIT_FAILURE;
  }

  if(imageBase->numcomps != imageTest->numcomps)
  {
    spdlog::error("Component count mismatch between images: {} vs {}", imageBase->numcomps,
                  imageTest->numcomps);
    return EXIT_FAILURE;
  }

  // Only enforce specified component count for multi-file formats
  auto baseFormat = grk_get_file_format(params.base_filename.c_str());
  auto testFormat = grk_get_file_format(params.test_filename.c_str());
  if((baseFormat == GRK_FMT_PGX || baseFormat == GRK_FMT_PXM) &&
     params.num_components != imageBase->numcomps)
  {
    spdlog::error("Specified number of components ({}) doesn't match actual ({}) for base PGX/PNM",
                  params.num_components, imageBase->numcomps);
    return EXIT_FAILURE;
  }
  if((testFormat == GRK_FMT_PGX || testFormat == GRK_FMT_PXM) &&
     params.num_components != imageTest->numcomps)
  {
    spdlog::error("Specified number of components ({}) doesn't match actual ({}) for test PGX/PNM",
                  params.num_components, imageTest->numcomps);
    return EXIT_FAILURE;
  }

  for(uint16_t i = 0; i < imageBase->numcomps; ++i)
  {
    const auto& baseComp = imageBase->comps[i];
    const auto& testComp = imageTest->comps[i];

    if(baseComp.sgnd != testComp.sgnd)
    {
      spdlog::error("Sign mismatch for component {}: {} vs {}", i, baseComp.sgnd, testComp.sgnd);
      return EXIT_FAILURE;
    }

    if(baseComp.prec != testComp.prec)
    {
      spdlog::error("Precision mismatch for component {}: {} vs {}", i, baseComp.prec,
                    testComp.prec);
      return EXIT_FAILURE;
    }

    if(params.region_set)
    {
      uint32_t regionWidth = static_cast<uint32_t>(params.region[2] - params.region[0]);
      uint32_t regionHeight = static_cast<uint32_t>(params.region[3] - params.region[1]);

      if(testComp.w != regionWidth || testComp.h != regionHeight)
      {
        spdlog::error("Region size mismatch for component {}: {}x{} vs {}x{}", i, testComp.w,
                      testComp.h, regionWidth, regionHeight);
        return EXIT_FAILURE;
      }
    }
    else if(baseComp.w != testComp.w || baseComp.h != testComp.h)
    {
      spdlog::error("Dimensions mismatch for component {}: {}x{} vs {}x{}", i, baseComp.w,
                    baseComp.h, testComp.w, testComp.h);
      return EXIT_FAILURE;
    }
  }

  spdlog::info("---- TEST SUCCEEDED ----");
  return EXIT_SUCCESS;
}

} // namespace grk