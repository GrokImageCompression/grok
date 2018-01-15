/*
*    Copyright (C) 2016-2018 Grok Image Compression Inc.
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
#include "opj_apps_config.h"
#include "openjpeg.h"
#include "JPEGFormat.h"
#include "convert.h"
#include <cstring>


#ifndef GROK_HAVE_LIBJPEG
# error GROK_HAVE_LIBJPEG_NOT_DEFINED
#endif /* GROK_HAVE_LIBPNG */



#include "jpeglib.h"
#include <setjmp.h>
#include <cassert>



/*
* ERROR HANDLING:
*
* The JPEG library's standard error handler (jerror.c) is divided into
* several "methods" which you can override individually.  This lets you
* adjust the behavior without duplicating a lot of code, which you might
* have to update with each future release.
*
* Our example here shows how to override the "error_exit" method so that
* control is returned to the library's caller when a fatal error occurs,
* rather than calling exit() as the standard error_exit method does.
*
* We use C's setjmp/longjmp facility to return control.  This means that the
* routine which calls the JPEG library must first execute a setjmp() call to
* establish the return point.  We want the replacement error_exit to do a
* longjmp().  But we need to make the setjmp buffer accessible to the
* error_exit routine.  To do this, we make a private extension of the
* standard JPEG error handler object.  (If we were using C++, we'd say we
* were making a subclass of the regular error handler.)
*
* Here's the extended error handler struct:
*/

struct my_error_mgr {
	struct jpeg_error_mgr pub;    /* "public" fields */

	jmp_buf setjmp_buffer;        /* for return to caller */
};

typedef struct my_error_mgr *my_error_ptr;

/*
* Here's the routine that will replace the standard error_exit method:
*/

METHODDEF(void)
my_error_exit(j_common_ptr cinfo)
{
	/* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
	my_error_ptr myerr = (my_error_ptr)cinfo->err;

	/* Always display the message. */
	/* We could postpone this until after returning, if we chose. */
	(*cinfo->err->output_message) (cinfo);

	/* Return control to the setjmp point */
	longjmp(myerr->setjmp_buffer, 1);
}

struct imageToJpegInfo {
	imageToJpegInfo() : success(true),
		buffer(nullptr),
		buffer32s(nullptr),
		color_space(JCS_UNKNOWN),
		writeToStdout(false)
	{}

	bool success;
	uint8_t* buffer;
	int32_t* buffer32s;
	J_COLOR_SPACE color_space;
	bool writeToStdout;
};


static int imagetojpeg(opj_image_t* image, const char *filename, int compressionParam, bool verbose)
{
	if (!image)
		return 1;
	imageToJpegInfo info;
	info.writeToStdout = ((filename == nullptr) || (filename[0] == 0));
	convert_32s_PXCX cvtPxToCx = nullptr;
	convert_32sXXx_C1R cvt32sToTif = nullptr;
	int32_t const* planes[3];
	int32_t firstAlpha = -1;
	size_t numAlphaChannels = 0;
	uint32_t numcomps = image->numcomps;
	uint32_t sgnd = image->comps[0].sgnd;
	uint32_t adjust = sgnd ? 1 << (image->comps[0].prec - 1) : 0;
	uint32_t width = image->comps[0].w;

	// actual bits per sample
	uint32_t bps = image->comps[0].prec;
	uint32_t i = 0;

	/* This struct contains the JPEG compression parameters and pointers to
	* working space (which is allocated as needed by the JPEG library).
	* It is possible to have several such structures, representing multiple
	* compression/decompression processes, in existence at once.  We refer
	* to any one struct (and its associated working data) as a "JPEG object".
	*/
	struct jpeg_compress_struct cinfo;
	/* This struct represents a JPEG error handler.  It is declared separately
	* because applications often want to supply a specialized error handler
	* (see the second half of this file for an example).  But here we just
	* take the easy way out and use the standard error handler, which will
	* print a message on stderr and call exit() if compression fails.
	* Note that this struct must live as long as the main JPEG parameter
	* struct, to avoid dangling-pointer problems.
	*/
	JDIMENSION image_width = image->x1 - image->x0;       /* input image width */
	JDIMENSION image_height = image->y1 - image->y0;      /* input image height */

	switch (image->color_space) {
	case OPJ_CLRSPC_SRGB: 		/**< sRGB */
		info.color_space = JCS_RGB;
		break;
	case OPJ_CLRSPC_GRAY: 		/**< grayscale */
		info.color_space = JCS_GRAYSCALE;
		break;
	case OPJ_CLRSPC_SYCC:		/**< YUV */
		info.color_space = JCS_YCbCr;
		break;
	case OPJ_CLRSPC_EYCC:        /**< e-YCC */
		info.color_space = JCS_YCCK;
		break;
	case OPJ_CLRSPC_CMYK:        /**< CMYK */
		info.color_space = JCS_CMYK;
		break;
	default:
		if (numcomps == 3)
			info.color_space = JCS_RGB;
		else if (numcomps == 1)
			info.color_space = JCS_GRAYSCALE;
		else {
			info.success = false;
			goto cleanup;
		}
		break;
	}

	struct my_error_mgr jerr;
	/* More stuff */
	FILE *outfile;                /* target file */
	JSAMPROW row_pointer[1];      /* pointer to JSAMPLE row[s] */

								  /* Step 1: allocate and initialize JPEG compression object */

								  /* We have to set up the error handler first, in case the initialization
								  * step fails.  (Unlikely, but it could happen if you are out of memory.)
								  * This routine fills in the contents of struct jerr, and returns jerr's
								  * address which we place into the link field in cinfo.
								  */

	if (image->numcomps > 4) {
		info.success = false;
		goto cleanup;
	}


	planes[0] = image->comps[0].data;
	if (bps == 0) {
		fprintf(stderr, "[ERROR] imagetojpeg: image precision is zero.\n");
		info.success = false;
		goto cleanup;
	}

	//check for null image components
	for (uint32_t i = 0; i < numcomps; ++i) {
		auto comp = image->comps[i];
		if (!comp.data) {
			info.success = false;
			goto cleanup;
		}
	}
	for (i = 1U; i < numcomps; ++i) {
		if (image->comps[0].dx != image->comps[i].dx) {
			break;
		}
		if (image->comps[0].dy != image->comps[i].dy) {
			break;
		}
		if (image->comps[0].prec != image->comps[i].prec) {
			break;
		}
		if (image->comps[0].sgnd != image->comps[i].sgnd) {
			break;
		}
		planes[i] = image->comps[i].data;
	}
	if (i != numcomps) {
		fprintf(stderr, "[ERROR] imagetojpeg: All components shall have the same subsampling, same bit depth.\n");
		fprintf(stderr, "\tAborting\n");
		info.success = false;
		goto cleanup;
	}
	cvtPxToCx = convert_32s_PXCX_LUT[numcomps];
	switch (bps) {
	case 1:
	case 2:
	case 4:
	case 6:
	case 8:
		cvt32sToTif = convert_32sXXu_C1R_LUT[bps];
		break;
	default:
		fprintf(stderr, "[ERROR] imagetojpeg: Unsupported precision %d.\n", bps);
		info.success = false;
		goto cleanup;
		break;
	}

	// Alpha channels
	for (i = 0U; i < numcomps; ++i) {
		if (image->comps[i].alpha) {
			if (firstAlpha == -1)
				firstAlpha = 0;
			numAlphaChannels++;
		}
	}
	// We assume that alpha channels occur as last channels in image.
	if (numAlphaChannels && (firstAlpha + numAlphaChannels >= numcomps)) {
		if (verbose)
			fprintf(stdout, "WARNING: PNG requires that alpha channels occur as last channels in image.\n");
		numAlphaChannels = 0;
	}
	info.buffer = new uint8_t[width * numcomps];
	info.buffer32s = new int32_t[width * numcomps];

	/* We set up the normal JPEG error routines, then override error_exit. */
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	/* Establish the setjmp return context for my_error_exit to use. */
	if (setjmp(jerr.setjmp_buffer)) {
		/* If we get here, the JPEG code has signaled an error.
		* We need to clean up the JPEG object, close the input file, and return.
		*/
		jpeg_destroy_compress(&cinfo);
		info.success = false;
		goto cleanup;
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
	if (info.writeToStdout) {
		if (!grok_set_binary_mode(stdin))
			return 1;
		outfile = stdout;
	}
	else {
		if ((outfile = fopen(filename, "wb")) == nullptr) {
			fprintf(stderr, "[ERROR] can't open %s\n", filename);
			info.success = false;
			goto cleanup;
		}
	}
	jpeg_stdio_dest(&cinfo, outfile);

	/* Step 3: set parameters for compression */

	/* First we supply a description of the input image.
	* Four fields of the cinfo struct must be filled in:
	*/
	cinfo.image_width = image_width;      /* image width and height, in pixels */
	cinfo.image_height = image_height;
	cinfo.input_components = numcomps;           /* # of color components per pixel */
	cinfo.in_color_space = info.color_space;       /* colorspace of input image */
												   /* Now use the library's routine to set default compression parameters.
												   * (You must set at least cinfo.in_color_space before calling this,
												   * since the defaults depend on the source color space.)
												   */
	jpeg_set_defaults(&cinfo);
	/* Now you can set any non-default parameters you wish to.
	* Here we just illustrate the use of quality (quantization table) scaling:
	*/
	jpeg_set_quality(&cinfo,
		(compressionParam == DECOMPRESS_COMPRESSION_LEVEL_DEFAULT) ? 90 : compressionParam,
		(boolean)TRUE /* limit to baseline-JPEG values */);

	/* Step 4: Start compressor */

	/* TRUE ensures that we will write a complete interchange-JPEG file.
	* Pass TRUE unless you are very sure of what you're doing.
	*/
	jpeg_start_compress(&cinfo, (boolean)TRUE);

	/* Step 5: while (scan lines remain to be written) */
	/*           jpeg_write_scanlines(...); */

	/* Here we use the library's state variable cinfo.next_scanline as the
	* loop counter, so that we don't have to keep track ourselves.
	* To keep things simple, we pass one scanline per call; you can pass
	* more if you wish, though.
	*/

	while (cinfo.next_scanline < cinfo.image_height) {
		/* jpeg_write_scanlines expects an array of pointers to scanlines.
		* Here the array is only one element long, but you could pass
		* more than one scanline at a time if that's more convenient.
		*/
		cvtPxToCx(planes, info.buffer32s, (size_t)width, adjust);
		cvt32sToTif(info.buffer32s, (uint8_t *)info.buffer, (size_t)width * numcomps);
		row_pointer[0] = info.buffer;
		planes[0] += width;
		planes[1] += width;
		planes[2] += width;
		(void)jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	/* Step 6: Finish compression */

	jpeg_finish_compress(&cinfo);
	/* After finish_compress, we can close the output file. */
	fclose(outfile);

	/* Step 7: release JPEG compression object */

	/* This is an important step since it will release a good deal of memory. */
	jpeg_destroy_compress(&cinfo);

cleanup:
	delete[] info.buffer;
	delete[] info.buffer32s;
	return info.success ? 0 : 1;
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

struct jpegToImageInfo {
	jpegToImageInfo() : image(nullptr), success(true), buffer32s(nullptr) {}

	opj_image_t *image;
	bool success;
	int32_t* buffer32s;
};

static opj_image_t* jpegtoimage(const char *filename, opj_cparameters_t *parameters)
{
	bool readFromStdin = ((filename == nullptr) || (filename[0] == 0));
	jpegToImageInfo imageInfo;

	int32_t* planes[3];
	JDIMENSION w = 0, h = 0;
	int bps = 0, numcomps = 0;
	convert_XXx32s_C1R cvtJpegTo32s;
	OPJ_COLOR_SPACE color_space = OPJ_CLRSPC_UNKNOWN;
	opj_image_cmptparm_t cmptparm[3]; /* mono or RGB */
	convert_32s_CXPX cvtCxToPx;

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
	FILE *infile;                 /* source file */
	JSAMPARRAY buffer;            /* Output row buffer */
	int row_stride;               /* physical row width in output buffer */

								  /* In this example we want to open the input file before doing anything else,
								  * so that the setjmp() error recovery below can assume the file is open.
								  * VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
								  * requires it in order to read binary files.
								  */

	if (readFromStdin) {
		if (!grok_set_binary_mode(stdin))
			return nullptr;
		infile = stdin;
	}
	else {
		if ((infile = fopen(filename, "rb")) == nullptr) {
			fprintf(stderr, "[ERROR] can't open %s\n", filename);
			return 0;
		}
	}

	/* Step 1: allocate and initialize JPEG decompression object */

	/* We set up the normal JPEG error routines, then override error_exit. */
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	/* Establish the setjmp return context for my_error_exit to use. */
	if (setjmp(jerr.setjmp_buffer)) {
		/* If we get here, the JPEG code has signaled an error.
		* We need to clean up the JPEG object, close the input file, and return.
		*/
		jpeg_destroy_decompress(&cinfo);
		imageInfo.success = false;
		goto cleanup;
	}
	/* Now we can initialize the JPEG decompression object. */
	jpeg_create_decompress(&cinfo);

	/* Step 2: specify data source (eg, a file) */
	jpeg_stdio_src(&cinfo, infile);

	/* Step 3: read file parameters with jpeg_read_header() */

	(void)jpeg_read_header(&cinfo, TRUE);
	/* We can ignore the return value from jpeg_read_header since
	*   (a) suspension is not possible with the stdio data source, and
	*   (b) we passed TRUE to reject a tables-only JPEG file as an error.
	* See libjpeg.txt for more info.
	*/

	/* Step 4: set parameters for decompression */

	/* In this example, we don't need to change any of the defaults set by
	* jpeg_read_header(), so we do nothing here.
	*/

	/* Step 5: Start decompressor */

	(void)jpeg_start_decompress(&cinfo);
	/* We can ignore the return value since suspension is not possible
	* with the stdio data source.
	*/

	bps = cinfo.data_precision;
	if (bps != 8) {
		fprintf(stderr, "[ERROR] jpegtoimage: Unsupported image precision %d\n", bps);
		imageInfo.success = false;
		goto cleanup;
	}

	numcomps = cinfo.output_components;
	w = cinfo.image_width;
	h = cinfo.image_height;
	cvtJpegTo32s = convert_XXu32s_C1R_LUT[bps];
	memset(&cmptparm[0], 0, 3 * sizeof(opj_image_cmptparm_t));
	cvtCxToPx = convert_32s_CXPX_LUT[numcomps];

	if (cinfo.output_components == 3)
		color_space = OPJ_CLRSPC_SRGB;
	else
		color_space = OPJ_CLRSPC_GRAY;

	for (int j = 0; j < cinfo.output_components; j++) {
		cmptparm[j].prec = bps;
		cmptparm[j].dx = 1;
		cmptparm[j].dy = 1;
		cmptparm[j].w = w;
		cmptparm[j].h = h;
	}

	imageInfo.image = opj_image_create(numcomps, &cmptparm[0], color_space);
	if (!imageInfo.image) {
		imageInfo.success = false;
		goto cleanup;
	}
	/* set image offset and reference grid */
	imageInfo.image->x0 = parameters->image_offset_x0;
	imageInfo.image->x1 = !imageInfo.image->x0 ? (w - 1) * 1 + 1 :
		imageInfo.image->x0 + (w - 1) * 1 + 1;
	if (imageInfo.image->x1 <= imageInfo.image->x0) {
		fprintf(stderr, "[ERROR] jpegtoimage: Bad value for image->x1(%d) vs. "
			"image->x0(%d)\n\tAborting.\n", imageInfo.image->x1, imageInfo.image->x0);
		imageInfo.success = false;
		goto cleanup;
	}

	imageInfo.image->y0 = parameters->image_offset_y0;
	imageInfo.image->y1 = !imageInfo.image->y0 ? (h - 1) * 1 + 1 :
		imageInfo.image->y0 + (h - 1) * 1 + 1;

	if (imageInfo.image->y1 <= imageInfo.image->y0) {
		fprintf(stderr, "[ERROR] jpegtoimage: Bad value for image->y1(%d) vs. "
			"image->y0(%d)\n\tAborting.\n", imageInfo.image->y1, imageInfo.image->y0);
		imageInfo.success = false;
		goto cleanup;
	}


	for (int j = 0; j < numcomps; j++) {
		planes[j] = imageInfo.image->comps[j].data;
	}

	imageInfo.buffer32s = new int32_t[w * numcomps];

	/* We may need to do some setup of our own at this point before reading
	* the data.  After jpeg_start_decompress() we have the correct scaled
	* output image dimensions available, as well as the output colormap
	* if we asked for color quantization.
	* In this example, we need to make an output work buffer of the right size.
	*/
	/* JSAMPLEs per row in output buffer */
	row_stride = cinfo.output_width * cinfo.output_components;
	/* Make a one-row-high sample array that will go away when done with image */
	buffer = (*cinfo.mem->alloc_sarray)
		((j_common_ptr)&cinfo, JPOOL_IMAGE, row_stride, 1);
	if (!buffer) {
		imageInfo.success = false;
		goto cleanup;
	}

	/* Step 6: while (scan lines remain to be read) */
	/*           jpeg_read_scanlines(...); */

	/* Here we use the library's state variable cinfo.output_scanline as the
	* loop counter, so that we don't have to keep track ourselves.
	*/
	while (cinfo.output_scanline < cinfo.output_height) {
		/* jpeg_read_scanlines expects an array of pointers to scanlines.
		* Here the array is only one element long, but you could ask for
		* more than one scanline at a time if that's more convenient.
		*/
		(void)jpeg_read_scanlines(&cinfo, buffer, 1);

		// convert 8 bit buffer to 32 bit buffer
		cvtJpegTo32s(buffer[0], imageInfo.buffer32s, (size_t)w * numcomps, false);

		// convert to planar
		cvtCxToPx(imageInfo.buffer32s, planes, (size_t)w);

		planes[0] += w;
		planes[1] += w;
		planes[2] += w;
	}

	/* Step 7: Finish decompression */

	(void)jpeg_finish_decompress(&cinfo);
	/* We can ignore the return value since suspension is not possible
	* with the stdio data source.
	*/

	/* Step 8: Release JPEG decompression object */

	/* This is an important step since it will release a good deal of memory. */
	jpeg_destroy_decompress(&cinfo);

cleanup:

	/* After finish_decompress, we can close the input file.
	* Here we postpone it until after no more JPEG errors are possible,
	* so as to simplify the setjmp error logic above.  (Actually, I don't
	* think that jpeg_destroy can do an error exit, but why assume anything...)
	*/
	if (infile)
		fclose(infile);

	delete[] imageInfo.buffer32s;

	/* At this point you may want to check to see whether any corrupt-data
	* warnings occurred (test whether jerr.pub.num_warnings is nonzero).
	*/
	assert(jerr.pub.num_warnings == 0);

	if (imageInfo.success) {
		return imageInfo.image;
	}
	if (imageInfo.image)
		opj_image_destroy(imageInfo.image);
	return nullptr;
}/* jpegtoimage() */

bool JPEGFormat::encode(opj_image_t* image, std::string filename, int compressionParam, bool verbose) {
	return imagetojpeg(image, filename.c_str(), compressionParam, verbose) ? false : true;
}
opj_image_t*  JPEGFormat::decode(std::string filename, opj_cparameters_t *parameters) {
	return jpegtoimage(filename.c_str(), parameters);
}









