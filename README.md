# Grok: Open-Source JPEG 2000 Codec That Can Beat Leading Commercial Codec

[![badge-license]][link-license]

<span>
 <a href="https://jpeg.org/jpeg2000/index.html" target="_blank">
  <img src="https://jpeg.org/images/jpeg2000-logo.svg" width=200, height=200 />
 </a>
</span>
<p>

Grok is production-ready, fully open source (AGPL v3), and consistently **faster than Kakadu** (the commercial gold standard) in real-world geospatial workloads.

In GDAL benchmarks Grok delivers **up to 10× faster** decompression on network storage and crushes both Kakadu and OpenJPEG on large images and region decoding.

### Performance That Speaks for Itself

Integrated into [GDAL](https://gdal.org) via the official [JP2Grok driver](https://github.com/GrokImageCompression/gdal).

**Benchmark results** (16 threads, GDAL release build on Fedora 42):

| Workflow                     | Grok (JP2Grok) | Kakadu (JP2KAK) | OpenJPEG     |
|------------------------------|----------------|------------------|--------------|
| **Spot 6 (Network Storage)** | **35.17 s**    | 344 s            | 85 s         |
| **Spot 6 (Local Storage)**   | **26.92 s**    | 30.57 s          | 52.09 s      |
| **Pleiades (Region)**        | **0.74 s**     | 1.41 s           | 4.28 s       |

### Battle-Tested Reliability
- **2,000+ unit tests**
- Continuous fuzzing via **OSS-Fuzz** (see live status below)
- Used in production geospatial pipelines worldwide

### Killer Features
- Full **High Throughput JPEG 2000 (HTJ2K)** support
- Lightning-fast random-access sub-image decoding (TLM + PLT markers)
- Complete ICC color profiles, XML/IPTC/XMP/EXIF metadata
- Monochrome, sRGB, palette, YCC, extended YCC, CIELab, CMYK
- 1–16 bit precision + JPEG/PNG/BMP/TIFF/RAW/PNM/PAM I/O
- Runs on Linux (x86-64/AArch64), Windows, macOS, and WebAssembly

### Get Started
- [Installation](https://github.com/GrokImageCompression/grok/blob/master/INSTALL.md)
- [Wiki & Docs](https://github.com/GrokImageCompression/grok/wiki)
- [GitHub repo](https://github.com/GrokImageCompression/grok)

### Build Status
[![badge-actions]][link-actions]
[![badge-oss-fuzz]][link-oss-fuzz]

---

[badge-license]: https://img.shields.io/badge/License-AGPL%20v3-blue.svg
[link-license]: https://github.com/GrokImageCompression/grok/blob/master/LICENSE
[badge-actions]: https://github.com/GrokImageCompression/grok/actions/workflows/build.yml/badge.svg?branch=master
[link-actions]: https://github.com/GrokImageCompression/grok/actions
[badge-oss-fuzz]: https://oss-fuzz-build-logs.storage.googleapis.com/badges/grok.svg
[link-oss-fuzz]: https://issues.oss-fuzz.com/issues?q=proj:grok