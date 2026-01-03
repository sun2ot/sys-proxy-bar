@echo off
setlocal EnableDelayedExpansion
echo ========================================
echo SysProxyBar Build Script
echo ========================================
echo.

REM Auto-detect TDM-GCC
set "TDM_GCC="

REM Check Scoop path
if exist "D:\env\Scoop\apps\tdm-gcc\10.3.0\bin\gcc.exe" (
    set "TDM_GCC=D:\env\Scoop\apps\tdm-gcc\10.3.0\bin"
    goto :found
)

echo ERROR: TDM-GCC not found at default path
echo.
echo Please install TDM-GCC:
echo   scoop install tdm-gcc
echo.
pause
exit /b 1

:found
echo Found TDM-GCC at: %TDM_GCC%
echo.

REM Generate WebUI resource files
echo Generating WebUI resources...
pixi run python generate_resources.py
if errorlevel 1 (
    echo.
    echo Failed to generate resources!
    pause
    exit /b 1
)
echo.

REM Check version
"%TDM_GCC%\gcc.exe" --version | findstr "gcc"
echo.

REM Create build directory
if not exist build mkdir build
cd build

REM Clean old build
if exist CMakeCache.txt del /Q CMakeCache.txt

echo Configuring project...
set "GCC_EXE=%TDM_GCC%\g++.exe"
set "MAKE_EXE=%TDM_GCC%\mingw32-make.exe"
set "WINDRES_EXE=%TDM_GCC%\windres.exe"

REM Convert backslashes to forward slashes for CMake
set "GCC_EXE=!GCC_EXE:\=/!"
set "MAKE_EXE=!MAKE_EXE:\=/!"
set "WINDRES_EXE=!WINDRES_EXE:\=/!"

cmake .. -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER="!GCC_EXE!" -DCMAKE_MAKE_PROGRAM="!MAKE_EXE!" -DCMAKE_RC_COMPILER="!WINDRES_EXE!" -DCMAKE_BUILD_TYPE=Release

if errorlevel 1 (
    echo.
    echo Configuration FAILED!
    cd ..
    pause
    exit /b 1
)

echo.
echo Compiling...
"%TDM_GCC%\mingw32-make.exe"

if errorlevel 1 (
    echo.
    echo Build FAILED!
    cd ..
    pause
    exit /b 1
)

echo.
echo ========================================
echo Build SUCCESS!
echo ========================================
echo.
if exist bin\SysProxyBar.exe (
    echo Output: build\bin\SysProxyBar.exe
    dir bin\SysProxyBar.exe | findstr "SysProxyBar.exe"
)
echo.
echo You can now run SysProxyBar.exe
echo ========================================
echo.
cd ..
endlocal
pause
