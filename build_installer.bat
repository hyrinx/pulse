@echo off
rem Build Release binaries, then pack dist\PulseSetup-<version>.exe
rem (Pulse setup; see scripts\pack_setup.bat).
setlocal
cd /d "%~dp0"

if /i "%~1"=="/skipbuild" goto after_release_build
call "%~dp0build_release.bat" || exit /b 1
:after_release_build

call "%~dp0scripts\pack_setup.bat" build windows
exit /b %errorlevel%
