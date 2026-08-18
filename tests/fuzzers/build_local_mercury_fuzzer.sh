#!/usr/bin/env bash
#
# build the mercury decompress fuzzer locally with clang against the shared libgrokj2k
#
# usage: build_local_mercury_fuzzer.sh [build_dir] [output_binary]
#   build_dir     defaults to <repo_root>/build
#   output_binary defaults to <build_dir>/bin/grk_decompress_mercury_fuzzer

set -e

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"

build_dir="${1:-${repo_root}/build}"
output_binary="${2:-${build_dir}/bin/grk_decompress_mercury_fuzzer}"

mkdir -p "$(dirname "${output_binary}")"

clang++ -std=c++20 -g -O1 -fsanitize=fuzzer,address \
  -I "${repo_root}/src/lib/core" \
  -I "${build_dir}/src/lib/core" \
  "${repo_root}/tests/fuzzers/grk_decompress_mercury_fuzzer.cpp" \
  -o "${output_binary}" \
  -L "${build_dir}/bin" -lgrokj2k -Wl,-rpath,"${build_dir}/bin"

echo "built ${output_binary}"
echo "run it with a corpus dir, for example:"
echo "  mkdir -p /tmp/grk_mercury_corpus"
echo "  cp <grok-test-data>/input/nonregression/*.j2k /tmp/grk_mercury_corpus/"
echo "  ${output_binary} -max_total_time=60 -rss_limit_mb=4096 /tmp/grk_mercury_corpus"
