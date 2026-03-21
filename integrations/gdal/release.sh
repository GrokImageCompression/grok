cd $HOME/src/grok-test-data
git checkout master
cd $HOME/src/grok
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DGRK_ENABLE_LIBCURL=1
make -j$(nproc) && make install -j$(nproc)
cd $HOME/src/gdal
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
make install -j$(nproc)
