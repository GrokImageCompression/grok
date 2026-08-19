#!/bin/bash

# Copyright (C) 2016-2026 Grok Image Compression Inc.
#
# This source code is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License, version 3,
# as published by the Free Software Foundation.
#
# This source code is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.

set -e

if [ "$SRC" == "" ]; then
    echo "SRC env var not defined"
    exit 1
fi

if [ "$OUT" == "" ]; then
    echo "OUT env var not defined"
    exit 1
fi

if [ "$CXX" == "" ]; then
    echo "CXX env var not defined"
    exit 1
fi


# static links must resolve grokj2k -> mercury -> kernels -> hwy, and
# -ldl is for rust's std. CARGO_BUILD_TARGET adds a triple subdir.
mercuryLib="$SRC/grok/mercury/target-grok/${CARGO_BUILD_TARGET:+$CARGO_BUILD_TARGET/}release/libmercury.a"
mercuryLibs=""
if [ -f "$mercuryLib" ]; then
    mercuryLibs="$mercuryLib $SRC/grok/build/bin/libmercury_kernels.a"
fi

build_fuzzer()
{
    fuzzerName=$1
    sourceFilename=$2
    shift
    shift
    echo "Building fuzzer $fuzzerName"
    $CXX $CXXFLAGS -std=c++20 -I$SRC/grok/src/lib/core -I$SRC/grok/build/src/lib/core \
        $sourceFilename $* -o $OUT/$fuzzerName \
        $LIB_FUZZING_ENGINE $SRC/grok/build/bin/libgrokj2k.a $mercuryLibs $SRC/grok/build/bin/libhwy.a $SRC/grok/build/bin/liblcms2.a -lm -lpthread -ldl
}

fuzzerFiles=$(dirname $0)/*.cpp
for F in $fuzzerFiles; do
    fuzzerName=$(basename $F .cpp)
    build_fuzzer $fuzzerName $F
done

