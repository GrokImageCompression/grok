# Grok

[![badge-license]][link-license]
[![badge-actions]][link-actions]
[![badge-oss-fuzz]][link-oss-fuzz]

<span>
 <a href="https://jpeg.org/jpeg2000/index.html" target="_blank">
  <img src="https://jpeg.org/images/jpeg2000-logo.svg" width=200, height=200 />
 </a>
</span>
<p>

Grok is an open-source JPEG 2000 codec licensed under AGPL v3.

### Performance

Grok is integrated into [GDAL](https://gdal.org).
Benchmark results (16 threads, GDAL release build, Fedora 42):

| Workflow                     | Grok (JP2Grok) | Kakadu (JP2KAK) | OpenJPEG     |
|------------------------------|----------------|------------------|--------------|
| Spot 6 (Network Storage)    | 35.17 s        | 344 s            | 85 s         |
| Spot 6 (Local Storage)      | 26.92 s        | 30.57 s          | 52.09 s      |
| Pleiades (Region)           | 0.74 s         | 1.41 s           | 4.28 s       |

### Features
- High Throughput JPEG 2000 (HTJ2K) support
- Random-access sub-image decoding (TLM + PLT markers)
- ICC color profiles, XML/IPTC/XMP/EXIF metadata
- Monochrome, sRGB, palette, YCC, extended YCC, CIELab, CMYK
- 1–16 bit precision
- JPEG/PNG/BMP/TIFF/RAW/PNM/PAM I/O
- Linux (x86-64/AArch64), Windows, macOS, WebAssembly

### Transcoding (`grk_transcode`)

`grk_transcode` rewrites JP2/J2K files at the packet level — no full decompression required. Supported operations:

- Insert **TLM** (Tile-part Length) and **PLT** (Packet Length) markers for random access
- Inject **SOP** / **EPH** markers
- Truncate quality layers (`--max-layers`)
- Strip resolution levels (`--max-res`)
- Reorder packet progression (LRCP, RLCP, RPCL, PCRL, CPRL)

Example — add TLM + PLT markers to an existing file:
```
grk_transcode -i input.jp2 -o output.jp2 -X -L
```

### Testing
- 2,000+ unit tests
- Continuous fuzzing via [OSS-Fuzz](https://issues.oss-fuzz.com/issues?q=proj:grok)

### Language Bindings
- [Python, C#, Java](bindings/swig/) (SWIG)
- [Rust](bindings/rust/) (bindgen)

### Links
- [Installation](INSTALL.md)
- [Language Bindings](bindings/swig/README.md)
- [Wiki & Docs](https://github.com/GrokImageCompression/grok/wiki)
- [GitHub](https://github.com/GrokImageCompression/grok)

### Quick Build
```
git clone --recursive https://github.com/GrokImageCompression/grok.git
cd grok
cmake -B build
cmake --build build --parallel
```

---

[badge-license]: https://img.shields.io/badge/License-AGPL%20v3-blue.svg
[link-license]: https://github.com/GrokImageCompression/grok/blob/master/LICENSE
[badge-actions]: https://github.com/GrokImageCompression/grok/actions/workflows/build.yml/badge.svg?branch=master
[link-actions]: https://github.com/GrokImageCompression/grok/actions
[badge-oss-fuzz]: https://oss-fuzz-build-logs.storage.googleapis.com/badges/grok.svg
[link-oss-fuzz]: https://issues.oss-fuzz.com/issues?q=proj:grok