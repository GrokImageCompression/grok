cd $HOME/src/grok-test-data
git checkout master
cd $HOME/src/grok
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=-RelWithDebInfo -DGRK_ENABLE_LIBCURL=1
make install -j$(nproc)
cd $HOME/src/gdal
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
make install -j$(nproc)

