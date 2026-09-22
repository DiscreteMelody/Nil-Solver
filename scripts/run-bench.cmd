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
rem A third leg times the two-nil shape on its own 13-card deals.  It is
rem separate because it is a separate objective on a separate tree: a change
rem that helps one nil can easily do nothing for two, and averaging them
rem would hide it.  Set NIL_SKIP_MULTINIL=1 to skip it.
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
rem Baselines to compare against, deterministic and machine independent.
rem Re-banked at patch 105; the figures that used to sit here were
rem 60,020,405 / 71,253,358 / 32,230,695, summing to 163,504,458.  That total
rem EXCEEDS the whole-file figure patch 95 recorded before it raised the table,
rem so these had already drifted when patch 95 re-banked around them -- at
rem least two re-banks stale.  Nothing was wrong with the solver; the numbers
rem simply lived where no re-bank looked.
rem   c13-0000     59,483,222 nodes
rem   c13-0001     70,957,819 nodes   ^<- the hardest deal in the repo
rem   c13-0002     32,058,737 nodes
rem   ---------------------------
rem   total       162,499,778 nodes
rem
rem THE STALENESS MATTERED BY THIS SCRIPT'S OWN STANDARD, which is why the
rem correction is worth more than the 0.6%%.  The closing text below tells the
rem reader a 1%% move on these rows is real and not noise; the figures above
rem were 0.5-0.9%% high, so a run that changed nothing read as a small win.
rem tools\check_baselines.py is the fix: it holds the numbers in one place and
rem COMPARES them rather than printing them for a human to eyeball.
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

rem THIS LEG HAD NEVER RUN.  It used to sit below `exit /b 0` and below the
rem :not_built and :fail labels, so control could not reach it -- the .sh has
rem been running a leg the .cmd silently skipped, on the platform that is the
rem primary one for this project.  Moved here, where the .sh runs it.
if defined NIL_SKIP_MULTINIL goto :multinil_done
if not exist tests\corpus\multinil.txt goto :multinil_done
echo.
echo === Two nils on one side (13 cards, every bound still gated off) ===
rem These six rows carry no recorded answer -- the oracle is exhaustive and
rem cannot reach 13 cards -- so nothing is verified here and the node counts
rem are the whole output.  A change means the tree MOVED, which may be a win
rem or a bug; check corpus_multinil for whether the answers survived it.
"%BENCH%" --corpus tests\corpus\multinil.txt --cards-only 13 --slowest 3
:multinil_done

rem The banked-baseline leg.  Off by default -- it is ~740M nodes and a few
rem minutes -- but it is the only thing here that checks the workloads nobody
rem runs: the three opposed corpora each need a DIFFERENT invocation, and two
rem of them need --seats that no row in the file supplies.  A wrong invocation
rem does not error, it returns a plausible number.  The invocations live in
rem tools\check_baselines.py so they cannot drift between this file and the .sh.
if not "%NIL_RUN_BASELINES%"=="1" goto :baselines_done
echo.
echo === Banked baselines (~740M nodes, a few minutes) ===
rem Resolve the interpreter BEFORE running the check, not after.  A bare
rem `python` that is not on PATH exits 9009, which `if errorlevel 1` cannot
rem tell apart from the checker's own exit 1 -- so a missing interpreter would
rem be reported as a moved baseline.  Probing first also avoids re-running
rem ~740M nodes under a fallback just to find out which failure it was.
rem (Line 87 above already needs this dance for bench_history.py.)
set "NIL_PY=python"
python --version >nul 2>&1
if errorlevel 1 set "NIL_PY=py"
%NIL_PY% tools\check_baselines.py
if errorlevel 1 goto :fail
:baselines_done

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
