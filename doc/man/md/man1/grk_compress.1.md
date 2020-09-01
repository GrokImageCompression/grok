% grk_compress(1) Version 7.6 | convert to JPEG 2000 format

NAME
====

**grk_compress** — compresses images to JPEG 2000 format

SYNOPSIS
========

| **grk_compress** \[**-i** infile.bmp] \[**-o** outfile.j2k]


DESCRIPTION
===========


Options
-------


`-h, -help` 

Print a help message and exit.

`-version` 

Print library version and exit.

`-v, -verbose` 

Output information and warnings about encoding to console (errors are always output). Default is false i.e. console is silent by default.

`-i, -InputFile [file]`

Input file. Either this argument or the `-ImgDir` argument described below is required.  See above for supported input formats. 

* `PNG` requires `libpng` while `TIF/TIFF` requires `libtiff`
* `JPG` requires `libjpeg` (or `libjpeg-turbo`), and only 8 bit precision is supported
*  For `BMP` format, the coder accepts 24 bits color images and 8 bits (RLE or no-RLE) images
*  `TIF` files can have up to 16 bits per component. 
*  For `RAW` or `RAWL` (`RAW` `L`ittle endian) images, the `-F` parameter must be used (see below). In the case of raw images with a component depth value between 9 and 16 bits, each component's data must be stored on two bytes (`RAW` format assumes big endian-ness, `RAWL` assumes little endian-ness) When using this option, the output file must be specified using `-o`.

`-o, -OutputFile [file]`

Output file. Required when using `-i` option. Valid output image extensions are `J2K`, `JP2` and `J2C`.

`-y, -ImgDir [directory path]`

Path to the folder where the images to be compressed are stored. Either this argument or the `-i` argument described above is required. When image files are in the same directory as the executable, this can be indicated by a dot `.` argument. When using this option, output format must be specified using `-O`. 

`-a, -OutDir [output directory]`

Output directory where compressed files are stored. Only relevant when the `-ImgDir` flag is set. Default: same directory as specified by `-y`.

`-O, -OutFor [J2K|J2C|JP2]`

Output format used to compress the images read from the directory specified with `-ImgDir`. Required when `-ImgDir` option is used. Supported formats are `J2K`, `J2C`, and `JP2`.

`-K, -InFor [pbm|pgm|ppm|pnm|pam|pgx|png|bmp|tif|raw|rawl|jpg]`

Input format. Will override file tag.

`-F, -Raw [width,height,number of components,bit depth,[s,u]@<dx1>x<dy1>:...:<dxn>x<dyn>]`

Raw input image characteristics. Required only if RAW or RAWL (RAW little endian) input file is provided. Note: If sub-sampling is omitted, `1x1` is assumed for all components. 

Example of a raw `512x512` unsigned image with `4:2:0` sub-sampling

       -F 512,512,3,8,u@1x1:2x2:2x2

`-A, -RateControlAlgorithm [0|1]`

Select algorithm used for rate control.
* 0: Bisection search for optimal threshold using all code passes in code blocks.
 (default; slightly higher PSNR than algorithm 1)
* 1: Bisection search for optimal threshold using only feasible truncation points, on convex hull. Faster than algorithm 0.

`-r, -CompressionRatios [<compression ratio>,<compression ratio>,...]`

Compression ratio values (double precision, greater than or equal to one). Each value is a factor of compression, thus 20 means 20 times compressed. Each value represents a quality layer. The order used to define the different levels of compression is important and must be from left to right in descending order. A final lossless quality layer (including all remaining code passes) will be signified by the value 1. Default: 1 single lossless quality layer.

`-q, -Quality [quality in dB,quality in dB,...]`

Quality values (double precision, greater than or equal to zero). Each value is a PSNR measure, given in dB, representing a quality layer. The order used to define the different PSNR values is important and must be from left to right in ascending order. A value of 0 signifies a final lossless quality layer (including all remaining code passes) Default: 1 single lossless quality layer.

`-n, -Resolutions [number of resolutions]`

Number of resolutions. It corresponds to the `number of DWT decompositions +1`. Default: 6.

`-b, -CodeBlockDim [code block width,code block height]`

Code-block size. The dimension must respect the constraint defined in the JPEG-2000 standard (no dimension smaller than 4 or greater than 1024, no code-block with more than 4096 coefficients). The maximum value authorized is 64x64. Default: 64x64.

`-c, -PrecinctDims [prec width,prec height,prec width,prec height,...]`

Precinct dimensions. Dimensions specified must be powers of 2. Multiple records may be specified, in which case the first record refers to the highest resolution level and subsequent records refer to lower resolution levels. The last specified record's dimensions are progressively right-shifted (halved in size) for each remaining lower resolution level. Default: `2^15x2^15` at each resolution i.e. precincts are not used.

`-t, -TileDim [tile width,tile height]`

Tile size. Default: the dimension of the whole image, thus only one tile.

`-L, -PLT`

Use PLT markers. Default: off

`-I, -Irreversible`

Irreversible compression (ICT + DWT 9-7). This option enables the Irreversible Color Transformation (ICT) in place of the Reversible Color Transformation (RCT) and the irreversible DWT 9-7 in place of the 5-3 filter. Default: off.

`-p, -ProgressionOrder` [progression order]

Progression order. The five progression orders are : `LRCP`, `RLCP`, `RPCL`, `PCRL` and `CPRL`. Default: `LRCP`.

`-Z, -RSIZ [rsiz]`

Profile, main level, sub level and version. Note: this flag will be ignored if cinema profile flags are used.

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

`-x, -cinema4K`

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

`-U, -BROADCAST [PROFILE [,mainlevel=X][,framerate=FPS] ]`

Broadcast compliant code stream

* `PROFILE` must be one of { `SINGLE`, `MULTI`, `MULTI_R`}
* X must be between 0 and 11
* frame rate may be specified to enhance checks and set maximum bit rate when Y > 0. 
If specified, it must be positive.

`-z, -IMF [PROFILE [,mainlevel=X][,sublevel=Y][,framerate=FPS]] ]`

Interoperable Master Format (IMF) compliant codestream.

* `PROFILE` must be one of { `2K`, `4K`, `8K`, `2K_R`, `4K_R`, `8K_R`}
* X must be between 0 and 11
* Y must be between 0 and 9
* frame rate may be specified to enhance checks and set maximum bit rate when Y > 0. If specified, it must be positive.

`-P, -POC [T<tile number 1>=resolution number start>,component number start,layer number end,resolution number end,component number end,progression order/T<tile number 2>= ...]` 

Progression order change. This defines the bounds of resolution, color component, layer and progression order if a progression order change is desired.

Example:
      -POC T1=0,0,1,5,3,CPRL/T1=5,0,1,6,3,CPRL

`-S, -SOP`

SOP marker is added before each packet. Default: no SOP.

`-E, -EPH`

EPH marker is added after each packet header. Default: no EPH.

`-M, -Mode [value]`

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

`-u, -TP] [R|L|C]`

Divide packets of every tile into tile-parts. The division is made by grouping Resolutions (R), Layers (L) or Components (C). The type of division is specified by setting the single letter `R`, `L`, or `C` as the value for this flag.

`-R, -ROI [c=component index,U=upshifting value]`

Quantization indices upshifted for a component. 

Warning: This option does not implement the usual ROI (Region of Interest). It should be understood as a "Component of Interest". It offers the possibility to upshift the value of a component during quantization step. The value after `c=` is the component number `[0, 1, 2, ...]` and the value after `U=` is the value of upshifting. U must be in the range `[0, 37]`.

`-d, -ImageOffset [x offset,y offset]`

Offset of the image origin. The division in tile could be modified as the anchor point for tiling will be different than the image origin. Keep in mind that the offset of the image can not be higher than the tile dimension if the tile option is used. The two values are respectively for `X` and `Y` axis offset. Default: no offset.

`-T, -TileOffset [x offset,y offset]`

Offset of the tile origin. The two values are respectively for X and Y axis offset. The tile anchor point can not be inside the image area. Default: no offset.

`-Y, -mct [0|1|2]`

Specify explicitly if a Multiple Component Transform has to be used. 

* 0: no MCT 
* 1: RGB->YCC conversion 
* 2: custom MCT. 

For custom MCT, `-m` option has to be used (see below). By default, `RGB`->`YCC` conversion is used if there are three components or more, otherwise no conversion.

`-m, -CustomMCT [file]`

Use custom array-based MCT of 32 bit signed values, comma separated, line-by-line no specific separators between lines, no space allowed between values. If this option is used, it automatically sets `[-Y|-mct]` option equal to 2.

`-Q, -CaptureRes [capture resolution X,capture resolution Y]`

Capture resolution in pixels/metre, in double precision.

* If the input image has a resolution stored in its header, then this resolution will be set as the capture resolution, by default.
* If the `-Q` command line parameter is set, then it will override the resolution stored in the input image, if present
* The special values `[0,0]` for `-Q` will force the encoder to **not** store capture resolution, even if present in input image. 

`-D, -DisplayRes [display resolution X,display resolution Y]`

Display resolution in pixels/metre, in double precision. 
The special values `[0,0]` for `-D` will force the encoder to set the display resolution equal to the capture resolution. 

`-C, -Comment [comment]`

Add `<comment>` in comment marker segment(s). Multiple comments (up to a total of 256) can be specified, separated by the `|` character. For example:   `-C "This is my first comment|This is my second` will store `This is my first comment` in the first comment marker segment, and `This is my second` in a second comment marker.

`-W, -logfile [output file name]`

Log to file. File name will be set to `output file name`

`-H, -num_threads [number of threads]`

Number of threads used for T1 compression. Default is total number of logical cores.

`-J, -Duration [duration]`

Duration in seconds for a batch compress job. `grk_compress` will exit when duration has been reached.

`-e, -Repetitions [number of repetitions]`
Number of repetitions, for either a single image, or a folder of images. Default value is `1`. Unlimited repetitions are specified by a value of `0`.

`-g, -PluginPath [plugin path]`

Path to Grok plugin, which handles T1 compression. 
Default search path for plugin is in same folder as `grk_compress` binary

`-G, -DeviceId [device ID]`

For Grok plugin running on multi-GPU system. Specifies which single GPU accelerator to run codec on.
If the flag is set to -1, all GPUs are used in round-robin scheduling. If set to -2, then plugin is disabled and
compression is done on the CPU. Default value: 0.


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
