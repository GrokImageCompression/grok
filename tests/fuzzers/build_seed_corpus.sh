#!/bin/bash

set -e

if [ "$OUT" == "" ]; then
    echo "OUT env var not defined"
    exit 1
fi

SRC_DIR=$(dirname $0)/../..

CONF_DIR=$SRC_DIR/data/input/conformance
NR_DIR=$SRC_DIR/data/input/nonregression

rm -f $OUT/grk_decompress_fuzzer_seed_corpus.zip
zip $OUT/grk_decompress_fuzzer_seed_corpus.zip $CONF_DIR/*.jp2 $CONF_DIR/*.j2k $NR_DIR/*.jp2 $NR_DIR/*.j2k 
cd $OLDPWD
