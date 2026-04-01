% grk_transcode(1) Version 10.0 | transcode JPEG 2000 files

NAME
====

grk_transcode - transcode JPEG 2000 files without full decompression

SYNOPSIS
========

| **grk_transcode** \[**-i** infile] \[**-o** outfile] \[options]


DESCRIPTION
===========

This program transcodes `JPEG 2000` files, rewriting boxes or markers and
optionally modifying the codestream without full decompression. The codestream
is parsed at the packet level (T2 only — entropy decoding is skipped) and
reassembled with the requested modifications.

Supported formats:

* JP2 (`.jp2`) — ISO 15444-1 container
* JPH (`.jph`) — ISO 15444-15 High-Throughput JPEG 2000 container
* J2K / J2C / JPC (`.j2k`, `.j2c`, `.jpc`) — raw codestream

Format conversion:

When the input is a container (JP2/JPH) and the output is a raw codestream
(J2K/J2C/JPC), all box metadata (XMP, ICC profile, GeoTIFF UUID, etc.) is
stripped and only the codestream is written. Any requested codestream
modifications (TLM, PLT, SOP, EPH, etc.) are applied before extraction.

Converting from raw codestream to container (J2K -> JP2) is not supported;
use **grk_compress** to create a container from scratch.

The input format is detected automatically from file content (magic bytes),
not the file extension.

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

Input file (JP2, JPH, or raw J2K/J2C/JPC codestream). Required.

`-o, --output [file]`

Output file (JP2, JPH, or raw J2K/J2C/JPC codestream). Required.

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

Insert TLM and PLT markers into a JP2 file:

    grk_transcode -i input.jp2 -o output.jp2 -X -L

Add SOP/EPH markers and change progression to RLCP:

    grk_transcode -i input.jp2 -o output.jp2 -S -E -p RLCP

Keep only 3 quality layers and 4 resolution levels:

    grk_transcode -i input.jp2 -o output.jp2 -n 3 -R 4

Transcode a JPH (High-Throughput JPEG 2000) file:

    grk_transcode -i input.jph -o output.jph -X -L

Extract raw codestream from a JP2 container:

    grk_transcode -i input.jp2 -o output.j2k

Extract codestream from JP2 and insert TLM markers:

    grk_transcode -i input.jp2 -o output.j2k -X

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
