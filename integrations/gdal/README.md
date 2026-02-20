# GDAL CMake settings


## Adjust these entries (replace FOO with your user name)

```
KDU_ROOT
/home/FOO/src/KDU_DIR

CMAKE_INSTALL_PREFIX
/home/FOO/bin/gdal
```

## Add these entries


```
GDAL_ENABLE_DRIVER_JP2GROK  set to on

CMAKE_PREFIX_PATH
/home/FOO/bin/grok
```

# Install GDAL from source on Windows (with Visual Studio 2026)

1. Install miniconda

2. Open a conda prompt (not PowerShell)

## Install environment

```
conda create -n gdal-src python=3.11 cmake swig numpy pytest -c conda-forge
conda activate gdal-src
conda install -c conda-forge --only-deps libgdal-core
conda install -c conda-forge openjpeg ninja
```

## Clone GDAL

```
mkdir src
cd src
git clone https://github.com/OSGeo/gdal.git
cd gdal
git checkout v3.10.2   # or v3.11.x / main for latest
```

## Build GDAL with Ninja

```
mkdir build
cd build

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64

cmake .. -G Ninja -DCMAKE_INSTALL_PREFIX="%CONDA_PREFIX%" -DPython3_EXECUTABLE="%CONDA_PREFIX%\python.exe" -DGDAL_USE_EXTERNAL_LIBS=ON -DGDAL_BUILD_OPTIONAL_DRIVERS=ON -DOGR_BUILD_OPTIONAL_DRIVERS=ON -DBUILD_TESTING=OFF

cmake --build . --config Release --parallel 16
cmake --install . --config Release
```