cd $HOME/src/grok-test-data
git checkout master
cd ../grok
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
make -j$(nproc)

