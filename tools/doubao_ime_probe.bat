@echo off
setlocal

set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo Visual Studio Build Tools not found: %VCVARS%
    exit /b 1
)

call "%VCVARS%"
cd /d "%~dp0\.."
if not exist build\artifacts\tools mkdir build\artifacts\tools

set "OPUS_INCLUDE=%CD%\third_party\opus\include"
set "OPUS_LIB=%CD%\third_party\opus\lib"

rem Tool output stays under build\artifacts (the only whitelisted place for
rem diagnostics); build\cmake and build\run stay owned by CMake, and any other
rem top-level item makes scripts\validate_build_layout.ps1 fail the next build.
cl /nologo /O2 /EHsc /MT /std:c++17 /utf-8 /DUNICODE /D_UNICODE /Isrc\asr /Isrc\core /Isrc\audio /Isrc\platform /Isrc\app /I"%OPUS_INCLUDE%" /Fobuild\artifacts\tools\ /Fe:build\artifacts\tools\doubao_ime_probe.exe tools\doubao_ime_probe.cpp src\asr\doubao_ime_asr.cpp src\audio\audio_diagnostics.cpp winhttp.lib bcrypt.lib crypt32.lib ole32.lib shell32.lib "%OPUS_LIB%\opus.lib"
if errorlevel 1 exit /b 1

build\artifacts\tools\doubao_ime_probe.exe %*
exit /b %ERRORLEVEL%
