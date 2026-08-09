@echo off
rem Verify the corpus, report timings, and append a row to bench-history.csv so
rem you can see the trend across commits.  Double-click to run.
rem
rem   run-bench.cmd                          time the corpus, log the run
rem   run-bench.cmd "added alpha-beta"       same, with a note on the row
rem
rem To see the history afterwards:   python tools\bench_history.py
setlocal
title Nil-Solver benchmark

cd /d "%~dp0.."

set "BENCH=build\bin\nil_bench.exe"
if not exist "%BENCH%" goto :not_built

set "NOTE=%~1"

echo === Benchmark ===
"%BENCH%" --corpus tests\corpus\positions.txt --repeat 3 ^
          --history bench-history.csv --note "%NOTE%"
if errorlevel 1 goto :fail

echo.
echo === History ===
python tools\bench_history.py 2>nul
if errorlevel 1 py tools\bench_history.py 2>nul

echo.
echo Node counts are deterministic, so they compare across machines and commits.
echo Wall time only compares within one machine and build configuration.
echo.
pause
exit /b 0

:not_built
echo.
echo %BENCH% does not exist yet -- run scripts\build-and-test.cmd first.
echo.
pause
exit /b 1

:fail
echo.
echo *** The benchmark reported a failure (see above) ***
echo.
pause
exit /b 1
