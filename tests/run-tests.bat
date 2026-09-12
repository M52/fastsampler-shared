@echo off
setlocal
if defined VSCMD_VER goto ready
rem vswhere runs from its own folder: a quoted path with "(x86)" in it breaks
rem the command of a for /f. The .\ prefixes also work where cmd does not
rem search the current folder for programs.
if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" exit /b 1
pushd "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
for /f "usebackq tokens=*" %%i in (`.\vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "FORMATS_VS=%%i"
popd
if not defined FORMATS_VS exit /b 1
call "%FORMATS_VS%\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
:ready
cd /d "%~dp0.."
if not exist build mkdir build
set "FLAGS=/nologo /W4 /wd4100 /O2 /MT /EHsc /std:c++17 /DPB_FIELD_32BIT /D_CRT_SECURE_NO_WARNINGS /I include /I 3rdparty\nanopb"
set "BINDINGS=include\fastsampler.pb.c 3rdparty\nanopb\pb_common.c 3rdparty\nanopb\pb_encode.c 3rdparty\nanopb\pb_decode.c"
cl %FLAGS% tests\formats_test.cpp src\fastsampler_fsb.cpp src\fastsampler_smfp.cpp src\fastsampler_fsp.cpp %BINDINGS% /Fo"build/" /Fe"build/formats_test.exe"
if errorlevel 1 exit /b 1
cl %FLAGS% tests\source_test.cpp src\fastsampler_hash.cpp src\fastsampler_source.cpp %BINDINGS% /Fo"build/" /Fe"build/source_test.exe"
if errorlevel 1 exit /b 1
pushd build
.\formats_test.exe
if errorlevel 1 goto failed
.\source_test.exe
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
