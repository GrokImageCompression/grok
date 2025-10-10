cd $HOME/src/grok-test-data
git checkout master-debug
cd ../grok
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)

