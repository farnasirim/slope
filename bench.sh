#!/bin/bash

SLOPE_DIR=/home/farnasirim/workspace/shared/slope
BENCH_RESULT_DIR=bench-results

SLOPE_CMDLINE_OPTIONS=$@
bench_dir_name=$(date | sed 's/ /-/g')

build() {
    MACHINE=0
    ADDR=forwarded-mel-25
    ssh $ADDR "cd '$SLOPE_DIR' ; make -j30"
}

zero() {
    MACHINE=0
    ADDR=forwarded-mel-25
    ssh $ADDR "cd '$SLOPE_DIR' ; mkdir -p '$BENCH_RESULT_DIR/$bench_dir_name' ; ID=$MACHINE SLOPE_CMDLINE_OPTIONS='$SLOPE_CMDLINE_OPTIONS' ./run.sh 1>$BENCH_RESULT_DIR/$bench_dir_name/$MACHINE"
    scp $ADDR:$SLOPE_DIR/$BENCH_RESULT_DIR/$bench_dir_name/$MACHINE $BENCH_RESULT_DIR/$bench_dir_name
}

one() {
    MACHINE=1
    ADDR=forwarded-mel-26
    ssh $ADDR "cd '$SLOPE_DIR' ; mkdir -p '$BENCH_RESULT_DIR/$bench_dir_name' ; ID=$MACHINE SLOPE_CMDLINE_OPTIONS='$SLOPE_CMDLINE_OPTIONS' ./run.sh 1>$BENCH_RESULT_DIR/$bench_dir_name/$MACHINE"
    scp $ADDR:$SLOPE_DIR/$BENCH_RESULT_DIR/$bench_dir_name/$MACHINE $BENCH_RESULT_DIR/$bench_dir_name
}

mkdir -p $BENCH_RESULT_DIR/$bench_dir_name
build
zero &
sleep 1
one

echo "$SLOPE_CMDLINE_OPTIONS" > $BENCH_RESULT_DIR/$bench_dir_name/cmdline_options
echo "$(git rev-parse HEAD)" > $BENCH_RESULT_DIR/$bench_dir_name/commit
echo "$(git diff)" > $BENCH_RESULT_DIR/$bench_dir_name/patch
echo "$(git status)" > $BENCH_RESULT_DIR/$bench_dir_name/status
