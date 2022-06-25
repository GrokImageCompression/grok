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

Below is a benchmark comparing time and memory performance for **Grok 9.7.8** and **Kakadu 8.05** on the following workflows:

1. decompress large single-tiled [image of Mars](https://hirise.lpl.arizona.edu/PDS/RDR/ESP/ORB_011200_011299/ESP_011277_1825/ESP_011277_1825_RED.JP2) to TIF output
1. decompress region `(1000,1000,5000,5000)` from large single-tiled [image of Mars](https://hirise.lpl.arizona.edu/PDS/RDR/ESP/ORB_011200_011299/ESP_011277_1825/ESP_011277_1825_RED.JP2) to TIF output
1. decompress large multi-tiled [Pleiades image](https://l3harrisgeospatial-webcontent.s3.amazonaws.com/MM_Samples/Pleiades_ORTHO_UTM_BUNDLE.zip) to TIF output.
1. decompress large multi-tiled [Pleiades image](https://l3harrisgeospatial-webcontent.s3.amazonaws.com/MM_Samples/Pleiades_ORTHO_UTM_BUNDLE.zip) to PGM output.
1. decompress 6 resolutions from `580000x825000` single-tiled [image of Luxembourg](https://s3.eu-central-1.amazonaws.com/download.data.public.lu/resources/orthophoto-officelle-du-grand-duche-de-luxembourg-edition-2020/20210602-110516/Luxembourg-2020_ortho10cm_RVB_LUREF.jp2)
1. decompress 7 resolutions from `580000x825000` single-tiled [image of Luxembourg](https://s3.eu-central-1.amazonaws.com/download.data.public.lu/resources/orthophoto-officelle-du-grand-duche-de-luxembourg-edition-2020/20210602-110516/Luxembourg-2020_ortho10cm_RVB_LUREF.jp2)
1. decompress 8 resolutions from `580000x825000` single-tiled [image of Luxembourg](https://s3.eu-central-1.amazonaws.com/download.data.public.lu/resources/orthophoto-officelle-du-grand-duche-de-luxembourg-edition-2020/20210602-110516/Luxembourg-2020_ortho10cm_RVB_LUREF.jp2)
1. decompress region `(574200,816750,580000,825000)` from `580000x825000` single-tiled [image of Luxembourg](https://s3.eu-central-1.amazonaws.com/download.data.public.lu/resources/orthophoto-officelle-du-grand-duche-de-luxembourg-edition-2020/20210602-110516/Luxembourg-2020_ortho10cm_RVB_LUREF.jp2)



#### Benchmark Details

* test system : 24 core / 48 thread `AMD Threadripper`
running `Fedora 36` with `5.17` Linux kernel and `xfs` file system
* codecs were configured to use all 48 threads
* file cache was cleared before each decompression using `$ sudo sysctl vm.drop_caches=3`
* Grok was built in release mode using `GCC 10`

#### Results

| Test  | Grok               | Kakadu
| :---- | :-----             | :------: 
| 1     | 13.74 s / 16.6 GB  | 9.02 s / 0.05 GB
| 2     | 0.25 s / 0.4 GB    | 0.12 s   
| 3     | 3.21 s / 3.65 GB   | 4.94 s / 0.1 GB
| 4     | 2.94 s / 4.0 GB    | 3.90 s / 0.09 GB
| 5     | 0.37 s / 0.7 GB    | 2.72 s / 1.0 GB
| 6     | 0.67 s / 1.0 GB    | 3.02 s / 1.0 GB
| 7     | 1.76 s / 1.8 GB    | 3.72 s / 1.1 GB
| 8     | 2.89 s / 6.0 GB    | 7.39 s / 1.1 GB

### Library Details

* [INSTALL](https://github.com/GrokImageCompression/grok/blob/master/INSTALL.md)
* [WIKI](https://github.com/GrokImageCompression/grok/wiki)
* [LICENSE][link-license]

### Current Build Status
[![badge-actions]][link-actions]
[![badge-oss-fuzz]][link-oss-fuzz]  

[badge-license]: https://img.shields.io/badge/License-AGPL%20v3-blue.svg
[link-license]: https://github.com/GrokImageCompression/grok/blob/master/LICENSE
[badge-actions]: https://github.com/GrokImageCompression/grok/actions/workflows/build.yml/badge.svg?branch=master
[link-actions]: https://github.com/GrokImageCompression/grok/actions
[badge-oss-fuzz]: https://oss-fuzz-build-logs.storage.googleapis.com/badges/grok.svg
[link-oss-fuzz]: https://bugs.chromium.org/p/oss-fuzz/issues/list?sort=-opened&can=1&q=proj:grok
