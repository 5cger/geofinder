@echo off
set PATH=C:\Program Files\CMake\bin;C:\msys64\mingw64\bin;C:\msys64\usr\bin;C:\WINDOWS\system32;C:\WINDOWS
set MSYSTEM=MINGW64
cd /d D:\project\neofinder
cmake --build build -j4 2>&1
