### Distribution Build

#### Linux


##### Build
```
cd $SOURCE_DIR
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release .
cmake --build build -- -j$(nproc)  # Use all cores
cd build
cpack
```

##### Validate

`file libgrokj2k.so.14.2.0`

output should say `stripped`

or

`readelf -S libgrokj2k.so.14.2.0 | grep debug`

(for version 14.2.0)


#### Windows with MSVC (PowerShell)

##### Build

```
cd $SOURCE_DIR
Remove-Item -Path build -Recurse -Force
cmake -B build -DCMAKE_BUILD_TYPE=Release .
cmake --build build --config Release -- /m
cd build
cpack -G 7Z
```

##### Validate


` dumpbin /symbols .\build\bin\Release\grokj2k.dll`

should have no .debug section
