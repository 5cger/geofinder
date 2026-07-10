@echo off
setlocal
set PATH=C:\Program Files\CMake\bin;C:\msys64\mingw64\bin;C:\msys64\usr\bin;C:\WINDOWS\system32;C:\WINDOWS
set MSYSTEM=MINGW64
cd /d D:\project\neofinder

echo === GeoFinder Build (static) ===

if not exist build\CMakeCache.txt (
    cmake -B build -S . -G "MinGW Makefiles" ^
        -DCMAKE_BUILD_TYPE=Release ^
        2>&1
    if %ERRORLEVEL% NEQ 0 (echo CMake failed & exit /b 1)
)

cmake --build build -j4 2>&1
if %ERRORLEVEL% NEQ 0 (echo Build failed & exit /b 1)

echo.
echo ========================================
echo   BUILD SUCCESS
echo   build\bin\geofinder.exe (static, ~8MB)
echo   No DLLs needed - just run it
echo ========================================
