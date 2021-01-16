@echo off

if [%1] == [release] (
    echo Building for release...
) else (
    echo Building for debug...
)

if exist build\ rmdir /S /Q build\
mkdir build\

where /Q cl
if errorlevel 1 (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
)

setlocal

set CommonCFlags=/nologo /MP /std:c++17 /W3 /Febuild\better.exe /Fobuild\ /Fdbuild\ /EHsc ^
    /D UNICODE /D _UNICODE /D IMGUI_IMPL_WIN32_DISABLE_GAMEPAD ^
    /Isrc ^
    /Ilib\imgui ^
    /Ilib\imgui\backends ^
    /Ilib\implot ^
    /Ilib\binn\src
set CommonLFlags=build\better.res d3d11.lib ws2_32.lib crypt32.lib shell32.lib

if [%1] == [release] (
    set TargetCFlags=/MT /O2 /D NDEBUG /D BETTER_DEBUG=0
) else (
    set TargetCFlags=/MTd /Zi /D BETTER_DEBUG=1
)

rc /nologo /fobuild\better.res resources\better.rc
if errorlevel 1 (
    echo Resource compilation failed.
) else (
    echo Resource compilation successful.

    cl src\*.cpp src\*.c ^
       lib\imgui\*.cpp ^
       lib\imgui\backends\imgui_impl_dx11.cpp ^
       lib\imgui\backends\imgui_impl_win32.cpp ^
       lib\implot\implot.cpp ^
       lib\implot\implot_items.cpp ^
       %CommonCFlags% %TargetCFlags% ^
       /link %CommonLFlags%

    if errorlevel 1 (
        echo Build failed.
    ) else (
        xcopy /Q /Y data build\
        echo Build successful.
    )
)

