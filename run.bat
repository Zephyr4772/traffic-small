@echo off
rem Starts the simulator server and opens the browser. Extra arguments are passed through,
rem for example:  run.bat --port 9000 --no-open
pushd "%~dp0"
if not exist build\traffic.exe call build.bat || exit /b 1
build\traffic.exe %*
popd
