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
* fast random-access sub-image decoding using `TLM` and `PLT` markers
* full encode/decode support for `ICC` colour profiles
* full encode/decode support for `XML`,`IPTC`, `XMP` and `EXIF` meta-data
* full encode/decode support for `monochrome`, `sRGB`, `palette`, `YCC`, `extended YCC`, `CIELab` and `CMYK` colour spaces
* full encode/decode support for `JPEG`,`PNG`,`BMP`,`TIFF`,`RAW`,`PNM` and `PAM` image formats
* full encode/decode support for 1-16 bit precision images

### Performance

Below is a benchmark comparing **Kakadu 8.05**, **Grok 9.2** and **OpenJPEG 2.4**
on the following workflows:

1. compress [image](https://github.com/GrokImageCompression/grok-test-data/blob/master/input/nonregression/Bretagne2.ppm) using many small tiles
1. decompress full [large single-tiled image of Mars](http://hirise-pds.lpl.arizona.edu/PDS/RDR/ESP/ORB_011200_011299/ESP_011277_1825/ESP_011277_1825_RED.JP2)
1. decompress region {1000,1000,5000,5000} from [large single-tiled image of Mars](http://hirise-pds.lpl.arizona.edu/PDS/RDR/ESP/ORB_011200_011299/ESP_011277_1825/ESP_011277_1825_RED.JP2)

* test system : 24 core / 48 thread AMD Threadripper
running Ubuntu 20.04 with 5.8 kernel
* codecs were configured to use all 48 threads
* timing measured in seconds

| Test | Kakadu | Grok     | OpenJPEG  |
| :---- | :----- | :------: | --------: |
| 1     | 0.17   | 0.34     | 1.64      |
| 2     | 9.81   | 15.85    | 38.57     |
| 3     | 0.12   | 0.17     | 0.88      |

### Library Details

* [INSTALL](https://github.com/GrokImageCompression/grok/blob/master/INSTALL.md)
* [WIKI](https://github.com/GrokImageCompression/grok/wiki)
* [LICENSE][link-license]

### Current Build Status
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
