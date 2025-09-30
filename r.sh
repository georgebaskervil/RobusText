#!/usr/bin/env bash
make clean
make -j$(sysctl -n hw.ncpu)
./RobusText Inter_18pt-Regular.ttf testdata/combining_10k.txt
