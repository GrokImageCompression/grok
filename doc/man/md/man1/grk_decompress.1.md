% grk_decompress(1) Version 7.6 | convert from JPEG 2000 format

NAME
====

**grk_decompress** — decompresses an image in the JPEG 2000 format to a different
image format

SYNOPSIS
========

| **grk_dcompress** \[**-i** infile.j2k] \[**-o** outfile.bmp]

DESCRIPTION
===========



Options
-------

`-h,  -help` 

Print a help message and exit.

`-version` 

Print library version and exit.

####  `-v, -verbose`

Output information and warnings about decoding to console (errors are always output). Default is false i.e. console is silent by default.

####  `-i, -InputFile [file]`

Input file. Either this argument or the `-ImgDir` argument described below is required. Valid input image extensions are J2K, JP2 and JPC. When using this option output file must be specified using -o.

####  `-o, -OutputFile [file]`

Output file. Required when using `-i` option. See above for supported file types. If a `PGX` filename is given, there will be as many output files as there are components: an index starting from 0 will then be appended to the output filename, just before the `pgx` extension. If a `PGM` filename is given and there are more than one component, only the first component will be written to the file.

#### `-y, -ImgDir [directory path]`

Path to the folder where the compressed images are stored. Either this argument or the `-i` argument described above is required. When image files are in the same directory as the executable, this can be indicated by a dot `.` argument. When using this option, the output format must be specified using `-OutFor`. The output images are saved in the same folder.

#### `-a, -OutDir [output directory]'

Output directory where compressed files are stored. Only relevant when the `-ImgDir` flag is set. Default: same directory as specified by `-ImgDir`.

#### `-O, -OutFor [format]`

Output format used to decompress the code streams. Required when `-ImgDir` option is used. See above for supported formats.

#### `-r, -Reduce [reduce factor]`

Reduce factor. Set the number of highest resolution levels to be discarded. The image resolution is effectively divided by 2 to the power of the number of discarded levels. The reduce factor is limited by the smallest total number of decomposition levels among tiles.

#### `-l, -Layer [layer number]`

Layer number. Set the maximum number of quality layers to decode. If there are fewer quality layers than the specified number, all quality layers will be decoded.

#### `-d, -DecodeRegion [x0,y0,x1,y1]`

Decode a sub-region of the image. If `(X,Y)` is a location in the image, then it will only be decoded
if `x0 <= X < x1` and `y0 <= Y < y1`. By default, the entire image is decoded.

#### `-c, -Compression [compression value]` 

Compress output image data. Currently, this flag is only applicable when output format is set
to `TIF`. Possible values are {`NONE`, `LZW`,`JPEG`, `PACKBITS`. `ZIP`,`LZMA`,`ZSTD`,`WEBP`}. 
Default value is `NONE`.

#### `-L, -CompressionLevel [compression level]` 

"Quality" of compression. Currently only implemented for `PNG` format. 
For `PNG`, compression level ranges from 0 (no compression) up to 9.
Grok default value is 3.

Note: PNG is always lossless, so using a different level will not affect the image quality. It only changes
the speed vs file size tradeoff.

#### `-t, -TileIndex [tile index]`

Only decode tile with specified index. Index follows the JPEG2000 convention from top-left to bottom-right. By default all tiles are decoded.

#### `-p, -Precision [component 0 precision[C|S],component 1 precision[C|S],...]`

Force precision (bit depth) of components. There must be at least one value present, but there is no limit on the number of values. 
The last values are ignored if too many values. If there are fewer values than components, the last value is used for the remaining components. If `C` is specified (default), values are clipped. If `S` is specified, values are scaled. Specifying a `0` value indicates use of the original bit depth.

Example:

     `-p 8C,8C,8c` 

Clip all components of a 16 bit RGB image to 8 bits.

#### `-f, -force-rgb`

Force output image color space to `RGB`. For `TIF/TIFF` or `PNG` output formats, the ICC profile will be applied in this case - default behaviour is to stored the profile in the output file, if supported.

#### `-u, -upsample`

Sub-sampled components will be upsampled to image size.

#### `-s, -split-pnm`

Split output components into different files when writing to `PNM`.

#### `-X, -XML [output file name]`

Store XML metadata to file, if it exists in compressed file. File name will be set to `output file name + ".xml"`

#### `-W, -logfile [output file name]`

Log to file. File name will be set to `output file name`

#### `-H, -num_threads [number of threads]`

Number of threads used for T1 compression. Default is total number of logical cores.

####  `-e, -Repetitions [number of repetitions]`
Number of repetitions, for either a single image, or a folder of images. Default is 1. 0 signifies unlimited repetitions.

#### `-g, -PluginPath [plugin path]`

Path to Grok plugin, which handles T1 decompression. 
Default search path for plugin is in same folder as `grk_decompress` binary

#### `-G, -DeviceId [device ID]`

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

**grk_compress(1)**
