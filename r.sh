make clean
make -j$(nproc)
./RobusText Inter_18pt-Regular.ttf testdata/combining_10k.txt
