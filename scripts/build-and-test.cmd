@echo off
rem Build Nil-Solver and run the test suite.  Double-click this file, or run it
rem from any prompt -- it does not need cmake to be on PATH and it does not care
rem what directory you start in.
rem
rem   build-and-test.cmd            release build, full test suite
rem   build-and-test.cmd Debug      debug build
setlocal
title Nil-Solver build and test

rem %ProgramFiles(x86)% contains a close paren, which breaks cmd's parser inside
rem an if(...) block, so capture both here at the top level and use the copies.
set "PF=%ProgramFiles%"
set "PF86=%ProgramFiles(x86)%"

rem Double-clicking starts in the script's own folder; the project root is up one.
cd /d "%~dp0.."

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

call :find_cmake
if not defined CMAKE goto :no_cmake

echo Using    %CMAKE%
echo Config   %CONFIG%
echo Root     %CD%
echo.

echo === Configure ===
"%CMAKE%" -S . -B build -DCMAKE_BUILD_TYPE=%CONFIG%
if errorlevel 1 goto :fail

echo.
echo === Build ===
"%CMAKE%" --build build --config %CONFIG% --parallel
if errorlevel 1 goto :fail

echo.
echo === Test ===
rem --build-config matters on the Visual Studio generator, which is multi-config:
rem without it ctest does not know which build of the tests to run.
"%CTEST%" --test-dir build --build-config %CONFIG% --output-on-failure
if errorlevel 1 goto :fail

echo.
echo === Built ===
dir /b build\bin
echo.
echo All tests passed.
echo.
pause
exit /b 0

rem ---------------------------------------------------------------------------
rem Find cmake.exe: PATH first, then the usual standalone install, then the copy
rem that ships inside Visual Studio.  ctest.exe always lives beside it.
rem ---------------------------------------------------------------------------
:find_cmake
set "CMAKE="
set "CTEST="
for /f "delims=" %%I in ('where cmake 2^>nul') do if not defined CMAKE set "CMAKE=%%I"
if defined CMAKE goto :got_cmake

if exist "%PF%\CMake\bin\cmake.exe" set "CMAKE=%PF%\CMake\bin\cmake.exe"
if defined CMAKE goto :got_cmake

if exist "%PF86%\CMake\bin\cmake.exe" set "CMAKE=%PF86%\CMake\bin\cmake.exe"
if defined CMAKE goto :got_cmake

set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 0
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT exit /b 0
set "VSCMAKE=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if exist "%VSCMAKE%" set "CMAKE=%VSCMAKE%"

:got_cmake
if defined CMAKE for %%I in ("%CMAKE%") do set "CTEST=%%~dpIctest.exe"
exit /b 0

:no_cmake
echo.
echo Could not find cmake.exe.  Any one of these fixes it:
echo.
echo   * Install CMake from https://cmake.org/download/ and tick
echo     "Add CMake to the system PATH" during setup.
echo   * In the Visual Studio Installer, add the "C++ CMake tools for Windows"
echo     component to your Visual Studio installation.
echo   * Run this from the "Developer Command Prompt for VS 2022", which puts
echo     Visual Studio's own cmake on PATH.
echo.
pause
exit /b 1

:fail
echo.
echo *** FAILED (see the output above) ***
echo.
pause
exit /b 1
