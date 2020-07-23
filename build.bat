@echo off

echo Building...

if exist build\ rmdir /S /Q build\
mkdir build\

where /Q cl
if errorlevel 1 (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
)

setlocal

set CommonCFlags=/nologo /MP /std:c++17 /W3 /Febuild\better.exe /Fobuild\ /Fdbuild\ /EHsc ^
    /D UNICODE /D _UNICODE /D IMGUI_IMPL_WIN32_DISABLE_GAMEPAD ^
    /Isrc ^
    /Ilib\imgui ^
    /Ilib\imgui\examples
set CommonLFlags=build\better.res d3d11.lib ws2_32.lib crypt32.lib
set DebugCFlags=/MDd /Zi /D BETTER_DEBUG=1
set DebugLFlags=
set ReleaseCFlags=/MD /O2 /D NDEBUG /D BETTER_DEBUG=0
set ReleaseLFlags=

rc /nologo /fobuild\better.res resources\better.rc
if errorlevel 1 (
    echo Resource compilation failed.
) else (
    echo Resource compilation successful.

    cl src\*.cpp ^
       lib\imgui\*.cpp ^
       lib\imgui\examples\imgui_impl_dx11.cpp ^
       lib\imgui\examples\imgui_impl_win32.cpp ^
       %CommonCFlags% %ReleaseCFlags% ^
       /link %CommonLFlags% %ReleaseLFlags%

    if errorlevel 1 (
        echo Build failed.
    ) else (
        xcopy /Q /Y data build\
        xcopy /Q /Y lib build\
        echo Build successful.
    )
)

