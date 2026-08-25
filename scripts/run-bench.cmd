@echo off
rem Verify the corpus, report timings, and append a row to bench-history.csv so
rem you can see the trend across commits.  Double-click to run.
rem
rem   run-bench.cmd                          time the corpus, log the run
rem   run-bench.cmd "added alpha-beta"       same, with a note on the row
rem
rem Two legs.  The corpus leg is 560 small oracle-verified positions and takes a
rem couple of seconds; it is the correctness net.  The worst-case leg is the
rem three 13-card rows in tests\corpus\large.txt and takes about a minute; it is
rem the only thing here that measures what a user actually waits for.
rem
rem They answer different questions and neither substitutes for the other.  Cost
rem varies by two orders of magnitude WITHIN a hand size, so a mean over easy
rem positions can improve while the deals people complain about get slower.
rem Set NIL_SKIP_WORST=1 to run the corpus leg alone.
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

if "%NIL_SKIP_WORST%"=="1" goto :skip_worst
echo.
echo === Worst case (13 cards, ~163M nodes, under half a minute) ===
rem --cards-only 13 selects the three 13-card rows and nothing else.  Their
rem answers are PINNED FROM THIS SOLVER, not from nil_oracle.py, which cannot
rem reach 13 cards: a mismatch here means something CHANGED, not necessarily
rem that something broke.  Investigate rather than assume either way.
rem
rem Baselines to compare against, deterministic and machine independent:
rem   c13-0000     60,020,405 nodes
rem   c13-0001     71,253,358 nodes   ^<- the hardest deal in the repo
rem   c13-0002     32,230,695 nodes
rem
rem All three run the MAX tie-break, matching the rest of the file.  Worth
rem knowing before you read a win off them: min is the more expensive
rem direction, roughly 2x on these deals, measured on one build with only the
rem flag moved.  These rows are the milder of the two worst cases.  If the
rem application ever solves for bag avoidance, the deals users actually wait
rem on are about twice what this leg reports.
rem
rem They are also all UNBROKEN, and that is not a detail.  Two of the three
rem used to carry broken=1 on a full thirteen-card deal, which validate() now
rem rejects: no card has been played, so no spade has been played.  Clearing
rem it restores the ban on a voluntary spade lead, which prunes hard near the
rem root and took this leg from 290M nodes to 163M.  The old figures measured
rem a game nobody can play.
"%BENCH%" --corpus tests\corpus\large.txt --cards-only 13 --slowest 3 ^
          --history bench-history.csv --note "worst-case 13c %NOTE%"
if errorlevel 1 goto :fail
:skip_worst

echo.
echo === History ===
python tools\bench_history.py 2>nul
if errorlevel 1 py tools\bench_history.py 2>nul

echo.
echo Node counts are deterministic, so they compare across machines and commits.
echo Wall time only compares within one machine and build configuration.
echo.
echo The worst-case rows are single deals, so their node counts are exact rather
echo than averaged -- a change of even 1%% there is real and not sampling noise.
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
