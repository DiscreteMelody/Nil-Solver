@echo off
rem Verify the corpus and report timings.  Double-click to run.
rem
rem   run-bench.cmd                     time the whole corpus
rem   run-bench.cmd before.csv          also compare against an earlier run
rem
rem To capture a baseline before changing the search:
rem   build\bin\nil_bench --corpus tests\corpus\positions.txt --csv before.csv
setlocal
title Nil-Solver benchmark

cd /d "%~dp0.."

set "BENCH=build\bin\nil_bench.exe"
if not exist "%BENCH%" goto :not_built

set "BASELINE=%~1"
if "%BASELINE%"=="" goto :plain

echo Comparing against %BASELINE%
echo.
"%BENCH%" --corpus tests\corpus\positions.txt --repeat 3 --baseline "%BASELINE%"
goto :done

:plain
"%BENCH%" --corpus tests\corpus\positions.txt --repeat 3

:done
echo.
echo Node counts are deterministic, so they are the number to compare across
echo commits.  Wall time drifts with whatever else the machine is doing.
echo.
pause
exit /b 0

:not_built
echo.
echo %BENCH% does not exist yet -- run scripts\build-and-test.cmd first.
echo.
pause
exit /b 1
