@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Preview\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.42
if errorlevel 1 exit /b 1
C:\vcpkg\vcpkg.exe remove abseil:x64-windows-static utf8-range:x64-windows-static protobuf:x64-windows-static --recurse
if errorlevel 1 exit /b 1
C:\vcpkg\vcpkg.exe install protobuf:x64-windows-static --editable --recurse
exit /b %ERRORLEVEL%
