% grk_transcode(1) Version 10.0 | transcode JPEG 2000 files

NAME
====

grk_transcode - transcode JPEG 2000 files without full decompression

SYNOPSIS
========

| **grk_transcode** \[**-i** infile.jp2] \[**-o** outfile.jp2] \[options]


DESCRIPTION
===========

This program transcodes `JPEG 2000` files (JP2 container format), rewriting
JP2 boxes and optionally modifying the codestream without full decompression.
The codestream is parsed at the packet level (T2 only — entropy decoding is
skipped) and reassembled with the requested modifications.

Supported operations:

* Insert TLM (Tile-part Length) markers for random access
* Insert PLT (Packet Length) markers for random access
* Inject SOP (Start of Packet) markers before each packet
* Inject EPH (End of Packet Header) markers after each packet header
* Truncate quality layers
* Strip resolution levels
* Reorder packet progression

All operations can be combined freely.

Options
-------

`-h, --help`

Print a help message and exit.

`-v, --version`

Print library version and exit.

`-i, --input [file]`

Input JP2 file. Required.

`-o, --output [file]`

Output JP2 file. Required.

`-X, --tlm`

Insert TLM markers in the output codestream. Default: off.

`-L, --plt`

Insert PLT markers in the output codestream. Default: off.

`-S, --sop`

Inject SOP marker before each packet. Default: off.

`-E, --eph`

Inject EPH marker after each packet header. Default: off.

`-n, --max-layers [number of layers]`

Keep at most this many quality layers. Packets belonging to higher layers are
discarded. A value of 0 (default) keeps all layers.

`-R, --max-res [number of resolutions]`

Keep at most this many resolution levels. Packets belonging to higher resolutions
are discarded. A value of 0 (default) keeps all resolutions.

`-p, --progression [LRCP|RLCP|RPCL|PCRL|CPRL]`

Reorder packets to the specified progression order. Default: preserve original order.

The five progression orders are:
* `LRCP` — Layer-Resolution-Component-Position
* `RLCP` — Resolution-Layer-Component-Position
* `RPCL` — Resolution-Position-Component-Layer
* `PCRL` — Position-Component-Resolution-Layer
* `CPRL` — Component-Position-Resolution-Layer

EXAMPLES
========

Insert TLM and PLT markers:

    grk_transcode -i input.jp2 -o output.jp2 -X -L

Add SOP/EPH markers and change progression to RLCP:

    grk_transcode -i input.jp2 -o output.jp2 -S -E -p RLCP

Keep only 3 quality layers and 4 resolution levels:

    grk_transcode -i input.jp2 -o output.jp2 -n 3 -R 4

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

**grk_compress(1)**, **grk_decompress(1)**
