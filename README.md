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
* supported platforms: Linux x86-64, Linux AArch64, Windows, macOS and WebAssembly


### Grok in the News

1. [Accelerating Grok With Blosc2](https://www.blosc.org/posts/blosc2-grok-release/)

2. [Generating Lossy Access JP2s With Grok](https://www.bitsgalore.org/2022/03/30/generating-lossy-access-jp2s-from-lossless-preservation-masters)


### Performance

Grok can be integrated into the Geospatial Data Abstraction Layer ([GDAL](https://gdal.org/en/stable/)) software with this downstream [driver](https://github.com/GrokImageCompression/gdal). Below is a benchmark comparing **decompression** time performance for **GDAL** using **JP2Grok**, **JP2KAK**, and **JP2OpenJPEG** drivers.

#### Benchmark Details

* test system : 8 core / 16 thread CPU running `Fedora 42` with `6.10` Linux kernel and `btrfs` file system
* Grok 20.0.0, Kakadu 8.4.1 and OpenJPEG 2.5.4 were used, configured to use all 16 threads
* Grok was built in release mode using `GCC 15.2`
* Linux page cache was cleared before each local file decompression
* decompress command: `gdal_translate $FILE -if $DRIVER output.tif`

#### Test Files

- **Spot 6 (Network Storage)**: 26624 x 26624 image, 4 components, TLM, lossless, 12-bit, 2048 x 2048 tiles, 10 layers, stored on MinIO with 20 ms latency.
- **Spot 6 (Local Storage)**: 26624 x 26624 image, 4 components, TLM, lossless, 12-bit, 2048 x 2048 tiles, 10 layers, stored locally.
- **Pleiades (Region)**: 82704 x 81100 image, 8000 x 8000 region at (50000,50000), 1 component, TLM, lossy, 16-bit, 1024 x 1024 tiles, 15 layers, stored locally.


#### Results


| Workflow                     | JP2Grok       | JP2KAK       | JP2OpenJPEG       |
|------------------------------|---------------|--------------|-------------------|
| Spot 6 (Network Storage)     | 35.17 s       | 344 s        | 85 s              |
| Spot 6 (Local Storage)       | 26.92 s       | 30.57 s      | 34.91 s           |
| Pleiades (Region)            | 0.74 s        | 1.41 s       | 4.28 s            |


### Library Details

* [INSTALL](https://github.com/GrokImageCompression/grok/blob/master/INSTALL.md)
* [WIKI](https://github.com/GrokImageCompression/grok/wiki)
* [LICENSE][link-license]

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
[link-oss-fuzz]: https://issues.oss-fuzz.com/issues?q=proj:grok