@echo off
rem Build the Windows 8.1 candidate, then pack dist\PulseSetup-<version>-win81.exe.
setlocal
cd /d "%~dp0"
if /i not "%~1"=="/skipbuild" call build_win81.bat /release
if errorlevel 1 exit /b 1
python tools\audit_win81_imports.py build-win81
if errorlevel 1 exit /b 1
call "%~dp0scripts\pack_setup.bat" build-win81 win81
exit /b %errorlevel%
