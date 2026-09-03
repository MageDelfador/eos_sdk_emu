@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Preview\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1

set VCToolsVersion=14.44.35207
set PreferredToolArchitecture=x64
set "MSVC_BIN=C:\Program Files\Microsoft Visual Studio\2022\Preview\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64"
set "PATH=%MSVC_BIN%;%PATH%"

set ROOT=%~dp0..
set BUILD=%ROOT%\build

if not exist "%BUILD%\CMakeCache.txt" (
  C:\vcpkg\vcpkg.exe remove abseil:x64-windows-static utf8-range:x64-windows-static protobuf:x64-windows-static --recurse
  if errorlevel 1 exit /b 1
  C:\vcpkg\vcpkg.exe install protobuf:x64-windows-static --editable --recurse --binarysource=clear
  if errorlevel 1 exit /b 1
  if exist "%BUILD%" rmdir /s /q "%BUILD%"
  mkdir "%BUILD%"
  cmake -S "%ROOT%" -B "%BUILD%" -G "Visual Studio 17 2022" -A x64 -T version=14.44.35207 -DX64=ON -DCMAKE_BUILD_TYPE=Release "-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake" "-DVCPKG_TARGET_TRIPLET=x64-windows-static"
  if errorlevel 1 exit /b 1
) else (
  cmake -S "%ROOT%" -B "%BUILD%"
  if errorlevel 1 exit /b 1
)

cmake --build "%BUILD%" --config Release --clean-first
if errorlevel 1 exit /b 1

python -c "import pefile; pe=pefile.PE(r'%BUILD%\\bin\\Release\\EOSSDK-Win64-Shipping.dll'); deps=[e.dll.decode() for e in pe.DIRECTORY_ENTRY_IMPORT]; print('IMPORTS:', ', '.join(sorted(deps)))"
exit /b %ERRORLEVEL%
