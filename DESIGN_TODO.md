# Design TODO

- Fuse the DC shift into the float 9/7 partial final read. The region decode
  canvas holds float bit patterns in int32 storage, so the integer fused sink
  would corrupt them and the float path keeps a standalone DC shift pass. The
  fusion predicate is `fuseDcShift` in DecompressScheduler.cpp. Only worth
  doing if profiling shows the standalone pass costs anything.
