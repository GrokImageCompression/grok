# Cinema 4K POC Marker Handling

## Overview

DCI Cinema 4K (`--cinema-4k 24`) requires a Progression Order Change (POC)
marker in the JPEG 2000 codestream. This splits the image into two progression
ranges for bandwidth compliance:

- **POC entry 0**: Resolutions 0 to `numresolutions-2`, CPRL order
- **POC entry 1**: Resolution `numresolutions-1` to `numresolutions`, CPRL order

## Bug Fix: POC Decode Offset (commit 4a3127f)

### Symptom

```
finalizePocs: invalid POC end resolution 0
```

This error appeared during Cinema 4K decode even though the POC marker in the
codestream was valid (e.g., res_e=6 and res_e=7 with numresolutions=7).

### Root Cause

In `TileCodingParams::readPoc()` (CodingParams.cpp), when parsing the main
header (tilePartIndex == -1), POC entries were stored starting at index
`numpocs_ + 1`:

```cpp
uint32_t oldNum = numpocs_ + 1;  // = 1 when numpocs_=0
progressionOrderChange_[oldNum + i] = newPocs[i];  // stored at [1], [2]
numpocs_ = oldNum + currentNumProgressions - 1;    // = 2
```

This left `progressionOrderChange_[0]` uninitialized (all zeros). But both
`finalizePocs()` and `PacketIter` (the decompress packet iterator) assume
entries are contiguous starting at index 0:

- `finalizePocs` validated [0] (zeros → error)
- `PacketIter` used `numProgressions = numpocs_ + 1 = 3`, reading [0],[1],[2]
  where [0] was garbage

Meanwhile, the **encode** path stores entries starting at index 0:
```cpp
// CodeStreamCompress.cpp
tcp->progressionOrderChange_[numTileProgressions] = parameters->progression[i];
tcp->numpocs_ = numTileProgressions - 1;  // e.g., 2 entries → numpocs_=1
```

### Fix

Two changes in `CodingParams.cpp`:

1. **readPoc**: Start at index 0 when first POC marker is read:
   ```cpp
   uint32_t oldNum = (numpocs_ == 0) ? 0 : numpocs_ + 1;
   ```

2. **finalizePocs**: Validate all entries [0..numpocs_], with guard:
   ```cpp
   if(numpocs_ > 0)
   for(uint32_t i = 0; i <= numpocs_; ++i) { ... }
   ```

### Invariant

After fix, the POC entry layout is consistent between encode and decode:
- Entries at indices `[0]` through `[numpocs_]`
- Total progressions = `numpocs_ + 1`
- `hasPoc()` returns `numpocs_ > 0`

### Validation

- Cinema 4K encode → decode round-trip: no errors
- Full test suite: 1717/1729 pass (12 pre-existing failures unrelated to POC)
- `grk_dump -i file.j2k` on Cinema 4K files: no POC validation errors
