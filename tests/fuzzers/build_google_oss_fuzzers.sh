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
    $CXX $CXXFLAGS -std=c++11 -I$SRC/grok/src/lib/jp2 -I$SRC/grok/build/src/lib/jp2 \
        $sourceFilename $* -o $OUT/$fuzzerName \
        $LIB_FUZZING_ENGINE $SRC/grok/build/bin/libgrok.a -lm -lpthread
}

fuzzerFiles=$(dirname $0)/*.cpp
for F in $fuzzerFiles; do
    fuzzerName=$(basename $F .cpp)
    build_fuzzer $fuzzerName $F
done

