@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
rc /nologo /fo build\AutoAPO.res AutoAPO.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /utf-8 /W4 main.cpp build\AutoAPO.res /Fo:build\AutoAPO.obj /Fe:build\AutoAPO.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED
