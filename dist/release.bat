REM Copyright (C) 2016-2025 Grok Image Compression Inc.
REM
REM This source code is free software: you can redistribute it and/or modify
REM it under the terms of the GNU Affero General Public License, version 3,
REM as published by the Free Software Foundation.
REM
REM This source code is distributed in the hope that it will be useful,
REM but WITHOUT ANY WARRANTY; without even the implied warranty of
REM MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
REM GNU Affero General Public License for more details.
REM
REM You should have received a copy of the GNU Affero General Public License
REM along with this program. If not, see <http://www.gnu.org/licenses/>.


@echo off
cd %USERPROFILE%\src\grok
set VCPKG_ROOT=%USERPROFILE%\src\vcpkg
if exist build rmdir /s /q build
cmake -B build -DGRK_BUILD_LIBCURL=OFF -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release -- /m
cd build
cpack -G 7Z