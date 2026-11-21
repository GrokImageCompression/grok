# Testing

Grok has a comprehensive test suite covering conformance, non-regression, shared memory IPC,
Python bindings, GPU acceleration, and fuzz testing.

## Enabling Tests

Configure `cmake` with testing enabled:

    $ cmake /PATH/TO/SOURCE -DBUILD_TESTING=ON -DGRK_DATA_ROOT:PATH='/PATH/TO/DATA/DIRECTORY'
    $ make -j$(nproc)

JPEG 2000 test files can be cloned
[here](https://github.com/GrokImageCompression/grok-test-data.git).
If the `-DGRK_DATA_ROOT:PATH` option is omitted,
test files will be automatically searched for in
`${CMAKE_SOURCE_DIR}/../grok-test-data`.

## Running All Tests

    $ cd /PATH/TO/BUILD
    $ ctest

Or with verbose output:

    $ ctest -V

Or with memory checking (Valgrind):

    $ ctest -D NightlyMemCheck

## Unit Tests

Unit tests for internal components (LRU cache, JP2 metadata, messenger protocol)
run automatically via `ctest`:

    $ ctest -R grk_lru_cache_test -V
    $ ctest -R grk_jp2_metadata_test -V
    $ ctest -R grk_messenger_test -V
    $ ctest -R grk_messenger_loopback -V

## Python Tests

Python tests are automatically enabled when `BUILD_TESTING=ON` and SWIG bindings are built
(`GRK_BUILD_CORE_SWIG_BINDINGS=ON`). They require [pytest](https://docs.pytest.org/) to be installed.

To run the Python tests via `ctest`:

    $ cd /PATH/TO/BUILD
    $ ctest -R python_tests -V

To run them directly with `pytest`:

    $ cd /PATH/TO/BUILD/bin
    $ PYTHONPATH=/PATH/TO/BUILD/bin python -m pytest /PATH/TO/SOURCE/tests/python -v --tb=short

To run a single test file:

    $ cd /PATH/TO/BUILD/bin
    $ PYTHONPATH=/PATH/TO/BUILD/bin python -m pytest /PATH/TO/SOURCE/tests/python/test_roundtrip.py -v

Note: if the build has address sanitizer enabled (`-DWITH_SANITIZER=Address`),
Python tests will fail because the ASAN runtime must be loaded before Python.
Either rebuild without ASAN (`-DWITH_SANITIZER=OFF`) or preload the runtime:

    $ LD_PRELOAD=$(gcc -print-file-name=libasan.so) PYTHONPATH=/PATH/TO/BUILD/bin python -m pytest ...

## GPU Plugin Tests

GPU tests exercise the GPU plugin via `grk_compress` and `grk_decompress` binaries.
They require a CUDA-capable GPU with the plugin built. Tests are disabled by default.

To run GPU tests via `ctest`:

    $ cd /PATH/TO/BUILD
    $ ctest -R python_gpu_tests -V

Or filter by the `gpu` label:

    $ cd /PATH/TO/BUILD
    $ ctest -L gpu -V

To run them directly with `pytest`:

    $ cd /PATH/TO/SOURCE
    $ PYTHONPATH=/PATH/TO/BUILD/bin LD_LIBRARY_PATH=/PATH/TO/BUILD/bin \
      python -m pytest tests/python/test_gpu.py --run-gpu -v

All GPU compress operations use `-b 32,32` code block size (required for GPU decompress).
The `CUDA_MODULE_LOADING=EAGER` environment variable is set automatically by the CTest target.

Tests cover: single/batch compress and decompress, lossy/lossless, bit depths 8/10/12/16,
mono and RGB, cinema 2K profile, and CPU-only fallback (`-G -2`).

## S3 / MinIO Integration Tests

S3 tests verify decompression from S3-compatible object storage (MinIO).
They are disabled by default and require a running MinIO instance on `localhost:9000`.

To run S3 tests with `pytest`:

    $ cd /PATH/TO/BUILD/bin
    $ PYTHONPATH=/PATH/TO/BUILD/bin python -m pytest /PATH/TO/SOURCE/tests/python/test_s3.py --run-s3 -v

Or run only S3-marked tests:

    $ PYTHONPATH=/PATH/TO/BUILD/bin python -m pytest /PATH/TO/SOURCE/tests/python --run-s3 -m s3 -v

Environment variables for MinIO configuration:

| Variable             | Default        | Description                    |
|----------------------|----------------|--------------------------------|
| `MINIO_ENDPOINT`     | `localhost:9000` | MinIO host and port          |
| `AWS_ACCESS_KEY_ID`  | `minioadmin`   | MinIO access key               |
| `AWS_SECRET_ACCESS_KEY` | `minioadmin` | MinIO secret key              |
| `MINIO_SECURE`       | `true`         | Use HTTPS (`true`) or HTTP (`false`) |

See [doc/S3.md](doc/S3.md) for full S3/cloud storage documentation and
[network/README.md](network/README.md) for building and running the MinIO Docker container.

## Shared Memory Batch Tests

The SHM batch tests verify the shared-memory inter-process protocol used for batch
compression and decompression. Each test spawns the codec binary as a child process and
communicates via POSIX shared memory and semaphores.

To run SHM batch tests via `ctest`:

    $ cd /PATH/TO/BUILD
    $ ctest -R grk_shm_batch -V

Individual tests:

    $ ctest -R grk_shm_batch_compress -V
    $ ctest -R grk_shm_batch_decompress -V

Or run the binaries directly:

    $ ./bin/grk_shm_batch_compress ./bin/grk_compress
    $ ./bin/grk_shm_batch_decompress ./bin/grk_decompress

Each test compresses/decompresses 4 frames (64×64, 3-channel, 12-bit) through the SHM
protocol and validates pixel-perfect round-trip fidelity.

Note: these two tests share POSIX named semaphores and SHM segments, so they must not
run in parallel. The CTest configuration uses `RESOURCE_LOCK` to enforce this automatically.

## Conformance and Non-Regression Tests

Conformance and non-regression tests validate codec correctness against the JPEG 2000
standard test suite and a corpus of known-good files. They require the test data
repository (see [Enabling Tests](#enabling-tests) above).

    $ ctest -R conformance -V
    $ ctest -R nonregression -V

## Fuzz Testing

Grok is continuously fuzzed via [OSS-Fuzz](https://issues.oss-fuzz.com/issues?q=proj:grok).
For local fuzzing instructions, see [tests/fuzzers/README.md](tests/fuzzers/README.md).
