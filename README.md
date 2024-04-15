## World's Leading Open Source JPEG 2000 Codec

[![badge-license]][link-license]

<span>
 <a href="https://jpeg.org/jpeg2000/index.html" target="_blank">
  <img src="https://jpeg.org/images/jpeg2000-logo.svg" width=200, height=200 />
 </a>
</span>
<p>


### Features

* support for new **High Throughput JPEG 2000 (HTJ2K)** standard
* fast random-access sub-image decoding using `TLM` markers
* full encode/decode support for `ICC` colour profiles
* full encode/decode support for `XML`,`IPTC`, `XMP` and `EXIF` meta-data
* full encode/decode support for `monochrome`, `sRGB`, `palette`, `YCC`, `extended YCC`, `CIELab` and `CMYK` colour spaces
* full encode/decode support for `JPEG`,`PNG`,`BMP`,`TIFF`,`RAW`,`PNM` and `PAM` image formats
* full encode/decode support for 1-16 bit precision images
* supported platforms: Linux x86-64, Linux AArch64, Windows, macOS and WebAssembly

### Library Details

* [INSTALL](https://github.com/GrokImageCompression/grok/blob/master/INSTALL.md)
* [WIKI](https://github.com/GrokImageCompression/grok/wiki)
* [LICENSE][link-license]


### Library Roadmap

This project is now stable, feature complete, and performs very well relative to commercial and other open source JPEG 2000 toolkits. 

* No new features are planned - only bugs reported by users will be considered
* Releases older than version 12.0 will no longer be supported and have been removed from release page
* Repository history is no longer needed and has been removed

Have no fear ! The current release continues to pass all 1900+ unit tests. The OSS-Fuzz fuzzer continues
to run, looking for memory leaks and other program errors. The issue tracker continues to be available
for any issues encountered by users.



### Current Build Status
[![badge-actions]][link-actions]
[![badge-oss-fuzz]][link-oss-fuzz]

### Contact

For more information please contact :

`info@grokcompression.com`


[badge-license]: https://img.shields.io/badge/License-AGPL%20v3-blue.svg
[link-license]: https://github.com/GrokImageCompression/grok/blob/master/LICENSE
[badge-actions]: https://github.com/GrokImageCompression/grok/actions/workflows/build.yml/badge.svg?branch=master
[link-actions]: https://github.com/GrokImageCompression/grok/actions
[badge-oss-fuzz]: https://oss-fuzz-build-logs.storage.googleapis.com/badges/grok.svg
[link-oss-fuzz]: https://bugs.chromium.org/p/oss-fuzz/issues/list?sort=-opened&can=1&q=proj:grok
