@echo off
setlocal
if defined VSCMD_VER goto ready
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "FORMATS_VS=%%i"
if not defined FORMATS_VS exit /b 1
call "%FORMATS_VS%\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
:ready
cd /d "%~dp0.."
if not exist build mkdir build
cl /nologo /W4 /wd4100 /O2 /MT /EHsc /std:c++17 /DPB_FIELD_32BIT /D_CRT_SECURE_NO_WARNINGS /I include /I 3rdparty\nanopb tests\formats_test.cpp src\fastsampler_fsb.cpp src\fastsampler_smfp.cpp src\fastsampler_fsp.cpp include\fastsampler.pb.c 3rdparty\nanopb\pb_common.c 3rdparty\nanopb\pb_encode.c 3rdparty\nanopb\pb_decode.c /Fo"build/" /Fe"build/formats_test.exe"
if errorlevel 1 exit /b 1
pushd build
formats_test.exe
set RESULT=%ERRORLEVEL%
popd
exit /b %RESULT%
