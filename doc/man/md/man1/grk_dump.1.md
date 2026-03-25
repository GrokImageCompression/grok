% grk_dump(1) Version 10.0 | dump JPEG 2000 code stream to stdout or to file

NAME
====

grk_dump - dump JPEG 2000 code stream to stdout or to file


SYNOPSIS
========

| **grk_dump** \[**-i** infile.jp2]

DESCRIPTION
===========

This program dumps the header and/or codestream of a JPEG 2000 file to stdout or to a file.

**Important note on command line argument notation below**: the outer square braces appear for clarity only, and **should not** be included in the actual command line argument. Square braces appearing inside the outer braces **should** be included.


Options
-------


`-h`

Print a help message and exit.

`--help`

Show detailed usage.

`-i, --input [file]`

Input file. Required if `--batch-src` option is not provided.

`-o, --output [file]`

Output file. Default: stdout.

`-y, --batch-src [directory]`

Path to image files directory. Required if `-i` option is not provided.

`-f, --flag [value]`

Flag value. Default: `0`.

* `0` : dump header only
* `1` : dump codestream only
* `2` : dump both header and codestream


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

**grk_decompress(1)**, **grk_compress(1)**


