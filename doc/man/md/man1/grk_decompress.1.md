% grk_decompress(1) Version 10.0 | convert from JPEG 2000 format

NAME
====

grk_decompress - decompresses an image in the JPEG 2000 format to a different
image format

SYNOPSIS
========

| **grk_dcompress** \[**-i** infile.j2k] \[**-o** outfile.bmp]

DESCRIPTION
===========

This program decompresses a JPEG 2000 image and stores it in another image format.

Supported input formats: `JP2` and `J2K\J2C`
Supported input image extensions are `.jp2` and `.j2k\.j2c`

Supported output formats are `JPEG`, `BMP`, `PNM`, `PGX`, `PNG`, `RAW` and `TIFF`
Valid output image extensions are `jpg`, `.jpeg`, `.bmp`, `.pgm`, `.pgx`, `.pnm`, `.ppm`, `.pam`, `.png`, `.raw`, `.rawl`, `.tif` and `.tiff`

* For `PNG` the library must have `libpng` available.
* For `TIF\\TIFF` the library must have `libtiff` available.
* For `JPG\\JPEG` the library must have a `libjpeg` variant available.

Limitations

* Grok supports up to and including 16 bit sample precision for decompression. This is a subset of the ISO standard, which allows up to 38 bit precision.

stdout

The decompresser can write output to `stdout` for the following formats: `BMP`,`PNG`, `JPG`, `PNM`, `RAW` and `RAWL`.  To enable writing to `stdout`, please ensure that the `-o` parameter is **not** present in the command line, and that the `--out-fmt` parameter is set to one of the supported formats listed above. Note: the verbose flag `-v` will be ignored in this mode, as verbose output would corrupt the output file.

Embedded ICC Profile

If there is an embedded ICC profile in the input file, then the profile will be stored in the output file for `TIF\TIFF`, `JPG`, `BMP` and `PNG` formats. For other formats, the profile will be applied to the decompressed image before it is stored.

IPTC (JP2 only)

If a compressed input contains `IPTC` metadata, this metadata will be stored to the output file if that output file is in `TIF\TIFF` format.

XMP (JP2 only)

If a compressed input contains `XMP` metadata, this metadata will be stored to the output file if that output file is in `TIF\\TIFF` or `PNG` format.

Exif (JP2 only)

To transfer Exif and all other meta-data tags, use the command line argument `-V` described below. To transfer the tags, Grok uses the [ExifTool](https://exiftool.org/) Perl module. ExifTool must be installed for this command line argument to work properly. Note: transferring Exif tags may add a few hundred ms to the decompress time, depending on the system.

**Important note on command line argument notation below**: the outer square braces appear for clarity only,and **should not** be included in the actual command line argument. Square braces appearing inside the outer braces **should** be included.


Options
-------


`-h,  -help`

Print a help message and exit.

`-version`

Print library version and exit.

`-v, -verbose`

Output information and warnings about decoding to console (errors are always output). Console is silent by default.

`-i, -in_file [file]`

Input file. Either this argument or the `-batch_src` argument described below is required. Valid input image extensions are J2K, JP2 and JPC. When using this option output file must be specified using -o.

`-o, -out_file [file]`

Output file. Required when using `-i` option. See above for supported file types. If a `PGX` filename is given, there will be as many output files as there are components: an index starting from 0 will then be appended to the output filename, just before the `pgx` extension. If a `PGM` filename is given and there is more than one component, then only the first component will be written to the file.

`-y, -batch_src [directory path]`

Path to the folder where the compressed images are stored. Either this argument or the `-i` argument described above is required. When image files are in the same directory as the executable, this can be indicated by a dot `.` argument. When using this option, the output format must be specified using `--out-fmt`. Output images are saved in the same folder.

`-a, -out_dir [output directory]`

Output directory where compressed files are stored. Only relevant when the `-batch_src` flag is set. Default: same directory as specified by `-batch_src`.

`-O, --out-fmt [format]`

Output format used to decompress the code streams. Required when `-batch_src` option is used. See above for supported formats.

`-r, -reduce [reduce factor]`

Reduce factor. Set the number of highest resolution levels to be discarded. The image resolution is effectively divided by 2 to the power of the number of discarded levels. The reduce factor is limited by the smallest total number of decomposition levels among tiles.

`-l, -layer [layer number]`

Layer number. Set the maximum number of quality layers to decode. If there are fewer quality layers than the specified number, all quality layers will be decoded.

`-d, -region [x0,y0,x1,y1]`

Decompress a region of the image. If `(X,Y)` is a location in the image, then it will only be decoded
if `x0 <= X < x1` and `y0 <= Y < y1`. By default, the entire image is decoded.

There are two ways of specifying the decompress region:

1. pixel coordinates relative to image origin - region is specified in 32 bit integers.

Example: if image coordinates on canvas are `(50,50,1050,1050)` and region is specified as `-d 100,100,200,200`,
then a region with canvas coordinates `(150,150,250,250)` is decompressed

2. pixel coordinates relative to image origin and scaled as floating point to unit square `[0,0,1,1]`

The above example would be specified as `-d 0.1,0.1,0.2,0.2`

Note: there is one ambiguous case, namely `-d 0,0,1,1`, which could be interpreted as either scaled or un-scaled.
We treat this case as a **scaled** pixel region.

`-m, -random_access [random access flags]`

Toggle support for random access code stream markers if present : PLT,TLM or PLM;

The random access flags value passed in is an or'd combination of the following flags

```
1   use PLT marker if present
2   use TLM marker if present
4   use PLM marker if present
```
example: `-m 0` would disable all three markers.


`-c, -compression [compression value]`

Compress output image data. Currently, this flag is only applicable when output format is set
to `TIF`. Possible values are {`NONE`, `LZW`,`JPEG`, `PACKBITS`. `ZIP`,`LZMA`,`ZSTD`,`WEBP`}. 
Default value is `NONE`.

`-L, -compression_level [compression level]`

"Quality" of compression. Currently only implemented for `PNG` format. 
For `PNG`, compression level ranges from 0 (no compression) up to 9.
Grok default value is 3.

Note: PNG is always lossless, so using a different level will not affect the image quality. It only changes
the speed vs file size tradeoff.

`-t, -tile_index [tile index]`

Only decode tile with specified index. Index follows the JPEG2000 convention from top-left to bottom-right. By default all tiles are decoded.

`-p, -precision [component 0 precision[C|S],component 1 precision[C|S],...]`

Force precision (bit depth) of components. There must be at least one value present, but there is no limit on the number of values. 
The last values are ignored if too many values. If there are fewer values than components, the last value is used for the remaining components. If `C` is specified (default), values are clipped. If `S` is specified, values are scaled. Specifying a `0` value indicates use of the original bit depth.

Example:

     -p 8C,8C,8c

Clip all components of a 16 bit RGB image to 8 bits.

`-f, -force_rgb`

Force output image color space to `RGB`. For `TIF/TIFF` or `PNG` output formats, the ICC profile will be applied in this case - default behaviour is to stored the profile in the output file, if supported.

`-u, -upsample`

Sub-sampled components will be upsampled to image size.

`-s, -split_pnm`

Split output components into different files when writing to `PNM`.

`-X, -xml [output file name]`

Store XML metadata to file, if it exists in compressed file. File name will be set to `output file name + ".xml"`

`-V, -transfer_exif_tags`

Transfer all Exif tags to output file. Note: [ExifTool](https://exiftool.org/) must be installed for this command line
argument to work correctly.

`-W, -logfile [output file name]`

Log to file. File name will be set to `output file name`

`-H, -num_threads [number of threads]`

Number of threads used for T1 compression. Default is total number of logical cores.

 `-e, -repetitions [number of repetitions]`

Number of repetitions, for either a single image, or a folder of images. Default is 1. 0 signifies unlimited repetitions.


FILES
=====


ENVIRONMENT
===========

BUGS
====

See GitHub Issues: https://github.com/GrokImageCompression/grok/issues

AUTHOR
======

Grok Image Compression Inc.

SEE ALSO
========

**grk_compress(1)**
