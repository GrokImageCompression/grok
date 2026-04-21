# Incremental Stripe Compositing

Grok supports incremental, row-at-a-time writing of decompressed JPEG 2000
images to output formats (TIFF, PNM, PNG, JPEG).  Instead of decompressing
every tile into a full-resolution composite image and then writing the whole
image at once, tiles are composited and written one tile-row at a time.  This
keeps resident memory proportional to a single tile-row rather than the entire
image.

## Overview

```
┌──────────────────────────────────────────────────────────────────────┐
│                         Parser Thread                                │
│  Reads codestream markers (SOT) and feeds tile data to decompression │
│  Back-pressure: blocks when tile row > nextBandTileY_ + 1            │
└────────────────────┬─────────────────────────────────────────────────┘
                     │  schedule(tileProcessor)
                     ▼
┌──────────────────────────────────────────────────────────────────────┐
│                     Taskflow Thread Pool                              │
│  T2 (packet parsing) → T1 (entropy decode) → DWT → post callback     │
└────────────────────┬─────────────────────────────────────────────────┘
                     │  tileCompletion_->complete(tileIndex)
                     ▼
┌──────────────────────────────────────────────────────────────────────┐
│                    Row Completion Callback                            │
│  Composites tiles into scratchImage_, calls ioBandCallback_,          │
│  releases tile memory, advances strip buffer                         │
└────────────────────┬─────────────────────────────────────────────────┘
                     │  ioBandCallback_(yBegin, yEnd, scratchImage_)
                     ▼
┌──────────────────────────────────────────────────────────────────────┐
│                    Format Writer (e.g. TIFFFormat)                    │
│  writeImageBand(): interleave planar→packed, write strip to disk      │
└──────────────────────────────────────────────────────────────────────┘
```

## When Incremental Writes Are Enabled

The CLI application (`GrkDecompress`) enables incremental writes when **all**
of the following are true:

1. `storeToDisk` is true (not a memory-only decode)
2. Not a single-tile decompress (`!parameters->single_tile_decompress`)
3. Post-processing is a no-op (`grk_image_is_post_process_no_op`) — i.e. no
   colour-space conversion, ICC profile application, or precision scaling
4. No windowed vertical crop (`parameters->dw_y1 == 0`)
5. The output format supports incremental band writes
   (`fmt->supportsIncrementalBandWrite()`)

When enabled, the application calls `grk_decompress_set_band_callback()` which
stores `ioBandCallback_` on the `CodeStreamDecompress` instance.

## Key Data Structures

### scratchImage_

A `GrkImage` that acts as a strip buffer.  Its component data arrays are sized
to hold one tile-row of pixels.  After each tile-row is written, the component
`y0` and `h` fields are advanced to the next tile-row so that tile compositing
writes into the correct location.

### TileCompletion

Tracks which tiles have finished decompressing.  When all tiles in a tile-row
are complete (`completedTilesPerRow_[row] == subregionWidth_`), the row
completion callback fires.

Key members:
- `completedTiles_[]` — per-tile completion flag
- `completedTilesPerRow_[]` — counter per tile-row
- `rowCallback_` — fired when an entire row of tiles finishes
- `heap_` — min-heap for tracking contiguous completion (used by `wait()`)

### pendingBands_

An `std::unordered_map<uint16_t, BandInfo>` mapping tile-row Y to its band
parameters (`yBegin`, `yEnd`, `tileX0`, `numCols`).  Because tiles in a row
may complete out-of-order relative to other rows, the row callback inserts
into this map and then drains from `nextBandTileY_` forward in order.

### Back-Pressure Variables

- `nextBandTileY_` — the next tile-row to be drained (composited + written)
- `bandOrderMutex_` — protects `nextBandTileY_` and `pendingBands_`
- `bandDrainCV_` — wakes the parser thread when a row is drained

## Lifecycle of a Tile Row

### 1. Parsing and Scheduling

The parser thread (sequential or TLM path) reads tile data from the
codestream.  Before scheduling each tile for decompression, it checks back
pressure:

```cpp
// Block if this tile's row is too far ahead of the drained row
if (ioBandCallback_ && tileCompletion_) {
    uint16_t tileY = tileIndex / numTileCols;
    std::unique_lock<std::mutex> lock(bandOrderMutex_);
    while (!(tileY < nextBandTileY_ + 2 || !success_))
        bandDrainCV_.wait_for(lock, std::chrono::milliseconds(100));
}
```

This limits the parser to scheduling at most **2 tile-rows ahead** of the
currently drained row.  This bounds memory to roughly 2 tile-rows of
decompressed data.

### 2. Decompression (Taskflow)

Each tile is decompress-scheduled through Taskflow:
T2 (packet parse) → T1 (entropy decode) → DWT (wavelet inverse) →
`postMultiTile(tileProcessor)` callback.

The `postMultiTile` callback:
1. Calls `post_decompressT2T1(scratchImage_)` to extract tile data into a
   per-tile image
2. Increments `numTilesDecompressed_`
3. Skips the global `scratchImage_->composite()` call (which the non-band path
   uses) since compositing is deferred to the row callback
4. Calls `tileCompletion_->complete(tileIndex)`

### 3. Row Completion

When `TileCompletion::complete()` detects that all tiles in a row are done, it
fires the row callback **outside** the TileCompletion lock.

The row callback (a lambda created during `decompressAllTiles()`) does:

1. **Insert into `pendingBands_`** — records the band's Y extents and tile
   column range.

2. **Drain in order** — walks `pendingBands_` starting from `nextBandTileY_`:

   a. **Composite** — for each tile in the row, copies its per-tile image into
      `scratchImage_` via `scratchImage_->composite(tileImage)`.  This is a
      per-component `memcpy` of each row of each tile into the correct position
      in the strip buffer.

   b. **Write band** — calls `ioBandCallback_(yBegin, yEnd, scratchImage_)`.
      The application's callback (`grkWriteBandCallback`) calls
      `imageFormat->writeImageBand(yBegin, yEnd)`, which interleaves the
      planar int32 data into packed bytes (SIMD-accelerated for 8/16-bit) and
      writes one or more TIFF strips to disk via `TIFFWriteEncodedStrip`.

   c. **Release tiles** — calls `tileCache_->releaseForSwath(tileIndex)` for
      each tile in the row and `MemoryManager::releaseFreedPages()` to return
      freed pages to the OS, dropping RSS.

   d. **Advance strip buffer** — updates `scratchImage_` component `y0` and
      `h` for the next tile row so that future compositing writes land in the
      correct location.

   e. **Advance `nextBandTileY_`** and notify `bandDrainCV_` so the parser
      thread can schedule more tiles.

### 4. Final Cleanup

After `decompressAllTiles()` completes, `postMultiTile()` (the no-arg variant)
runs.  When `ioBandCallback_` is set, it skips the `transferDataTo` and
`postProcess` steps since data was already incrementally consumed from
`scratchImage_`.

## Format Writer: TIFFFormat::writeImageBand()

For non-subsampled images (the common path):

1. Creates a `PlanarToInterleaved` interleaver via `InterleaverFactory`.
2. For each strip-worth of rows (`rows_per_strip`):
   - Allocates a packed buffer from the I/O buffer pool
   - Calls `interleaver_->interleave()` — for 8-bit and 16-bit, this uses
     Highway SIMD (`StoreInterleaved3`/`StoreInterleaved4`) to convert
     planar int32 to packed uint8/uint16
   - Calls `writeStripCore()` → `TIFFWriteEncodedStrip()` to write to disk
   - Returns the buffer to the pool

For subsampled YCbCr images, a scalar loop packs luma and chroma samples
according to the TIFF YCbCr layout (luma block + Cb + Cr per MCU).

## Memory Behaviour

Without incremental compositing, a 40000×40000 8-bit RGB image requires
~4.5 GB for the full composite.  With incremental compositing and 256-pixel
tile rows, only ~2 tile-rows (~40 MB) of decompressed data are resident at
any time.

The `MemoryManager::releaseFreedPages()` call after each row release uses
`madvise(MADV_DONTNEED)` (Linux) to return freed pages to the OS, ensuring
RSS tracks the working set rather than the high-water mark.

## Code Paths

| Codestream Type | Function | Back-Pressure |
|-----------------|----------|---------------|
| Sequential (no TLM) | `sequentialParseAndSchedule()` | Blocks parser at `bandOrderMutex_` |
| TLM, non-batched | `decompressTLM()` | Blocks per-tile at `bandOrderMutex_` |
| TLM, batched (async) | `scheduleTileBatch()` | Queue depth limit via `batchTileQueueCondition_` |
