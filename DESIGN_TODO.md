# Design TODO

- Fuse the DC shift into the float 9/7 partial final read. The region decode
  canvas holds float bit patterns in int32 storage, so the integer fused sink
  would corrupt them and the float path keeps a standalone DC shift pass. The
  fusion predicate is `fuseDcShift` in DecompressScheduler.cpp. Only worth
  doing if profiling shows the standalone pass costs anything.

- Cut mercury's peak memory on many cores. On 48 cores translate to pixel
  interleaved TIFF peaks at 1.9x classic on B04, 1.5x on PHR1B and 1.4x on
  mosaic, and callback reads at 1.6x on B04 and 1.4x on mosaic. The cause is
  `rowsAhead` in mercury_fastpath.cpp, `max(2, ceil(2 * cores / tile columns))`,
  which is 9 for B04 on 48 cores: forcing 48 tiles in flight with 3 rows ahead
  on 16 cores keeps classic's peak, 9 rows ahead does not. Fewer rows ahead
  may leave cores idle, so test rows ahead 3, 5 and 9 against `slice_blocks`
  16, 4 and 2 on a 48 core machine, time first.

- TCI is still slower than Kakadu on 16 cores through mercury: full read
  1.13x, translate to pixel interleaved TIFF 1.28x, warm region reads 1.17x.

- Cold TCI region reads take 684 ms against Kakadu's 579 ms on 48 cores, but
  358 ms against 758 ms on 16 cores. Compare the read pattern against
  Kakadu's on the slower disk.

- HiRISE on 48 cores is 1.14x Kakadu on a full read and 1.37x through read
  callbacks.

- Mercury's float 9/7 differs from classic by one in about 0.5% of samples on
  12 bit irreversible streams (p1_04, pleiades4x4). The p1_04 nonregression
  baseline is mercury's output, so it fails under `GRK_MERCURY=0`.

- The merge sink copies every row three times before the tile image: into
  `assembled` for the colour transform, through `dye_row` into a scratch row,
  and by `memcpy` in the host row callback. Removing all three gains 1.4% on
  SPOT6 on 48 cores and 2.2% on 16. Writing `dye_row` output straight into
  the host tile image needs a destination row callback in the C API.
