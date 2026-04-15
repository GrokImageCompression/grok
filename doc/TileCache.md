# Grok Tile Cache Architecture

Grok provides a multi-level tile caching system designed for efficient
random-access decompression of large JPEG 2000 images. The cache minimizes
redundant parsing, reduces memory consumption, and avoids unnecessary network
fetches when reading from cloud storage.

## Cache Strategies

Tile caching is controlled by a strategy bitmask set on the codec before
decompression. Strategies can be combined with bitwise OR.

| Constant | Value | Description |
|----------|-------|-------------|
| `GRK_TILE_CACHE_NONE` | 0 | No caching — each decompress is independent |
| `GRK_TILE_CACHE_IMAGE` | 1 | Cache decompressed tile images for reuse |
| `GRK_TILE_CACHE_ALL` | 2 | Cache everything (processors + images) |
| `GRK_TILE_CACHE_LRU` | 4 | LRU eviction with compressed chunk cache and re-decompression |

## Tile Lifecycle

### 1. First Encounter (SOT Parsed)

When a tile's SOT (Start of Tile-part) marker is parsed, a `TileProcessor` is
created and its `tilePartSeq_` is populated with byte offsets and lengths of
every tile part in the codestream. This metadata constitutes a partial
TLM-like database — even when the codestream lacks TLM markers.

### 2. Decompression

The T2 (packet parsing) and T1 (wavelet + entropy decoding) stages produce a
`GrkImage` containing the decompressed pixel data. The cache entry is marked
clean (`dirty_ = false`).

### 3. Cache Hit

Subsequent decompress calls for the same tile find a clean entry with a valid
image and return immediately — no reprocessing.

### 4. SOT-Cached Fast Path (Non-TLM)

When decompressing a different tile from the same codestream, the SOTs of
previously encountered tiles are already cached. On a later decode targeting
one of those tiles, `decompressFromCachedTileParts()` seeks directly to each
tile part using the cached byte offsets — no sequential codestream re-walk
required. This is the mechanism described in issue #396.

### 5. LRU Eviction

When `GRK_TILE_CACHE_LRU` is active and `maxActiveTiles` is set, exceeding the
limit evicts the least-recently-used tile's decompressed data via
`release(GRK_TILE_CACHE_LRU)`. The tile processor and its SOT metadata survive
so the tile can be re-decompressed without re-parsing the codestream (issue #398).

### 6. Re-decompression

An evicted tile is re-decompressed by calling `reinitForReDecompress()`, which
recreates the internal `Tile` structure, then runs the T2+T1 pipeline from
cached compressed data. The `CompressedChunkCache` (see below) provides the
compressed tile parts so no network re-fetch is needed (issue #399).

## Compressed Chunk Cache

The `CompressedChunkCache` is a per-file LRU cache that holds compressed tile
part data in memory after it has been fetched from the codestream (local or
network). It serves two purposes:

1. **Avoids network re-fetches** — When a tile is LRU-evicted and later needed
   again, its compressed data is retrieved from this cache rather than
   re-fetching from S3 / cloud storage.

2. **Manages memory with a high water mark** — The cache enforces a configurable
   memory budget (issue #397). When the budget is exceeded, the least-recently-used
   tile's compressed buffers are spilled to disk and freed from memory.

### Memory Budget Configuration

The budget is controlled by environment variables, checked in this order:

| Variable | Default | Description |
|----------|---------|-------------|
| `GRK_CACHEMAX` | 256 MB | Grok-specific cache limit |
| `GDAL_CACHEMAX` | 256 MB | GDAL-compatible cache limit (fallback) |

Values can be specified as:
- Raw bytes: `268435456`
- With suffix: `256M`, `256MB`, `1G`, `1GB`

### Disk Spillover

When the in-memory budget is exceeded, evicted tiles are serialized to a
temporary disk cache (`DiskCache`). On reload, the compressed buffers and
memory streams are recreated transparently. This allows the system to handle
codestreams much larger than available RAM.

## Configuration Summary

| Setting | How to Set | Description |
|---------|-----------|-------------|
| Cache strategy | `grk_decompress_set_params()` → `tile_cache_strategy` | Bitmask of `GRK_TILE_CACHE_*` constants |
| Max active tiles | `grk_decompress_set_params()` → `max_active_tiles` | LRU limit (0 = unlimited) |
| Memory budget | `GRK_CACHEMAX` or `GDAL_CACHEMAX` env var | Compressed chunk cache size |

## Example: Multi-Region Decompress with LRU

```c
grk_decompress_parameters params;
grk_decompress_default_parameters(&params);
params.tile_cache_strategy = GRK_TILE_CACHE_IMAGE | GRK_TILE_CACHE_LRU;
params.max_active_tiles = 4;  // Keep at most 4 tiles decompressed

grk_codec* codec = grk_decompress_init(&stream, &params);

// Decompress tiles in any order — previously parsed SOT metadata is reused,
// evicted tiles are re-decompressed from cached compressed data.
grk_decompress_tile(codec, 10, &image);
grk_decompress_tile(codec, 3, &image);   // SOT fast path — no re-walk
grk_decompress_tile(codec, 10, &image);  // Cache hit — instant return
grk_decompress_tile(codec, 50, &image);  // May evict LRU tile
```
