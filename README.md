## Grok is the leading open source JPEG 2000 codec

[![badge-license]][link-license]

<span>
 <a href="https://jpeg.org/jpeg2000/htj2k.html" target="_blank">
  <img src="https://jpeg.org/images/jpeg2000-logo.svg" width=200, height=200 />
 </a>
</span>
<p>


Features include:

* support for new **High Throughput JPEG 2000 (HTJ2K)** standard
* fast random-access sub-image decoding using `TLM` and `PLT` markers
* full encode/decode support for `ICC` colour profiles
* full encode/decode support for `XML`,`IPTC`, `XMP` and `EXIF` meta-data
* full encode/decode support for `monochrome`, `sRGB`, `palette`, `YCC`, `extended YCC`, `CIELab` and `CMYK` colour spaces
* full encode/decode support for `JPEG`,`PNG`,`BMP`,`TIFF`,`RAW`,`PNM` and `PAM` image formats
* full encode/decode support for 1-16 bit precision


Please see:

1. [INSTALL](https://github.com/GrokImageCompression/grok/blob/master/INSTALL.md) for installation guide
1. [WIKI](https://github.com/GrokImageCompression/grok/wiki) for library usage
1. [LICENSE][link-license] for license and copyright information

## Current Build Status
[![badge-build]][link-build]
[![badge-msvc-build]][link-msvc-build]
[![badge-oss-fuzz]][link-oss-fuzz]  

[badge-license]: https://img.shields.io/badge/License-AGPL%20v3-blue.svg "AGPL 3.0"
[link-license]: https://github.com/GrokImageCompression/grok/blob/master/LICENSE "AGPL 3.0"
[badge-build]: https://travis-ci.org/GrokImageCompression/grok.svg?branch=master "Build Status"
[link-build]: https://travis-ci.org/GrokImageCompression/grok "Build Status"
[badge-msvc-build]: https://ci.appveyor.com/api/projects/status/github/GrokImageCompression/grok?branch=master&svg=true "Windows Build Status"
[link-msvc-build]: https://ci.appveyor.com/project/boxerab/grok/branch/master "Windows Build Status"
[badge-oss-fuzz]: https://oss-fuzz-build-logs.storage.googleapis.com/badges/grok.svg "Fuzzing Status"
[link-oss-fuzz]: https://bugs.chromium.org/p/oss-fuzz/issues/list?sort=-opened&can=1&q=proj:grok "Fuzzing Status"
