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
 */
#include <cstdio>
#include <cstdlib>
#include "grk_apps_config.h"
#include "grok.h"
#include "spdlog/spdlog.h"
#include "JPEGFormat.h"
#include "convert.h"
#include <cstring>

#ifndef GROK_HAVE_LIBJPEG
#error GROK_HAVE_LIBJPEG_NOT_DEFINED
#endif /* GROK_HAVE_LIBJPEG */

#include <setjmp.h>
#include <cassert>

#include "common.h"
#include "FileStreamIO.h"

struct my_error_mgr
{
   struct jpeg_error_mgr pub; /* "public" fields */

   jmp_buf setjmp_buffer; /* for return to caller */
};

typedef struct my_error_mgr* my_error_ptr;

METHODDEF(void) my_error_exit(j_common_ptr cinfo)
{
   /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
   my_error_ptr myerr = (my_error_ptr)cinfo->err;

   /* Always display the message. */
   /* We could postpone this until after returning, if we chose. */
   (*cinfo->err->output_message)(cinfo);

   /* Return control to the setjmp point */
   longjmp(myerr->setjmp_buffer, 1);
}

/*
 * SOME FINE POINTS:
 *
 * In the code below, we ignored the return value of jpeg_read_scanlines,
 * which is the number of scanlines actually read.  We could get away with
 * this because we asked for only one line at a time and we weren't using
 * a suspending data source.  See libjpeg.txt for more info.
 *
 * We cheated a bit by calling alloc_sarray() after jpeg_start_decompress();
 * we should have done it beforehand to ensure that the space would be
 * counted against the JPEG max_memory setting.  In some systems the above
 * code would risk an out-of-memory error.  However, in general we don't
 * know the output image dimensions before jpeg_start_decompress(), unless we
 * call jpeg_calc_output_dimensions().  See libjpeg.txt for more about this.
 *
 * Scanlines are returned in the same order as they appear in the JPEG file,
 * which is standardly top-to-bottom.  If you must emit data bottom-to-top,
 * you can use one of the virtual arrays provided by the JPEG memory manager
 * to invert the data.  See wrbmp.c for an example.
 *
 * As with compression, some operating modes may require temporary files.
 * On some systems you may need to set up a signal handler to ensure that
 * temporary files are deleted if the program is interrupted.  See libjpeg.txt.
 */

grk_image* JPEGFormat::jpegtoimage(const char* filename, grk_cparameters* parameters)
{
   readFromStdin = grk::useStdio(std::string(filename));

   int32_t* planes[3];
   JDIMENSION w = 0, h = 0;
   uint32_t dest_stride;
   int bps = 0, decompress_num_comps = 0;
   cvtTo32 cvtJpegTo32s;
   GRK_COLOR_SPACE color_space = GRK_CLRSPC_UNKNOWN;
   grk_image_comp cmptparm[3]; /* mono or RGB */
   cvtInterleavedToPlanar cvtToPlanar;
   JOCTET* icc_data_ptr = nullptr;
   unsigned int icc_data_len = 0;

   /* This struct contains the JPEG decompression parameters and pointers to
	* working space (which is allocated as needed by the JPEG library).
	*/
   struct jpeg_decompress_struct cinfo;
   /* We use our private extension JPEG error handler.
	* Note that this struct must live as long as the main JPEG parameter
	* struct, to avoid dangling-pointer problems.
	*/
   struct my_error_mgr jerr;
   /* More stuff */
   JSAMPARRAY buffer; /* Output row buffer */
   int row_stride; /* physical row width in output buffer */

   /* In this example we want to open the input file before doing anything else,
	* so that the setjmp() error recovery below can assume the file is open.
	* VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
	* requires it in order to read binary files.
	*/

   if(readFromStdin)
   {
	  if(!grk::grk_set_binary_mode(stdin))
		 return nullptr;
	  fileStream_ = stdin;
   }
   else
   {
	  if((fileStream_ = fopen(filename, "rb")) == nullptr)
	  {
		 spdlog::error("can't open {}", filename);
		 return 0;
	  }
   }

   /* Step 1: allocate and initialize JPEG decompression object */

   /* We set up the normal JPEG error routines, then override error_exit. */
   cinfo.err = jpeg_std_error(&jerr.pub);
   jerr.pub.error_exit = my_error_exit;
   /* Establish the setjmp return context for my_error_exit to use. */
   if(setjmp(jerr.setjmp_buffer))
   {
	  success = false;
	  goto cleanup;
   }
   /* Now we can initialize the JPEG decompression object. */
   jpeg_create_decompress(&cinfo);
   setup_read_icc_profile(&cinfo);

   /* Step 2: specify data source (eg, a file) */
   jpeg_stdio_src(&cinfo, fileStream_);

   /* Step 3: read file parameters with jpeg_read_header() */

   jpeg_read_header(&cinfo, TRUE);
   /* We can ignore the return value from jpeg_read_header since
	*   (a) suspension is not possible with the stdio data source, and
	*   (b) we passed TRUE to reject a tables-only JPEG file as an error.
	* See libjpeg.txt for more info.
	*/

   // read ICC profile
   if(!read_icc_profile(&cinfo, &icc_data_ptr, &icc_data_len))
   {
	  spdlog::warn("jpegtoimage: Failed to read ICC profile");
   }

   /* Step 4: set parameters for decompression */

   /* In this example, we don't need to change any of the defaults set by
	* jpeg_read_header(), so we do nothing here.
	*/

   /* Step 5: Start decompressor */

   jpeg_start_decompress(&cinfo);
   /* We can ignore the return value since suspension is not possible
	* with the stdio data source.
	*/

   bps = cinfo.data_precision;
   if(bps != 8)
   {
	  spdlog::error("jpegtoimage: Unsupported image_ precision {}", bps);
	  success = false;
	  goto cleanup;
   }

   decompress_num_comps = cinfo.output_components;
   w = cinfo.image_width;
   h = cinfo.image_height;
   cvtJpegTo32s = cvtTo32_LUT[bps];
   memset(&cmptparm[0], 0, 3 * sizeof(grk_image_comp));
   cvtToPlanar = cvtInterleavedToPlanar_LUT[decompress_num_comps];

   if(cinfo.output_components == 3)
	  color_space = GRK_CLRSPC_SRGB;
   else
	  color_space = GRK_CLRSPC_GRAY;

   for(int j = 0; j < cinfo.output_components; j++)
   {
	  cmptparm[j].prec = (uint8_t)bps;
	  cmptparm[j].dx = 1;
	  cmptparm[j].dy = 1;
	  cmptparm[j].w = w;
	  cmptparm[j].h = h;
   }

   image_ = grk_image_new((uint16_t)decompress_num_comps, &cmptparm[0], color_space, true);
   if(!image_)
   {
	  success = false;
	  goto cleanup;
   }
   if(icc_data_ptr && icc_data_len)
   {
	  copy_icc(image_, icc_data_ptr, icc_data_len);
	  icc_data_len = 0;
   }
   free(icc_data_ptr);
   icc_data_ptr = nullptr;
   /* set image_ offset and reference grid */
   image_->x0 = parameters->image_offset_x0;
   image_->x1 = !image_->x0 ? (w - 1) * 1 + 1 : image_->x0 + (w - 1) * 1 + 1;
   if(image_->x1 <= image_->x0)
   {
	  spdlog::error("jpegtoimage: Bad value for image_->x1({}) vs. "
					"image_->x0({}).",
					image_->x1, image_->x0);
	  success = false;
	  goto cleanup;
   }

   image_->y0 = parameters->image_offset_y0;
   image_->y1 = !image_->y0 ? (h - 1) * 1 + 1 : image_->y0 + (h - 1) * 1 + 1;

   if(image_->y1 <= image_->y0)
   {
	  spdlog::error("jpegtoimage: Bad value for image_->y1({}) vs. "
					"image_->y0({}).",
					image_->y1, image_->y0);
	  success = false;
	  goto cleanup;
   }

   for(int j = 0; j < decompress_num_comps; j++)
   {
	  planes[j] = image_->comps[j].data;
   }

   buffer32s = new int32_t[w * (size_t)decompress_num_comps];

   /* We may need to do some setup of our own at this point before reading
	* the data.  After jpeg_start_decompress() we have the correct scaled
	* output image_ dimensions available, as well as the output colormap
	* if we asked for color quantization.
	* In this example, we need to make an output work buffer of the right size.
	*/
   /* JSAMPLEs per row in output buffer */
   row_stride = (int)cinfo.output_width * cinfo.output_components;
   /* Make a one-row-high sample array that will go away when done with image_ */
   buffer =
	   (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, (JDIMENSION)row_stride, 1);
   if(!buffer)
   {
	  success = false;
	  goto cleanup;
   }

   /* Step 6: while (scan lines remain to be read) */
   /*           jpeg_read_scanlines(...); */

   /* Here we use the library's state variable cinfo.output_scanline as the
	* loop counter, so that we don't have to keep track ourselves.
	*/
   dest_stride = image_->comps[0].stride;
   while(cinfo.output_scanline < cinfo.output_height)
   {
	  /* jpeg_read_scanlines expects an array of pointers to scanlines.
	   * Here the array is only one element long, but you could ask for
	   * more than one scanline at a time if that's more convenient.
	   */
	  jpeg_read_scanlines(&cinfo, buffer, 1);

	  // convert 8 bit buffer to 32 bit buffer
	  cvtJpegTo32s(buffer[0], buffer32s, (size_t)w * (size_t)decompress_num_comps, false);

	  // convert to planar
	  cvtToPlanar(buffer32s, planes, (size_t)w);

	  planes[0] += dest_stride;
	  planes[1] += dest_stride;
	  planes[2] += dest_stride;
   }

   /* Step 7: Finish decompression */

   jpeg_finish_decompress(&cinfo);
/* We can ignore the return value since suspension is not possible
 * with the stdio data source.
 */
cleanup:
   free(icc_data_ptr);
   jpeg_destroy_decompress(&cinfo);

   delete[] buffer32s;

   /* At this point you may want to check to see whether any corrupt-data
	* warnings occurred (test whether jerr.pub.num_warnings is nonzero).
	*/
   if(jerr.pub.num_warnings != 0)
   {
	  spdlog::warn("JPEG library reported {} of corrupt data warnings", (int)jerr.pub.num_warnings);
   }

   if(!success)
   {
	  grk_object_unref(&image_->obj);
	  image_ = nullptr;
   }
   /* After finish_decompress, we can close the input file.
	* Here we postpone it until after no more JPEG errors are possible,
	* so as to simplify the setjmp error logic above.  (Actually, I don't
	* think that jpeg_destroy can do an error exit, but why assume anything...)
	*/
   if(fileStream_ && !readFromStdin)
   {
	  if(!grk::safe_fclose(fileStream_))
	  {
		 grk_object_unref(&image_->obj);
		 image_ = nullptr;
	  }
   }
   return image_;
} /* jpegtoimage() */

JPEGFormat::JPEGFormat(void)
	: success(true), buffer(nullptr), buffer32s(nullptr), color_space(JCS_UNKNOWN), adjust(0),
	  readFromStdin(false), planes{0, 0, 0}
{}

bool JPEGFormat::encodeHeader(void)
{
   if(isHeaderEncoded())
	  return true;

   int32_t firstAlpha = -1;
   size_t numAlphaChannels = 0;
   uint32_t width = image_->decompress_width;

   // actual bits per sample
   uint8_t prec = image_->decompress_prec;
   uint16_t decompress_num_comps = image_->decompress_num_comps;
   uint32_t sgnd = image_->comps[0].sgnd;
   adjust = sgnd ? 1 << (prec - 1) : 0;

   uint32_t i = 0;

   struct my_error_mgr jerr;

   /* Step 1: allocate and initialize JPEG compression object */

   /* We have to set up the error handler first, in case the initialization
	* step fails.  (Unlikely, but it could happen if you are out of memory.)
	* This routine fills in the contents of struct jerr, and returns jerr's
	* address which we place into the link field in cinfo.
	*/

   /* This struct represents a JPEG error handler.  It is declared separately
	* because applications often want to supply a specialized error handler
	* (see the second half of this file for an example).  But here we just
	* take the easy way out and use the standard error handler, which will
	* print a message on stderr and return 1 if compression fails.
	* Note that this struct must live as long as the main JPEG parameter
	* struct, to avoid dangling-pointer problems.
	*/
   JDIMENSION image_width = image_->x1 - image_->x0; /* input image_ width */
   JDIMENSION image_height = image_->y1 - image_->y0; /* input image_ height */

   // sub-sampling not supported at the moment
   if(isFinalOutputSubsampled(image_))
   {
	  spdlog::error("JPEGFormat::encodeHeader: subsampling not currently supported.");
	  return false;
   }

   switch(image_->color_space)
   {
	  case GRK_CLRSPC_SRGB: /**< sRGB */
		 color_space = JCS_RGB;
		 break;
	  case GRK_CLRSPC_GRAY: /**< grayscale */
		 color_space = JCS_GRAYSCALE;
		 break;
	  case GRK_CLRSPC_SYCC: /**< YUV */
		 color_space = JCS_YCbCr;
		 break;
	  case GRK_CLRSPC_EYCC: /**< e-YCC */
		 color_space = JCS_YCCK;
		 break;
	  case GRK_CLRSPC_CMYK: /**< CMYK */
		 color_space = JCS_CMYK;
		 break;
	  default:
		 if(decompress_num_comps == 3)
			color_space = JCS_RGB;
		 else if(decompress_num_comps == 1)
			color_space = JCS_GRAYSCALE;
		 else
		 {
			spdlog::error("JPEGFormat::encodeHeader: unrecognized colour space");
		 }
		 break;
   }

   if(image_->decompress_num_comps > 4)
   {
	  spdlog::error("JPEGFormat::encodeHeader: number of components {} "
					"is greater than 4.",
					image_->decompress_num_comps);
	  return false;
   }
   if(!allComponentsSanityCheck(image_, true))
	  return false;

   planes[0] = image_->comps[0].data;
   for(i = 1U; i < decompress_num_comps; ++i)
	  planes[i] = image_->comps[i].data;

   if(prec != 1 && prec != 2 && prec != 4 && prec != 8)
   {
	  spdlog::error("JPEGFormat::encodeHeader: can not create {}\n\twrong bit_depth {}", fileName_,
					prec);
	  return false;
   }
   // Alpha channels
   for(i = 0U; i < decompress_num_comps; ++i)
   {
	  if(image_->comps[i].type)
	  {
		 if(firstAlpha == -1)
			firstAlpha = 0;
		 numAlphaChannels++;
	  }
   }
   // We assume that alpha channels occur as last channels in image_.
   if(numAlphaChannels && ((uint32_t)firstAlpha + numAlphaChannels >= decompress_num_comps))
   {
	  spdlog::warn("JPEGFormat::encodeHeader: PNG requires that alpha channels occur"
				   " as last channels in image_.");
	  numAlphaChannels = 0;
   }
   buffer = new uint8_t[width * decompress_num_comps];
   buffer32s = new int32_t[width * decompress_num_comps];

   /* We set up the normal JPEG error routines, then override error_exit. */
   cinfo.err = jpeg_std_error(&jerr.pub);
   jerr.pub.error_exit = my_error_exit;
   /* Establish the setjmp return context for my_error_exit to use. */
   if(setjmp(jerr.setjmp_buffer))
   {
	  /* If we get here, the JPEG code has signaled an error.
	   * We need to clean up the JPEG object, close the input file, and return.
	   */
	  jpeg_destroy_compress(&cinfo);
	  return false;
   }
   /* Now we can initialize the JPEG compression object. */
   jpeg_create_compress(&cinfo);

   /* Step 2: specify data destination (eg, a file) */
   /* Note: steps 2 and 3 can be done in either order. */

   /* Here we use the library-supplied code to send compressed data to a
	* stdio stream.  You can also write your own code to do something else.
	* VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
	* requires it in order to write binary files.
	*/
   if(!openFile())
	  return false;

   jpeg_stdio_dest(&cinfo, fileStream_);

   /* Step 3: set parameters for compression */

   /* First we supply a description of the input image_.
	* Four fields of the cinfo struct must be filled in:
	*/
   cinfo.image_width = image_width; /* image_ width and height, in pixels */
   cinfo.image_height = image_height;
   cinfo.input_components = (int)decompress_num_comps; /* # of color components per pixel */
   cinfo.in_color_space = color_space; /* colorspace of input image_ */

   /* Now use the library's routine to set default compression parameters.
	* (You must set at least cinfo.in_color_space before calling this,
	* since the defaults depend on the source color space.)
	*/
   jpeg_set_defaults(&cinfo);

   /* Now you can set any non-default parameters you wish to.
	* Here we just illustrate the use of quality (quantization table) scaling:
	*/
   jpeg_set_quality(&cinfo,
					(int)((compressionLevel_ == GRK_DECOMPRESS_COMPRESSION_LEVEL_DEFAULT)
							  ? 90
							  : compressionLevel_),
					(boolean)TRUE /* limit to baseline-JPEG values */);

   // set resolution
   if(image_->capture_resolution[0] > 0 && image_->capture_resolution[1] > 0)
   {
	  cinfo.density_unit = 2; // dots per cm
	  cinfo.X_density = (uint16_t)(image_->capture_resolution[0] / 100.0 + 0.5);
	  cinfo.Y_density = (uint16_t)(image_->capture_resolution[1] / 100.0 + 0.5);
   }

   /* Step 4: Start compressor */

   /* TRUE ensures that we will write a complete interchange-JPEG file.
	* Pass TRUE unless you are very sure of what you're doing.
	*/
   jpeg_start_compress(&cinfo, (boolean)TRUE);
   if(image_->meta && image_->meta->color.icc_profile_buf)
   {
	  write_icc_profile(&cinfo, image_->meta->color.icc_profile_buf,
						image_->meta->color.icc_profile_len);
   }
   encodeState = IMAGE_FORMAT_ENCODED_HEADER;

   return true;
}
bool JPEGFormat::encodePixels(void)
{
   /* Step 5: while (scan lines remain to be written) */
   /*           jpeg_write_scanlines(...); */

   /* Here we use the library's state variable cinfo.next_scanline as the
	* loop counter, so that we don't have to keep track ourselves.
	* To keep things simple, we pass one scanline per call; you can pass
	* more if you wish, though.
	*/
   auto iter = grk::InterleaverFactory<int32_t>::makeInterleaver(8);
   if(!iter)
	  return false;
   while(cinfo.next_scanline < cinfo.image_height)
   {
	  /* jpeg_write_scanlines expects an array of pointers to scanlines.
	   * Here the array is only one element long, but you could pass
	   * more than one scanline at a time if that's more convenient.
	   */
	  iter->interleave((int32_t**)planes, image_->decompress_num_comps, (uint8_t*)buffer,
					   image_->decompress_width, image_->comps[0].stride, image_->decompress_width, 1,
					   adjust);
	  JSAMPROW row_pointer[1]; /* pointer to JSAMPLE row[s] */
	  row_pointer[0] = buffer;
	  jpeg_write_scanlines(&cinfo, row_pointer, 1);
   }
   delete iter;

   return true;
}
bool JPEGFormat::encodeFinish(void)
{
   /* Step 6: Finish compression */
   jpeg_finish_compress(&cinfo);

   /* Step 7: release JPEG compression object */

   /* This is an important step since it will release a good deal of memory. */
   jpeg_destroy_compress(&cinfo);

   delete[] buffer;
   delete[] buffer32s;

   /* After finish_compress, we can close the output file. */
   return ImageFormat::encodeFinish() && success;
}

grk_image* JPEGFormat::decode(const std::string& filename, grk_cparameters* parameters)
{
   return jpegtoimage(filename.c_str(), parameters);
}
