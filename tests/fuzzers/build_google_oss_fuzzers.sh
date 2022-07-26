#!/bin/bash

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


build_fuzzer()
{
    fuzzerName=$1
    sourceFilename=$2
    shift
    shift
    echo "Building fuzzer $fuzzerName"
    $CXX $CXXFLAGS -std=c++20 -I$SRC/grok/src/lib/core -I$SRC/grok/build/src/lib/core \
        $sourceFilename $* -o $OUT/$fuzzerName \
        $LIB_FUZZING_ENGINE $SRC/grok/build/bin/libgrokj2k.a $SRC/grok/build/bin/libhwy.a $SRC/grok/build/bin/liblcms2.a -lm -lpthread
}

fuzzerFiles=$(dirname $0)/*.cpp
for F in $fuzzerFiles; do
    fuzzerName=$(basename $F .cpp)
    build_fuzzer $fuzzerName $F
done

