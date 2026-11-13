@echo off
rem Builds the simulator into build\traffic.exe using CMake + Ninja (both ship with WinLibs GCC).
setlocal
pushd "%~dp0"

where g++ >nul 2>nul
if errorlevel 1 set "PATH=%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;%PATH%"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || goto :fail
cmake --build build || goto :fail

echo.
echo Built build\traffic.exe   -   start it with run.bat
popd
exit /b 0

:fail
echo.
echo Build failed.
popd
exit /b 1
