% grk_compress(1) Version 10.0 | convert to JPEG 2000 format

NAME
====

grk_compress - compresses images to JPEG 2000 format

SYNOPSIS
========

| **grk_compress** \[**-i** infile.bmp] \[**-o** outfile.j2k]


DESCRIPTION
===========

This program converts non-`JPEG 2000` images to the `JPEG 2000` format.

* Supported input formats:  `JPEG`, `BMP`, `PNM`, `PGX`, `PNG`, `RAW`, `RAWL` and `TIFF`
* Supported input image extensions:  `jpg`, `.jpeg`, `.bmp`, `.pgm`, `.pgx`, `.pnm`, `.ppm`, `.pam`, `.png`, `.raw`, `.rawl`, `.tif` and `.tiff`
* Supported output formats: `JP2` and `J2K`/`J2C`
* Supported output image extensions: `.jp2` and `.j2k`/`.j2c`
* For `PNG` the library must have `libpng` available.
* For `TIF/TIFF` the library must have `libtiff` available.
* For `JPG/JPEG` the library must have a `libjpeg` variant available.

limitations

* `grk_compress` supports up to and including 16 bit sample precision for input images. This is a subset of the ISO standard, which allows up to 38 bit precision.

stdin

Input from `stdin` is supported for the following formats: `PNG`, `JPG`, `RAW` and `RAWL`.  To read from `stdin`,
make sure that the `-i` parameter is **not** present, and that the `-in_fmt` parameter is set to one of the supported formats listed above.

Embedded ICC Profile (JP2 Only)

If there is an embedded ICC profile in the input file, then the profile will be stored in the compressed file.

IPTC (JP2 Only)

If an input `TIF/TIFF` file contains `IPTC` metadata, this metadata will be stored in the compressed file.

XMP (JP2 Only)

If an input `TIF/TIFF` or `PNG` file contains `XMP` metadata, this metadata will be stored in the compressed file.

Exif (JP2 only)

To transfer Exif and all other meta-data tags, use the command line argument `-V` described below. To transfer the tags, Grok uses the wonderful [ExifTool](https://exiftool.org/) Perl module. ExifTool must be installed for this command line argument to work properly.
Note: transferring Exif tags may add a few hundred ms to the decompress time, depending on the system.

When only the input and output files are specified, the following default option values are used:

    * lossless compression
    * reversible DWT 5-3
    * single quality layer
    * single tile
    * precinct size : 2^15 x 2^15 (i.e. only 1 precinct)
    * code block dimensions : 64 x 64
    * number of resolutions (i.e. DWT decomposition levels + 1) : 6
    * no SOP markers
    * no EPH markers
    * default encode mode
    * progression order : `LRCP`
    * no ROI up-shifted
    * no image origin offset
    * no tile origin offset

**Important note on command line argument notation below**: the outer square braces appear for clarity only, and **should not** be included in the actual command line argument. Square braces appearing inside the outer braces **should** be included.


Options
-------

`-h, -help`

Print a help message and exit.

`-version`

Print library version and exit.

`-v, -verbose`

Output information and warnings about encoding to console (errors are always output). Default is false i.e. console is silent by default.

`-i, -in_file [file]`

Input file. Either this argument or the `-batch_src` argument described below is required.  See above for supported input formats.

* `PNG` requires `libpng` while `TIF/TIFF` requires `libtiff`
* `JPG` requires `libjpeg` (or `libjpeg-turbo`), and only 8 bit precision is supported
*  For `BMP` format, the coder accepts 24 bits color images and 8 bits (RLE or no-RLE) images
*  `TIF` files can have up to 16 bits per component. 
*  For `RAW` or `RAWL` (`RAW` `L`ittle endian) images, the `-F` parameter must be used (see below). In the case of raw images with a component depth value between 9 and 16 bits, each component's data must be stored on two bytes (`RAW` format assumes big endian-ness, `RAWL` assumes little endian-ness) When using this option, the output file must be specified using `-o`.

`-o, -out_file [file]`

Output file. Required when using `-i` option. Valid output image extensions are `J2K`, `JP2` and `J2C`.

`-y, -batch_src [input directory]`

Path to the folder where the images to be compressed are stored. Either this argument or the `-i` argument described above is required. When image files are in the same directory as the executable, this can be indicated by a dot `.` argument. When using this option, output format must be specified using `-O`. 

`-a, -out_dir [output directory]`

Output directory where compressed files are stored. Only relevant when the `-batch_src` flag is set. Default: same directory as specified by `-y`.

`-O, --out-fmt [J2K|J2C|JP2]`

Output format used to compress the images read from the directory specified with `-batch_src`. Required when `-batch_src` option is used. Supported formats are `J2K`, `J2C`, and `JP2`.

`-K, -in_fmt [pbm|pgm|ppm|pnm|pam|pgx|png|bmp|tif|raw|rawl|jpg]`

Input format. Will override file tag.

`-F, -raw [width,height,number of components,bit depth,[s,u]@<dx1>x<dy1>:...:<dxn>x<dyn>]`

Raw input image characteristics. Required only if RAW or RAWL (RAW little endian) input file is provided. Note: If sub-sampling is omitted, `1x1` is assumed for all components. 

Example of a raw `512x512` unsigned image with `4:2:0` sub-sampling

       -F 512,512,3,8,u@1x1:2x2:2x2

`-A, -rate_control_algorithm [0|1]`

Select algorithm used for rate control.
* 0: Bisection search for optimal threshold using all code passes in code blocks. Slightly higher PSNR than algorithm 1.
* 1: Bisection search for optimal threshold using only feasible truncation points, on convex hull (default). Faster than algorithm 0.

`-r, -compression_ratios [<compression ratio>,<compression ratio>,...]`

Note: not supported for Part 15 (HTJ2K) compression

Compression ratio values (double precision, greater than or equal to one). Each value is a factor of compression, thus 20 means 20 times compressed. Each value represents a quality layer. The order used to define the different levels of compression is important and must be from left to right in descending order. A final lossless quality layer (including all remaining code passes) will be signified by the value 1. Default: 1 single lossless quality layer.

`-q, -quality [quality in dB,quality in dB,...]`

Note: not supported for Part 15 (HTJ2K) compression

Quality values (double precision, greater than or equal to zero). Each value is a PSNR measure, given in dB, representing a quality layer. The order used to define the different PSNR values is important and must be from left to right in ascending order. A value of 0 signifies a final lossless quality layer (including all remaining code passes) Default: 1 single lossless quality layer.

`-n, -num_resolutions [number of resolutions]`

Number of resolutions. It corresponds to the `number of DWT decompositions +1`. Default: 6.

`-b, -code_block_dims [code block width,code block height]`

Code-block size. The dimension must respect the constraint defined in the JPEG-2000 standard (no dimension smaller than 4 or greater than 1024, no code-block with more than 4096 coefficients). The maximum value authorized is 64x64. Default: 64x64.

`-c, -precinct_dims [  [prec width,prec height],[prec width,prec height],... ]`

Precinct dimensions. Dimensions specified must be powers of 2. Multiple records may be specified, in which case the first record refers to the highest resolution level and subsequent records refer to lower resolution levels. The last specified record's dimensions are progressively right-shifted (halved in size) for each remaining lower resolution level. Default: `2^15x2^15` at each resolution i.e. precincts are not used. Note: the inner square brackets must actually be present.

Example for image with 6 resolutions :

`-c [256,256],[256,256],[256,256],[256,256],[256,256],[256,256]`

`-t, -tile_dims [tile width,tile height]`

Tile size. Default: the dimension of the whole image, thus only one tile.

`-L, -PLT`

Use PLT markers. Default: off

`-X, -TLM`

Use TLM markers. Default: off

`-I, -irreversible`

Irreversible compression (ICT + DWT 9-7). This option enables the Irreversible Color Transformation (ICT) in place of the Reversible Color Transformation (RCT) and the irreversible DWT 9-7 in place of the 5-3 filter. Default: off.

`-p, -progression_order` [progression order]

Progression order. The five progression orders are : `LRCP`, `RLCP`, `RPCL`, `PCRL` and `CPRL`. Default: `LRCP`.

`-Z, -rsiz [rsiz]`

Profile, main level, sub level and version. Note: this flag will be ignored if cinema profile flags are used.

`-N, -guard_bits [number of guard bits]`

Number of guard bits to use in block coder. Must be between 0 and 7.

`-w, -cinema2K [24|48]`

2K digital cinema profile. This option generates a codes stream compliant with the DCI specifications for 2K resolution content. The value given is the frame rate, which can be either 24 or 48 fps. The main specifications of the JPEG 2000 Profile-3 (2K Digital Cinema Profile) are:

* Image size = 2048 x 1080 (at least one of the dimensions should match 2048 x 1080)
* Single tile 
* Wavelet transform levels = Maximum of 5
* Wavelet filter = 9-7 filter
* Codeblock size = 32 x 32
* Precinct size = 128 x 128 (Lowest frequency sub-band), 256 x 256 (other sub-bands)
* Maximum Bit rate for entire frame = 1302083 bytes for 24 fps, 651041 bytes for 48fps
* Maximum Bit rate for each color component= 1041666 bytes for 24 fps, 520833 bytes for 48fps
* Tile parts = 3; Each tile part contains data necessary to decompress one 2K color component
* 12 bits per component.

`-x, -cinema4k`

4K digital cinema profile. This option generates a code stream compliant with the DCI specifications for 4K resolution content. The value given is the frame rate, which can be either 24 or 48 fps. The main specifications of the JPEG 2000 Profile-4 (4K Digital Cinema Profile) are:

* Image size = 4096 x 2160 (at least one of the dimensions must match 4096 x 2160)
* Single tile * Wavelet transform levels = Maximum of 6 and minimum of 1
* Wavelet filter = 9-7 filter
* Codeblock size = 32 x 32
* Precinct size = 128 x 128 (Lowest frequency sub-band), 256 x 256 (other sub-bands)
* Maximum Bit rate for entire frame = 1302083 bytes for 24 fps
* Maximum Bit rate for each color component= 1041666 bytes for 24 fps
* Tile parts = 6; Each of first 3 tile parts contains data necessary to decompress one 2K color component, and each of last 3 tile parts contains data necessary to decompress one 4K color component.
* 12 bits per component

`-U, -broadcast [PROFILE [,mainlevel=X][,framerate=FPS] ]`

Broadcast compliant code stream

* `PROFILE` must be one of { `SINGLE`, `MULTI`, `MULTI_R`}
* X must be between 0 and 11
* frame rate may be specified to enhance checks and set maximum bit rate when Y > 0. 
If specified, it must be positive.

`-z, --imf [PROFILE [,mainlevel=X][,sublevel=Y][,framerate=FPS]] ]`

Interoperable Master Format (IMF) compliant codestream.

* `PROFILE` must be one of { `2K`, `4K`, `8K`, `2K_R`, `4K_R`, `8K_R`}
* X must be between 0 and 11
* Y must be between 0 and 9
* frame rate may be specified to enhance checks and set maximum bit rate when Y > 0. If specified, it must be positive.

`-P, -POC [T<tile number 0>=resolution number start>,component number start,layer number end,resolution number end,component number end,progression order/T<tile number 1>= ...]`

Progression order change. This specifies a list of progression orders and their bounds if a progression order change is desired.
Note: there must be at least two progression orders specified.

Example:
      ` -POC T0=0,0,1,3,2,CPRL/T0=0,0,1,6,3,CPRL`

`-S, -SOP`

SOP marker is added before each packet. Default: no SOP.

`-E, -EPH`

EPH marker is added after each packet header. Default: no EPH.

`-M, -mode [value]`

Non-default encode modes. There are 7 modes available.
The first six are:

* BYPASS(LAZY) [1]
* RESET [2]
* RESTART(TERMALL) [4]
* VSC [8] 
* ERTERM(SEGTERM) [16] 
* SEGMARK(SEGSYM) [32] 
* HT [64] 

and they can be combined together. If more than one mode is used, the values between the brackets `[]` must be added together. Default: no mode.

    Example : RESTART(4) + RESET(2) + SEGMARK(32) => -M 38

Mode HT [64], for High Throughput encoding, *cannot* be combined with any of the other flags.

`-u, -tile_parts [R|L|C]`

Divide packets of every tile into tile-parts. The division is made by grouping Resolutions (R), Layers (L) or Components (C). The type of division is specified by setting the single letter `R`, `L`, or `C` as the value for this flag.

`-R, -ROI [c=component index,U=upshifting value]`

Quantization indices upshifted for a component. 

Warning: This option does not implement the usual ROI (Region of Interest). It should be understood as a "Component of Interest". It offers the possibility to upshift the value of a component during quantization step. The value after `c=` is the component number `[0, 1, 2, ...]` and the value after `U=` is the value of upshifting. U must be in the range `[0, 37]`.

`-d, -image_offset [x offset,y offset]`

Offset of the image origin. The division in tile could be modified as the anchor point for tiling will be different than the image origin. Keep in mind that the offset of the image can not be higher than the tile dimension if the tile option is used. The two values are respectively for `X` and `Y` axis offset. Default: no offset.

`-T, -tile_offset [x offset,y offset]`

Offset of the tile origin. The two values are respectively for X and Y axis offset. The tile anchor point can not be inside the image area. Default: no offset.

`-Y, --mct [0|1|2]`

Specify explicitly if a Multiple Component Transform has to be used. 

* 0: no MCT 
* 1: RGB->YCC conversion 
* 2: custom MCT. 

For custom MCT, `-m` option has to be used (see below). By default, `RGB`->`YCC` conversion is used if there are three components or more, otherwise no conversion.

`-m, -custom_mct [file]`

Use custom array-based MCT of 32 bit signed values, comma separated, line-by-line no specific separators between lines, no space allowed between values. If this option is used, it automatically sets `[-Y|-mct]` option equal to 2.

`-V, -transfer_exif_tags`

Transfer all Exif tags to output file.

Notes:

1. [ExifTool](https://exiftool.org/) must be installed for this command line argument
to function correctly.
2. Only supported on Linux. On other platforms, `exiftool` can be used directly after compression to transfer
tags:

`exiftool -TagsFromFile src.tif "-all:all>all:all" dest.jp2`

`-Q, -capture_res [capture resolution X,capture resolution Y]`

Capture resolution in pixels/metre, in double precision.

* If the input image has a resolution stored in its header, then this resolution will be set as the capture resolution, by default.
* If the `-Q` command line parameter is set, then it will override the resolution stored in the input image, if present
* The special values `[0,0]` for `-Q` will force the encoder to **not** store capture resolution, even if present in input image. 

`-D, -display_res [display resolution X,display resolution Y]`

Display resolution in pixels/metre, in double precision. 
The special values `[0,0]` for `-D` will force the encoder to set the display resolution equal to the capture resolution. 

`-C, -comment [comment]`

Add `<comment>` in comment marker segment(s). Multiple comments (up to a total of 256) can be specified, separated by the `|` character. For example:   `-C "This is my first comment|This is my second` will store `This is my first comment` in the first comment marker segment, and `This is my second` in a second comment marker.

`-W, -logfile [output file name]`

Log to file. File name will be set to `output file name`

`-H, -num_threads [number of threads]`

Number of threads used for T1 compression. Default is total number of logical cores.

`-J, -duration [duration]`

Duration in seconds for a batch compress job. `grk_compress` will exit when duration has been reached.

`-e, -repetitions [number of repetitions]`

Number of repetitions, for either a single image, or a folder of images. Default value is `1`. Unlimited repetitions are specified by a value of `0`.

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

**grk_decompress(1)**
