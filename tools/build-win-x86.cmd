@echo off
rem 中文：使用本机已安装的 Visual C++ 工具链构建 / English: Build with the installed Visual C++ toolchain
setlocal
set "PATH=D:\DevTools\VS2019\BuildTools\VC\Tools\MSVC\14.29.30133\bin\Hostx64\x86;D:\DevTools\VS2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files (x86)\Windows Kits\10\bin\10.0.17763.0\x86;%PATH%"
set "INCLUDE=D:\DevTools\VS2019\BuildTools\VC\Tools\MSVC\14.29.30133\include;C:\Program Files (x86)\Windows Kits\10\Include\10.0.17763.0\shared;C:\Program Files (x86)\Windows Kits\10\Include\10.0.17763.0\um;C:\Program Files (x86)\Windows Kits\10\Include\10.0.17763.0\ucrt"
set "LIB=D:\DevTools\VS2019\BuildTools\VC\Tools\MSVC\14.29.30133\lib\x86;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.17763.0\um\x86;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.17763.0\ucrt\x86"

where cl
if errorlevel 1 exit /b 1
where nmake
if errorlevel 1 exit /b 1

set "ROOT=%~dp0.."
cmake.exe -S "%ROOT%" -B "%ROOT%\build-win" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
cmake.exe --build "%ROOT%\build-win" --parallel
if errorlevel 1 exit /b 1
ctest.exe --test-dir "%ROOT%\build-win" --output-on-failure
exit /b %errorlevel%
