@echo off
echo ========================================
echo Environment Check
echo ========================================
echo.

echo [1] Checking CMake...
where cmake >nul 2>&1
if errorlevel 1 (
    echo   X CMake not found
    echo   Install: scoop install cmake
) else (
    echo   OK CMake found
    cmake --version | findstr "cmake"
)
echo.

echo [2] Checking TDM-GCC...
if exist "D:\env\Scoop\apps\tdm-gcc\10.3.0\bin\gcc.exe" (
    echo   OK TDM-GCC found
    "D:\env\Scoop\apps\tdm-gcc\10.3.0\bin\gcc.exe" --version | findstr "gcc"
) else (
    echo   X TDM-GCC not found
    echo   Install: scoop install tdm-gcc
)
echo.

echo [3] Checking project files...
if exist "src\main.cpp" (
    echo   OK Source files exist
) else (
    echo   X Source files missing
)
if exist "CMakeLists.txt" (
    echo   OK CMakeLists.txt exists
) else (
    echo   X CMakeLists.txt missing
)
echo.

echo [4] Checking build directory...
if exist "build" (
    echo   OK build directory exists
) else (
    echo   X build directory not found
)
echo.

echo ========================================
echo Check Complete
echo ========================================
echo.
echo If all checks pass, run: build.bat
echo.