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
#include <algorithm>

namespace grk
{
/* Table A.53 from JPEG2000 standard */
static const uint16_t tabMaxSubLevelFromMainLevel[] = {15, /* unspecified */
													   1,  1, 1, 2, 3, 4, 5, 6, 7, 8, 9};

int Profile::get_imf_max_NL(grk_cparameters* parameters, GrkImage* image)
{
   /* Decomposition levels */
   const uint16_t rsiz = parameters->rsiz;
   const uint16_t profile = GRK_GET_IMF_OR_BROADCAST_PROFILE(rsiz);
   const uint32_t XTsiz = parameters->tile_size_on ? (uint32_t)parameters->t_width : image->x1;
   switch(profile)
   {
	  case GRK_PROFILE_IMF_2K:
		 return 5;
	  case GRK_PROFILE_IMF_4K:
		 return 6;
	  case GRK_PROFILE_IMF_8K:
		 return 7;
	  case GRK_PROFILE_IMF_2K_R: {
		 if(XTsiz >= 2048)
			return 5;
		 else if(XTsiz >= 1024)
			return 4;
		 break;
	  }
	  case GRK_PROFILE_IMF_4K_R: {
		 if(XTsiz >= 4096)
			return 6;
		 else if(XTsiz >= 2048)
			return 5;
		 else if(XTsiz >= 1024)
			return 4;
		 break;
	  }
	  case GRK_PROFILE_IMF_8K_R: {
		 if(XTsiz >= 8192)
			return 7;
		 else if(XTsiz >= 4096)
			return 6;
		 else if(XTsiz >= 2048)
			return 5;
		 else if(XTsiz >= 1024)
			return 4;
		 break;
	  }
	  default:
		 break;
   }
   return -1;
}

void Profile::set_imf_parameters(grk_cparameters* parameters, GrkImage* image)
{
   const uint16_t rsiz = parameters->rsiz;
   const uint16_t profile = GRK_GET_IMF_OR_BROADCAST_PROFILE(rsiz);

   /* Override defaults set by set_default_compressor_parameters */
   if(parameters->cblockw_init == GRK_COMP_PARAM_DEFAULT_CBLOCKW &&
	  parameters->cblockh_init == GRK_COMP_PARAM_DEFAULT_CBLOCKH)
   {
	  parameters->cblockw_init = 32;
	  parameters->cblockh_init = 32;
   }

   /* One tile part for each component */
   parameters->newTilePartProgressionDivider = 'C';
   parameters->enableTilePartGeneration = true;

   if(parameters->prog_order == GRK_DEFAULT_PROG_ORDER)
	  parameters->prog_order = GRK_CPRL;

   if(profile == GRK_PROFILE_IMF_2K || profile == GRK_PROFILE_IMF_4K ||
	  profile == GRK_PROFILE_IMF_8K)
	  /* 9-7 transform */
	  parameters->irreversible = true;

   /* Adjust the number of resolutions if set to its defaults */
   if(parameters->numresolution == GRK_DEFAULT_NUMRESOLUTION && image->x0 == 0 && image->y0 == 0)
   {
	  const int max_NL = Profile::get_imf_max_NL(parameters, image);
	  if(max_NL >= 0 && parameters->numresolution > (uint32_t)max_NL)
		 parameters->numresolution = (uint8_t)(max_NL + 1);

	  /* Note: below is generic logic */
	  if(!parameters->tile_size_on)
	  {
		 while(parameters->numresolution > 0)
		 {
			if(image->x1 < (1U << ((uint32_t)parameters->numresolution - 1U)))
			{
			   parameters->numresolution--;
			   continue;
			}
			if(image->y1 < (1U << ((uint32_t)parameters->numresolution - 1U)))
			{
			   parameters->numresolution--;
			   continue;
			}
			break;
		 }
	  }
   }

   /* Set defaults precincts */
   if(parameters->csty == 0)
   {
	  parameters->csty |= J2K_CP_CSTY_PRT;
	  if(parameters->numresolution == 1)
	  {
		 parameters->res_spec = 1;
		 parameters->prcw_init[0] = 128;
		 parameters->prch_init[0] = 128;
	  }
	  else
	  {
		 parameters->res_spec = (uint32_t)(parameters->numresolution - 1);
		 for(uint32_t i = 0; i < parameters->res_spec; i++)
		 {
			parameters->prcw_init[i] = 256;
			parameters->prch_init[i] = 256;
		 }
	  }
   }
}

bool Profile::is_imf_compliant(grk_cparameters* parameters, GrkImage* image)
{
   assert(parameters->numresolution > 0);
   if(parameters->numresolution == 0)
	  return false;
   const uint16_t rsiz = parameters->rsiz;
   const uint16_t profile = GRK_GET_IMF_OR_BROADCAST_PROFILE(rsiz);
   const uint16_t mainlevel = GRK_GET_LEVEL(rsiz);
   const uint16_t sublevel = GRK_GET_IMF_SUBLEVEL(rsiz);
   const uint32_t NL = (uint32_t)(parameters->numresolution - 1);
   const uint32_t XTsiz = parameters->tile_size_on ? parameters->t_width : image->x1;
   bool ret = true;

   /* Validate mainlevel */
   if(mainlevel > GRK_LEVEL_MAX)
   {
	  Logger::logger_.warn("IMF profiles require mainlevel <= 11.\n"
						   "-> %u is thus not compliant\n"
						   "-> Non-IMF code stream will be generated",
						   mainlevel);
	  ret = false;
   }

   /* Validate sublevel */
   assert(sizeof(tabMaxSubLevelFromMainLevel) ==
		  (GRK_LEVEL_MAX + 1) * sizeof(tabMaxSubLevelFromMainLevel[0]));
   if(sublevel > tabMaxSubLevelFromMainLevel[mainlevel])
   {
	  Logger::logger_.warn("IMF profiles require sublevel <= %u for mainlevel = %u.\n"
						   "-> %u is thus not compliant\n"
						   "-> Non-IMF code stream will be generated",
						   tabMaxSubLevelFromMainLevel[mainlevel], mainlevel, sublevel);
	  ret = false;
   }
   int m = std::max((int)mainlevel - 2, 1);
   if((int)sublevel > m)
   {
	  Logger::logger_.warn("JPEG 2000 IMF profile: invalid sub-level %u", sublevel);
	  ret = false;
   }

   /* Number of components */
   if(image->numcomps > 3)
   {
	  Logger::logger_.warn("IMF profiles require at most 3 components.\n"
						   "-> Number of components of input image (%u) is not compliant\n"
						   "-> Non-IMF code stream will be generated",
						   image->numcomps);
	  ret = false;
   }

   if(image->x0 != 0 || image->y0 != 0)
   {
	  Logger::logger_.warn("IMF profiles require image origin to be at (0,0).\n"
						   "-> (%u,%u) is not compliant\n"
						   "-> Non-IMF code stream will be generated",
						   image->x0, image->y0);
	  ret = false;
   }

   if(parameters->tx0 != 0 || parameters->ty0 != 0)
   {
	  Logger::logger_.warn("IMF profiles require tile origin to be at (0,0).\n"
						   "-> (%u,%u) is not compliant\n"
						   "-> Non-IMF code stream will be generated",
						   parameters->tx0, parameters->ty0);
	  ret = false;
   }

   if(parameters->tile_size_on)
   {
	  if(profile == GRK_PROFILE_IMF_2K || profile == GRK_PROFILE_IMF_4K ||
		 profile == GRK_PROFILE_IMF_8K)
	  {
		 if(parameters->t_width < image->x1 || (uint32_t)parameters->t_height < image->y1)
		 {
			Logger::logger_.warn(
				"IMF 2K/4K/8K single tile profiles require tile to be greater or equal to "
				"image size.\n"
				"-> %u,%u is lesser than %u,%u\n"
				"-> Non-IMF code stream will be generated",
				parameters->t_width, parameters->t_height, image->x1, image->y1);
			ret = false;
		 }
	  }
	  else
	  {
		 if((uint32_t)parameters->t_width >= image->x1 &&
			(uint32_t)parameters->t_height >= image->y1)
		 {
			/* ok */
		 }
		 else if(parameters->t_width == 1024 && parameters->t_height == 1024)
		 {
			/* ok */
		 }
		 else if(parameters->t_width == 2048 && parameters->t_height == 2048 &&
				 (profile == GRK_PROFILE_IMF_4K || profile == GRK_PROFILE_IMF_8K))
		 {
			/* ok */
		 }
		 else if(parameters->t_width == 4096 && parameters->t_height == 4096 &&
				 profile == GRK_PROFILE_IMF_8K)
		 {
			/* ok */
		 }
		 else
		 {
			Logger::logger_.warn("IMF 2K_R/4K_R/8K_R single/multiple tile profiles "
								 "require tile to be greater or equal to image size,\n"
								 "or to be (1024,1024), or (2048,2048) for 4K_R/8K_R "
								 "or (4096,4096) for 8K_R.\n"
								 "-> %u,%u is non conformant\n"
								 "-> Non-IMF code stream will be generated",
								 parameters->t_width, parameters->t_height);
			ret = false;
		 }
	  }
   }

   /* Bitdepth */
   for(uint32_t i = 0; i < image->numcomps; i++)
   {
	  if(!(image->comps[i].prec >= 8 && image->comps[i].prec <= 16) || (image->comps[i].sgnd))
	  {
		 char signed_str[] = "signed";
		 char unsigned_str[] = "unsigned";
		 char* tmp_str = image->comps[i].sgnd ? signed_str : unsigned_str;
		 Logger::logger_.warn(
			 "IMF profiles require precision of each component to b in [8-16] bits unsigned"
			 "-> At least component %u of input image (%u bits, %s) is not compliant\n"
			 "-> Non-IMF code stream will be generated",
			 i, image->comps[i].prec, tmp_str);
		 ret = false;
	  }
   }

   /* Sub-sampling */
   for(uint32_t i = 0; i < image->numcomps; i++)
   {
	  if(i == 0 && image->comps[i].dx != 1)
	  {
		 Logger::logger_.warn("IMF profiles require XRSiz1 == 1. Here it is set to %u.\n"
							  "-> Non-IMF code stream will be generated",
							  image->comps[i].dx);
		 ret = false;
	  }
	  if(i == 1 && image->comps[i].dx != 1 && image->comps[i].dx != 2)
	  {
		 Logger::logger_.warn("IMF profiles require XRSiz2 == 1 or 2. Here it is set to %u.\n"
							  "-> Non-IMF code stream will be generated",
							  image->comps[i].dx);
		 ret = false;
	  }
	  if(i > 1 && image->comps[i].dx != image->comps[i - 1].dx)
	  {
		 Logger::logger_.warn("IMF profiles require XRSiz%u to be the same as XRSiz2. "
							  "Here it is set to %u instead of %u.\n"
							  "-> Non-IMF code stream will be generated",
							  i + 1, image->comps[i].dx, image->comps[i - 1].dx);
		 ret = false;
	  }
	  if(image->comps[i].dy != 1)
	  {
		 Logger::logger_.warn("IMF profiles require YRsiz == 1. "
							  "Here it is set to %u for component i.\n"
							  "-> Non-IMF code stream will be generated",
							  image->comps[i].dy, i);
		 ret = false;
	  }
   }

   /* Image size */
   switch(profile)
   {
	  case GRK_PROFILE_IMF_2K:
	  case GRK_PROFILE_IMF_2K_R:
		 if(((image->comps[0].w > 2048) | (image->comps[0].h > 1556)))
		 {
			Logger::logger_.warn("IMF 2K/2K_R profiles require:\n"
								 "width <= 2048 and height <= 1556\n"
								 "-> Input image size %u x %u is not compliant\n"
								 "-> Non-IMF code stream will be generated",
								 image->comps[0].w, image->comps[0].h);
			ret = false;
		 }
		 break;
	  case GRK_PROFILE_IMF_4K:
	  case GRK_PROFILE_IMF_4K_R:
		 if(((image->comps[0].w > 4096) | (image->comps[0].h > 3112)))
		 {
			Logger::logger_.warn("IMF 4K/4K_R profiles require:\n"
								 "width <= 4096 and height <= 3112\n"
								 "-> Input image size %u x %u is not compliant\n"
								 "-> Non-IMF code stream will be generated",
								 image->comps[0].w, image->comps[0].h);
			ret = false;
		 }
		 break;
	  case GRK_PROFILE_IMF_8K:
	  case GRK_PROFILE_IMF_8K_R:
		 if(((image->comps[0].w > 8192) | (image->comps[0].h > 6224)))
		 {
			Logger::logger_.warn("IMF 8K/8K_R profiles require:\n"
								 "width <= 8192 and height <= 6224\n"
								 "-> Input image size %u x %u is not compliant\n"
								 "-> Non-IMF code stream will be generated",
								 image->comps[0].w, image->comps[0].h);
			ret = false;
		 }
		 break;
	  default:
		 assert(0);
		 return false;
   }

   if(parameters->roi_compno != -1)
   {
	  Logger::logger_.warn("IMF profiles forbid RGN / region of interest marker.\n"
						   "-> Compression parameters specify a ROI\n"
						   "-> Non-IMF code stream will be generated");
	  ret = false;
   }

   if(parameters->cblockw_init != 32 || parameters->cblockh_init != 32)
   {
	  Logger::logger_.warn("IMF profiles require code block size to be 32x32.\n"
						   "-> Compression parameter set to %ux%u.\n"
						   "-> Non-IMF code stream will be generated",
						   parameters->cblockw_init, parameters->cblockh_init);
	  ret = false;
   }

   if(parameters->prog_order != GRK_CPRL)
   {
	  Logger::logger_.warn("IMF profiles require progression order to be CPRL.\n"
						   "-> Compression parameter set to %u.\n"
						   "-> Non-IMF code stream will be generated",
						   parameters->prog_order);
	  ret = false;
   }

   if(parameters->numpocs != 0)
   {
	  Logger::logger_.warn("IMF profile forbid POC markers.\n"
						   "-> Compression parameters set %u POC.\n"
						   "-> Non-IMF code stream will be generated",
						   parameters->numpocs);
	  ret = false;
   }

   /* Codeblock style: no mode switch enabled */
   if(parameters->cblk_sty != 0)
   {
	  Logger::logger_.warn("IMF profile forbid mode switch in code block style.\n"
						   "-> Compression parameters set code block style to %u.\n"
						   "-> Non-IMF code stream will be generated",
						   parameters->cblk_sty);
	  ret = false;
   }

   if(profile == GRK_PROFILE_IMF_2K || profile == GRK_PROFILE_IMF_4K ||
	  profile == GRK_PROFILE_IMF_8K)
   {
	  /* Expect 9-7 transform */
	  if(!parameters->irreversible)
	  {
		 Logger::logger_.warn("IMF 2K/4K/8K profiles require 9-7 Irreversible Transform.\n"
							  "-> Compression parameter set to reversible.\n"
							  "-> Non-IMF code stream will be generated");
		 ret = false;
	  }
   }
   else
   {
	  /* Expect 5-3 transform */
	  if(parameters->irreversible)
	  {
		 Logger::logger_.warn("IMF 2K/4K/8K profiles require 5-3 reversible Transform.\n"
							  "-> Compression parameter set to irreversible.\n"
							  "-> Non-IMF code stream will be generated");
		 ret = false;
	  }
   }

   /* Number of layers */
   if(parameters->numlayers != 1)
   {
	  Logger::logger_.warn("IMF 2K/4K/8K profiles require 1 single quality layer.\n"
						   "-> Number of layers is %u.\n"
						   "-> Non-IMF code stream will be generated",
						   parameters->numlayers);
	  ret = false;
   }

   /* Decomposition levels */
   switch(profile)
   {
	  case GRK_PROFILE_IMF_2K:
		 if(!(NL >= 1 && NL <= 5))
		 {
			Logger::logger_.warn("IMF 2K profile requires 1 <= NL <= 5:\n"
								 "-> Number of decomposition levels is %u.\n"
								 "-> Non-IMF code stream will be generated",
								 NL);
			ret = false;
		 }
		 break;
	  case GRK_PROFILE_IMF_4K:
		 if(!(NL >= 1 && NL <= 6))
		 {
			Logger::logger_.warn("IMF 4K profile requires 1 <= NL <= 6:\n"
								 "-> Number of decomposition levels is %u.\n"
								 "-> Non-IMF code stream will be generated",
								 NL);
			ret = false;
		 }
		 break;
	  case GRK_PROFILE_IMF_8K:
		 if(!(NL >= 1 && NL <= 7))
		 {
			Logger::logger_.warn("IMF 8K profile requires 1 <= NL <= 7:\n"
								 "-> Number of decomposition levels is %u.\n"
								 "-> Non-IMF code stream will be generated",
								 NL);
			ret = false;
		 }
		 break;
	  case GRK_PROFILE_IMF_2K_R: {
		 if(XTsiz >= 2048)
		 {
			if(!(NL >= 1 && NL <= 5))
			{
			   Logger::logger_.warn("IMF 2K_R profile requires 1 <= NL <= 5 for XTsiz >= 2048:\n"
									"-> Number of decomposition levels is %u.\n"
									"-> Non-IMF code stream will be generated",
									NL);
			   ret = false;
			}
		 }
		 else if(XTsiz >= 1024)
		 {
			if(!(NL >= 1 && NL <= 4))
			{
			   Logger::logger_.warn(
				   "IMF 2K_R profile requires 1 <= NL <= 4 for XTsiz in [1024,2048[:\n"
				   "-> Number of decomposition levels is %u.\n"
				   "-> Non-IMF code stream will be generated",
				   NL);
			   ret = false;
			}
		 }
		 break;
	  }
	  case GRK_PROFILE_IMF_4K_R: {
		 if(XTsiz >= 4096)
		 {
			if(!(NL >= 1 && NL <= 6))
			{
			   Logger::logger_.warn("IMF 4K_R profile requires 1 <= NL <= 6 for XTsiz >= 4096:\n"
									"-> Number of decomposition levels is %u.\n"
									"-> Non-IMF code stream will be generated",
									NL);
			   ret = false;
			}
		 }
		 else if(XTsiz >= 2048)
		 {
			if(!(NL >= 1 && NL <= 5))
			{
			   Logger::logger_.warn(
				   "IMF 4K_R profile requires 1 <= NL <= 5 for XTsiz in [2048,4096[:\n"
				   "-> Number of decomposition levels is %u.\n"
				   "-> Non-IMF code stream will be generated",
				   NL);
			   ret = false;
			}
		 }
		 else if(XTsiz >= 1024)
		 {
			if(!(NL >= 1 && NL <= 4))
			{
			   Logger::logger_.warn(
				   "IMF 4K_R profile requires 1 <= NL <= 4 for XTsiz in [1024,2048[:\n"
				   "-> Number of decomposition levels is %u.\n"
				   "-> Non-IMF code stream will be generated",
				   NL);
			   ret = false;
			}
		 }
		 break;
	  }
	  case GRK_PROFILE_IMF_8K_R: {
		 if(XTsiz >= 8192)
		 {
			if(!(NL >= 1 && NL <= 7))
			{
			   Logger::logger_.warn("IMF 4K_R profile requires 1 <= NL <= 7 for XTsiz >= 8192:\n"
									"-> Number of decomposition levels is %u.\n"
									"-> Non-IMF code stream will be generated",
									NL);
			   ret = false;
			}
		 }
		 else if(XTsiz >= 4096)
		 {
			if(!(NL >= 1 && NL <= 6))
			{
			   Logger::logger_.warn(
				   "IMF 4K_R profile requires 1 <= NL <= 6 for XTsiz in [4096,8192[:\n"
				   "-> Number of decomposition levels is %u.\n"
				   "-> Non-IMF code stream will be generated",
				   NL);
			   ret = false;
			}
		 }
		 else if(XTsiz >= 2048)
		 {
			if(!(NL >= 1 && NL <= 5))
			{
			   Logger::logger_.warn(
				   "IMF 4K_R profile requires 1 <= NL <= 5 for XTsiz in [2048,4096[:\n"
				   "-> Number of decomposition levels is %u.\n"
				   "-> Non-IMF code stream will be generated",
				   NL);
			   ret = false;
			}
		 }
		 else if(XTsiz >= 1024)
		 {
			if(!(NL >= 1 && NL <= 4))
			{
			   Logger::logger_.warn(
				   "IMF 4K_R profile requires 1 <= NL <= 4 for XTsiz in [1024,2048[:\n"
				   "-> Number of decomposition levels is %u.\n"
				   "-> Non-IMF code stream will be generated",
				   NL);
			   ret = false;
			}
		 }
		 break;
	  }
	  default:
		 break;
   }

   if(parameters->numresolution == 1)
   {
	  if(parameters->res_spec != 1 || parameters->prcw_init[0] != 128 ||
		 parameters->prch_init[0] != 128)
	  {
		 Logger::logger_.warn("IMF profiles require PPx = PPy = 7 for NLLL band, else 8.\n"
							  "-> Supplied values are different from that.\n"
							  "-> Non-IMF code stream will be generated",
							  NL);
		 ret = false;
	  }
   }
   else
   {
	  for(uint32_t i = 0; i < parameters->res_spec; i++)
	  {
		 if(parameters->prcw_init[i] != 256 || parameters->prch_init[i] != 256)
		 {
			Logger::logger_.warn("IMF profiles require PPx = PPy = 7 for NLLL band, else 8.\n"
								 "-> Supplied values are different from that.\n"
								 "-> Non-IMF code stream will be generated",
								 NL);
			ret = false;
		 }
	  }
   }

   return ret;
}

//////////////////////////////////////////////////////////////

int Profile::get_broadcast_max_NL(grk_cparameters* parameters, GrkImage* image)
{
   /* Decomposition levels */
   const uint16_t rsiz = parameters->rsiz;
   const uint16_t profile = GRK_GET_IMF_OR_BROADCAST_PROFILE(rsiz);
   const uint32_t XTsiz = parameters->tile_size_on ? (uint32_t)parameters->t_width : image->x1;
   switch(profile)
   {
	  case GRK_PROFILE_IMF_2K:
		 return 5;
	  case GRK_PROFILE_IMF_4K:
		 return 6;
	  case GRK_PROFILE_IMF_8K:
		 return 7;
	  case GRK_PROFILE_IMF_2K_R: {
		 if(XTsiz >= 2048)
			return 5;
		 else if(XTsiz >= 1024)
			return 4;
		 break;
	  }
	  case GRK_PROFILE_IMF_4K_R: {
		 if(XTsiz >= 4096)
			return 6;
		 else if(XTsiz >= 2048)
			return 5;
		 else if(XTsiz >= 1024)
			return 4;
		 break;
	  }
	  case GRK_PROFILE_IMF_8K_R: {
		 if(XTsiz >= 8192)
			return 7;
		 else if(XTsiz >= 4096)
			return 6;
		 else if(XTsiz >= 2048)
			return 5;
		 else if(XTsiz >= 1024)
			return 4;
		 break;
	  }
	  default:
		 break;
   }
   return -1;
}

void Profile::set_broadcast_parameters(grk_cparameters* parameters)
{
   const uint16_t rsiz = parameters->rsiz;
   const uint16_t profile = GRK_GET_IMF_OR_BROADCAST_PROFILE(rsiz);

   parameters->prog_order = GRK_CPRL;

   /* One tile part for each component */
   parameters->newTilePartProgressionDivider = 'C';
   parameters->enableTilePartGeneration = true;

   /* No ROI */
   parameters->roi_compno = -1;

   /* No subsampling */
   parameters->subsampling_dx = 1;
   parameters->subsampling_dy = 1;

   if(profile != GRK_PROFILE_BC_MULTI_R)
	  parameters->irreversible = true;

   /* Adjust the number of resolutions if set to its defaults */
   if(parameters->numresolution == GRK_DEFAULT_NUMRESOLUTION)
	  parameters->numresolution = 5;

   /* Set defaults precincts */
   if(parameters->csty == 0)
   {
	  parameters->csty |= J2K_CP_CSTY_PRT;
	  if(parameters->numresolution == 1)
	  {
		 parameters->res_spec = 1;
		 parameters->prcw_init[0] = 128;
		 parameters->prch_init[0] = 128;
	  }
	  else
	  {
		 parameters->res_spec = (uint32_t)(parameters->numresolution - 1);
		 for(uint32_t i = 0; i < parameters->res_spec; i++)
		 {
			parameters->prcw_init[i] = 256;
			parameters->prch_init[i] = 256;
		 }
	  }
   }
}

bool Profile::is_broadcast_compliant(grk_cparameters* parameters, GrkImage* image)
{
   assert(parameters->numresolution > 0);
   if(parameters->numresolution == 0 || image->numcomps == 0)
	  return false;
   const uint16_t rsiz = parameters->rsiz;
   const uint16_t profile = GRK_GET_IMF_OR_BROADCAST_PROFILE(rsiz);
   const uint16_t mainlevel = GRK_GET_LEVEL(rsiz);
   const uint32_t NL = (uint32_t)(parameters->numresolution - 1);
   bool ret = true;

   /* Validate mainlevel */
   if(mainlevel > GRK_LEVEL_MAX)
   {
	  Logger::logger_.warn("Broadcast profiles require mainlevel <= 11.\n"
						   "-> %u is thus not compliant\n"
						   "-> Non-broadcast code stream will be generated",
						   mainlevel);
	  ret = false;
   }

   /* Number of components */
   if(image->numcomps > 4)
   {
	  Logger::logger_.warn("Broadcast profiles require at most 4 components.\n"
						   "-> Number of components of input image (%u) is not compliant\n"
						   "-> Non-broadcast code stream will be generated",
						   image->numcomps);
	  ret = false;
   }

   if(image->x0 != 0 || image->y0 != 0)
   {
	  Logger::logger_.warn("Broadcast profiles require image origin to be at (0,0).\n"
						   "-> (%u,%u) is not compliant\n"
						   "-> Non-broadcast code stream will be generated",
						   image->x0, image->y0);
	  ret = false;
   }

   if(parameters->tx0 != 0 || parameters->ty0 != 0)
   {
	  Logger::logger_.warn("Broadcast profiles require tile origin to be at (0,0).\n"
						   "-> (%u,%u) is not compliant\n"
						   "-> Non-broadcast code stream will be generated",
						   parameters->tx0, parameters->ty0);
	  ret = false;
   }

   if(parameters->tile_size_on)
   {
	  if(profile == GRK_PROFILE_BC_SINGLE)
	  {
		 Logger::logger_.warn("Broadcast SINGLE profile requires 1x1 tile layout.\n"
							  "-> Non-broadcast code stream will be generated");
		 ret = false;
	  }

	  // avoid divide by zero
	  if(parameters->t_width == 0 || parameters->t_height == 0)
	  {
		 return false;
	  }
	  auto t_grid_width = ceildiv<uint32_t>((image->x1 - parameters->tx0), parameters->t_width);
	  auto t_grid_height = ceildiv<uint32_t>((image->y1 - parameters->ty0), parameters->t_height);

	  if(!((t_grid_width == 1 && t_grid_height == 1) || (t_grid_width == 2 && t_grid_height == 2) ||
		   (t_grid_width == 1 && t_grid_height == 4)))
	  {
		 Logger::logger_.warn("Tiled broadcast profiles require 2x2 or 1x4 tile layout.\n"
							  "-> (%u,%u) layout is not compliant\n"
							  "-> Non-broadcast code stream will be generated",
							  t_grid_width, t_grid_height);
		 ret = false;
	  }
   }

   /* Bitdepth */
   for(uint32_t i = 0; i < image->numcomps; i++)
   {
	  if(!(image->comps[i].prec >= 8 && image->comps[i].prec <= 12) || (image->comps[i].sgnd))
	  {
		 char signed_str[] = "signed";
		 char unsigned_str[] = "unsigned";
		 char* tmp_str = image->comps[i].sgnd ? signed_str : unsigned_str;
		 Logger::logger_.warn(
			 "Broadcast profiles require precision of each component to b in [8-12] bits "
			 "unsigned"
			 "-> At least component %u of input image (%u bits, %s) is not compliant\n"
			 "-> Non-broadcast code stream will be generated",
			 i, image->comps[i].prec, tmp_str);
		 ret = false;
	  }
   }

   /* Sub-sampling */
   if(image->numcomps >= 3)
   {
	  if(image->comps[1].dx != image->comps[2].dx)
	  {
		 Logger::logger_.warn("Broadcast profiles require XRSiz1 == XRSiz2. "
							  "Here they are set to %u and %u respectively.\n"
							  "-> Non-broadcast code stream will be generated",
							  image->comps[1].dx, image->comps[2].dx);
		 ret = false;
	  }
	  if(image->comps[1].dy != image->comps[2].dy)
	  {
		 Logger::logger_.warn("Broadcast profiles require YRSiz1 == YRSiz2. "
							  "Here they are set to %u and %u respectively.\n"
							  "-> Non-broadcast code stream will be generated",
							  image->comps[1].dy, image->comps[2].dy);
		 ret = false;
	  }
   }
   for(uint16_t i = 0; i < std::min<uint16_t>(image->numcomps, 4U); i++)
   {
	  if(i == 0 || i == 3)
	  {
		 if(image->comps[i].dx != 1)
		 {
			Logger::logger_.warn("Broadcast profiles require XRSiz%u == 1. Here it is set to %u.\n"
								 "-> Non-broadcast code stream will be generated",
								 i + 1, image->comps[i].dx);
			ret = false;
		 }
		 if(image->comps[i].dy != 1)
		 {
			Logger::logger_.warn("Broadcast profiles require YRSiz%u == 1. Here it is set to %u.\n"
								 "-> Non-broadcast code stream will be generated",
								 i + 1, image->comps[i].dy);
			ret = false;
		 }
	  }
	  else
	  {
		 if(image->comps[i].dx > 2)
		 {
			Logger::logger_.warn(
				"Broadcast profiles require XRSiz%u == [1,2]. Here it is set to %u.\n"
				"-> Non-broadcast code stream will be generated",
				i + 1, image->comps[i].dx);
			ret = false;
		 }
		 if(image->comps[i].dy > 2)
		 {
			Logger::logger_.warn(
				"Broadcast profiles require YRSiz%u == [1,2]. Here it is set to %u.\n"
				"-> Non-broadcast code stream will be generated",
				i + 1, image->comps[i].dy);
			ret = false;
		 }
	  }
   }

   if(!((parameters->cblockw_init == 32 && parameters->cblockh_init == 32) ||
		(parameters->cblockw_init == 64 && parameters->cblockh_init == 64) ||
		(parameters->cblockw_init == 128 && parameters->cblockh_init == 128)))
   {
	  Logger::logger_.warn(
		  "Broadcast profiles require each code block dimension to be in [32,64,128].\n"
		  "-> %ux%u is not valid.\n"
		  "-> Non-broadcast code stream will be generated",
		  parameters->cblockw_init, parameters->cblockh_init);
	  ret = false;
   }

   if(parameters->prog_order != GRK_CPRL)
   {
	  Logger::logger_.warn("Broadcast profiles require progression order to be CPRL.\n"
						   "-> Compression parameter set to %u.\n"
						   "-> Non-broadcast code stream will be generated",
						   parameters->prog_order);
	  ret = false;
   }

   if(parameters->numpocs != 0)
   {
	  Logger::logger_.warn("Broadcast profiles forbid POC markers.\n"
						   "-> Compression parameters set %u POC.\n"
						   "-> Non-broadcast code stream will be generated",
						   parameters->numpocs);
	  ret = false;
   }

   /* Codeblock style: no mode switch enabled */
   if(parameters->cblk_sty != 0)
   {
	  Logger::logger_.warn("Broadcast profiles forbid mode switch in code block style.\n"
						   "-> Compression parameters set code block style to %u.\n"
						   "-> Non-broadcast code stream will be generated",
						   parameters->cblk_sty);
	  ret = false;
   }

   if(profile == GRK_PROFILE_BC_SINGLE || profile == GRK_PROFILE_BC_MULTI)
   {
	  /* Expect 9-7 transform */
	  if(!parameters->irreversible)
	  {
		 Logger::logger_.warn(
			 "Broadcast single and multi profiles require 9-7 Irreversible Transform.\n"
			 "-> Compression parameter set to reversible.\n"
			 "-> Non-broadcast code stream will be generated");
		 ret = false;
	  }
   }
   else
   {
	  /* Expect 5-3 transform */
	  if(parameters->irreversible)
	  {
		 Logger::logger_.warn("Broadcast multi_r profile require 5-3 reversible Transform.\n"
							  "-> Compression parameter set to irreversible.\n"
							  "-> Non-broadcast code stream will be generated");
		 ret = false;
	  }
   }

   /* Number of layers */
   if(parameters->numlayers != 1)
   {
	  Logger::logger_.warn("Broadcast profiles require 1 single quality layer.\n"
						   "-> Number of layers is %u.\n"
						   "-> Non-broadcast code stream will be generated",
						   parameters->numlayers);
	  ret = false;
   }

   /* Decomposition levels */
   if(!(NL >= 1 && NL <= 5))
   {
	  Logger::logger_.warn("Broadcast profiles requires 1 <= NL <= 5:\n"
						   "-> Number of decomposition levels is %u.\n"
						   "-> Non-broadcast code stream will be generated",
						   NL);
	  ret = false;
   }

   if(parameters->numresolution == 1)
   {
	  if(parameters->res_spec != 1 || parameters->prcw_init[0] != 128 ||
		 parameters->prch_init[0] != 128)
	  {
		 Logger::logger_.warn("Broadcast profiles require PPx = PPy = 7 for NLLL band, else 8.\n"
							  "-> Supplied values are different from that.\n"
							  "-> Non-broadcast code stream will be generated",
							  NL);
		 ret = false;
	  }
   }
   else
   {
	  for(uint32_t i = 0; i < parameters->res_spec; i++)
	  {
		 if(parameters->prcw_init[i] != 256 || parameters->prch_init[i] != 256)
		 {
			Logger::logger_.warn(
				"Broadcast profiles require PPx = PPy = 7 for NLLL band, otherwise 8.\n"
				"-> Supplied values are different from this specification.\n"
				"-> Non-broadcast code stream will be generated",
				NL);
			ret = false;
		 }
	  }
   }

   return ret;
}

/*****************
 * Cinema Profile
 *****************/

void Profile::initialise_4K_poc(grk_progression* POC, uint8_t numres)
{
   assert(numres > 0);
   POC[0].tileno = 0;
   POC[0].resS = 0;
   POC[0].compS = 0;
   POC[0].layE = 1;
   POC[0].resE = (uint8_t)(numres - 1);
   POC[0].compE = 3;
   POC[0].specifiedCompressionPocProg = GRK_CPRL;
   POC[1].tileno = 0;
   POC[1].resS = (uint8_t)(numres - 1);
   POC[1].compS = 0;
   POC[1].layE = 1;
   POC[1].resE = numres;
   POC[1].compE = 3;
   POC[1].specifiedCompressionPocProg = GRK_CPRL;
}

void Profile::set_cinema_parameters(grk_cparameters* parameters, GrkImage* image)
{
   /* No tiling */
   parameters->tile_size_on = false;
   parameters->t_width = 1;
   parameters->t_height = 1;

   /* One tile part for each component */
   parameters->newTilePartProgressionDivider = 'C';
   parameters->enableTilePartGeneration = true;

   /* Tile and Image shall be at (0,0) */
   parameters->tx0 = 0;
   parameters->ty0 = 0;
   parameters->image_offset_x0 = 0;
   parameters->image_offset_y0 = 0;

   /* Codeblock size= 32*32 */
   parameters->cblockw_init = 32;
   parameters->cblockh_init = 32;

   /* Codeblock style: no mode switch enabled */
   parameters->cblk_sty = 0;

   /* No ROI */
   parameters->roi_compno = -1;

   /* No subsampling */
   parameters->subsampling_dx = 1;
   parameters->subsampling_dy = 1;

   /* 9-7 transform */
   parameters->irreversible = true;

   /* Number of layers */
   if(parameters->numlayers > 1)
   {
	  Logger::logger_.warn("JPEG 2000 profiles 3 and 4 (2k and 4k digital cinema) require:\n"
						   "1 single quality layer"
						   "-> Number of layers forced to 1 (rather than %u)\n"
						   "-> Rate of the last layer (%3.1f) will be used",
						   parameters->numlayers,
						   parameters->layer_rate[parameters->numlayers - 1]);
	  parameters->layer_rate[0] = parameters->layer_rate[parameters->numlayers - 1];
	  parameters->numlayers = 1;
   }

   /* Resolution levels */
   switch(parameters->rsiz)
   {
	  case GRK_PROFILE_CINEMA_2K:
		 if(parameters->numresolution > 6)
		 {
			Logger::logger_.warn("JPEG 2000 profile 3 (2k digital cinema) requires:\n"
								 "Number of decomposition levels <= 5\n"
								 "-> Number of decomposition levels forced to 5 (rather than %u)",
								 parameters->numresolution - 1);
			parameters->numresolution = 6;
		 }
		 break;
	  case GRK_PROFILE_CINEMA_4K:
		 if(parameters->numresolution < 2)
		 {
			Logger::logger_.warn("JPEG 2000 profile 4 (4k digital cinema) requires:\n"
								 "Number of decomposition levels >= 1 && <= 6\n"
								 "-> Number of decomposition levels forced to 1 (rather than %u)",
								 0);
			parameters->numresolution = 2;
		 }
		 else if(parameters->numresolution > 7)
		 {
			Logger::logger_.warn("JPEG 2000 profile 4 (4k digital cinema) requires:\n"
								 "Number of decomposition levels >= 1 && <= 6\n"
								 "-> Number of decomposition levels forced to 6 (rather than %u)",
								 parameters->numresolution - 1);
			parameters->numresolution = 7;
		 }
		 break;
	  default:
		 break;
   }

   /* Precincts */
   parameters->csty |= J2K_CP_CSTY_PRT;
   parameters->res_spec = (uint32_t)(parameters->numresolution - 1);
   for(uint32_t i = 0; i < parameters->res_spec; i++)
   {
	  parameters->prcw_init[i] = 256;
	  parameters->prch_init[i] = 256;
   }

   /* The progression order shall be CPRL */
   parameters->prog_order = GRK_CPRL;

   /* Progression order changes for 4K, disallowed for 2K */
   if(parameters->rsiz == GRK_PROFILE_CINEMA_4K)
   {
	  Profile::initialise_4K_poc(parameters->progression, parameters->numresolution);
	  parameters->numpocs = 1;
	  parameters->numgbits = 2;
   }
   else
   {
	  parameters->numpocs = 0;
	  parameters->numgbits = 1;
   }

   /* Limit bit-rate */
   parameters->allocationByRateDistoration = true;
   if(parameters->max_cs_size == 0)
   {
	  /* No rate has been introduced for code stream, so 24 fps is assumed */
	  parameters->max_cs_size = GRK_CINEMA_24_CS;
	  parameters->framerate = 24;
	  Logger::logger_.warn(
		  "JPEG 2000 profiles 3 and 4 (2k and 4k digital cinema) require:\n"
		  "Maximum 1302083 compressed bytes @ 24fps for code stream.\n"
		  "As no rate has been given for entire code stream, this limit will be used.");
   }
   if(parameters->max_comp_size == 0)
   {
	  /* No rate has been introduced for each component, so 24 fps is assumed */
	  parameters->max_comp_size = GRK_CINEMA_24_COMP;
	  parameters->framerate = 24;
	  Logger::logger_.warn("JPEG 2000 profiles 3 and 4 (2k and 4k digital cinema) require:\n"
						   "Maximum 1041666 compressed bytes @ 24fps per component.\n"
						   "As no rate has been given, this limit will be used.");
   }
   parameters->layer_rate[0] =
	   ((double)image->numcomps * image->comps[0].w * image->comps[0].h * image->comps[0].prec) /
	   ((double)parameters->max_cs_size * 8 * image->comps[0].dx * image->comps[0].dy);
}

bool Profile::is_cinema_compliant(GrkImage* image, uint16_t rsiz)
{
   /* Number of components */
   if(image->numcomps != 3)
   {
	  Logger::logger_.warn("JPEG 2000 profile 3 (2k digital cinema) requires:\n"
						   "3 components"
						   "-> Number of components of input image (%u) is not compliant\n"
						   "-> Non-profile-3 code stream will be generated",
						   image->numcomps);
	  return false;
   }

   /* Bitdepth */
   for(uint32_t i = 0; i < image->numcomps; i++)
   {
	  if((image->comps[i].prec != 12) | (image->comps[i].sgnd))
	  {
		 char signed_str[] = "signed";
		 char unsigned_str[] = "unsigned";
		 char* tmp_str = image->comps[i].sgnd ? signed_str : unsigned_str;
		 Logger::logger_.warn(
			 "JPEG 2000 profile 3 (2k digital cinema) requires:\n"
			 "Precision of each component shall be 12 bits unsigned"
			 "-> At least component %u of input image (%u bits, %s) is not compliant\n"
			 "-> Non-profile-3 code stream will be generated",
			 i, image->comps[i].prec, tmp_str);
		 return false;
	  }
   }

   /* Image size */
   switch(rsiz)
   {
	  case GRK_PROFILE_CINEMA_2K:
		 if(((image->comps[0].w > 2048) | (image->comps[0].h > 1080)))
		 {
			Logger::logger_.warn("JPEG 2000 profile 3 (2k digital cinema) requires:\n"
								 "width <= 2048 and height <= 1080\n"
								 "-> Input image size %u x %u is not compliant\n"
								 "-> Non-profile-3 code stream will be generated",
								 image->comps[0].w, image->comps[0].h);
			return false;
		 }
		 break;
	  case GRK_PROFILE_CINEMA_4K:
		 if(((image->comps[0].w > 4096) | (image->comps[0].h > 2160)))
		 {
			Logger::logger_.warn("JPEG 2000 profile 4 (4k digital cinema) requires:\n"
								 "width <= 4096 and height <= 2160\n"
								 "-> Image size %u x %u is not compliant\n"
								 "-> Non-profile-4 code stream will be generated",
								 image->comps[0].w, image->comps[0].h);
			return false;
		 }
		 break;
	  default:
		 break;
   }

   return true;
}

} // namespace grk
