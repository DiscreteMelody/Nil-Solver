// NilSolverPool.cs -- how to call the solver from a server without leaking memory.
//
// THE PROBLEM THIS SOLVES
// ----------------------
// The native transposition table is thread_local.  That is what makes the
// solver safe to call concurrently with no locking and no thread-id argument --
// unlike DDS, there is no scratch slot to collide on and nothing to serialise
// for correctness.  The cost is that the table is allocated per thread and
// stays there: the FIRST solve on a thread allocates 32 MiB (or whatever
// nil_set_table_size last said on that thread) and the memory is held until
// that thread calls nil_release_table or exits.
//
// On the ASP.NET thread pool that is a slow leak with a respectable-looking
// cause.  Continuations land on whatever pool thread is free, so over a few
// thousand requests the set of threads that have ever run a solve converges on
// the set of pool threads, and each one is holding a table.  Fifty threads at
// 32 MiB is 1.6 GB of transposition table for a workload that never has more
// than a handful of solves in flight.  Bounding *concurrency* with a semaphore
// -- the shape the DDS wrapper uses -- does not fix it, because the semaphore
// bounds how many run at once and not which threads they run on.
//
// So: a fixed set of long-lived threads that are the only threads that ever
// call the solver.  Exactly `workers` tables exist, for as long as the pool
// does, and the number is one you chose rather than one the runtime picked.
// Callers await a Task as usual and their continuations go back to the pool.
//
// This also gives you the thing the semaphore was for, for free: work queues
// rather than piling every concurrent request onto the CPU at once.

using System;
using System.Collections.Concurrent;
using System.Threading;
using System.Threading.Tasks;

namespace NilSolver
{
    /// <summary>
    /// Runs solves on a fixed set of dedicated threads. Register one of these as
    /// a singleton; it is safe to call from anywhere.
    /// </summary>
    public sealed class NilSolverPool : IDisposable
    {
        private readonly BlockingCollection<Action> _queue = new BlockingCollection<Action>();
        private readonly Thread[] _workers;
        private int _disposed;

        /// <param name="workers">
        /// How many solves may run at once, and how many transposition tables
        /// exist. Solving is CPU-bound and single-threaded, so this is a share of
        /// the machine: on a box also serving web requests, half the cores is a
        /// reasonable starting point rather than all of them.
        /// </param>
        /// <param name="tableMegabytes">
        /// Table size per worker. The default of 32 is the solver's own default.
        /// Bigger helps deep positions and does nothing for shallow ones; total
        /// resident memory is this times <paramref name="workers"/>.
        /// </param>
        public NilSolverPool(int workers = 0, uint tableMegabytes = 32)
        {
            if (workers <= 0) workers = Math.Max(1, Environment.ProcessorCount / 2);

            _workers = new Thread[workers];
            for (int i = 0; i < workers; i++)
            {
                var t = new Thread(() => WorkerLoop(tableMegabytes))
                {
                    IsBackground = true,
                    Name = $"nil-solver-{i}"
                };
                _workers[i] = t;
                t.Start();
            }
        }

        private void WorkerLoop(uint tableMegabytes)
        {
            // Per-thread setting, so it has to be said on this thread, once.
            Nil.SetTableSize(tableMegabytes);
            try
            {
                foreach (var job in _queue.GetConsumingEnumerable()) job();
            }
            finally
            {
                // Hand the table back rather than relying on thread teardown to do
                // it, which is not guaranteed to run for a background thread.
                Nil.ReleaseTable();
            }
        }

        /// <summary>Queue any solver call and await its result.</summary>
        public Task<T> RunAsync<T>(Func<T> job, CancellationToken cancellationToken = default)
        {
            if (job == null) throw new ArgumentNullException(nameof(job));

            // Asynchronous continuations, so an awaiting caller's code does not
            // run on -- and block -- a solver thread.
            var tcs = new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);

            try
            {
                _queue.Add(() =>
                {
                    // Cancellation is honoured up to the moment a worker picks the
                    // job up. There is no way to interrupt a search in progress,
                    // and pretending otherwise would be worse than saying so.
                    if (cancellationToken.IsCancellationRequested)
                    {
                        tcs.TrySetCanceled(cancellationToken);
                        return;
                    }
                    try { tcs.TrySetResult(job()); }
                    catch (Exception ex) { tcs.TrySetException(ex); }
                }, cancellationToken);
            }
            catch (Exception ex)
            {
                // ObjectDisposedException after Dispose, OperationCanceledException
                // if the token fired while queueing.
                tcs.TrySetException(ex);
            }

            return tcs.Task;
        }

        /// <summary>"Can this nil still be broken?" -- the boolean, on a solver thread.</summary>
        public Task<NilSolution> CanBeBrokenAsync(string pbn, NilSeat leader, string? currentTrick,
                                                  NilSeat nilSeat, NilFlags flags = NilFlags.None,
                                                  CancellationToken cancellationToken = default)
        {
            return RunAsync(() => Nil.CanBeBroken(pbn, leader, currentTrick, nilSeat, flags), cancellationToken);
        }

        /// <summary>The full lexicographic answer, on a solver thread.</summary>
        public Task<NilSolution> SolveFullAsync(string pbn, NilSeat leader, string? currentTrick,
                                                NilSeat nilSeat, NilFlags flags = NilFlags.None,
                                                CancellationToken cancellationToken = default)
        {
            return RunAsync(() => Nil.SolveFull(pbn, leader, currentTrick, nilSeat, flags), cancellationToken);
        }

        /// <summary>The full answer plus the principal variation, on a solver thread.</summary>
        public Task<NilSolution> SolveWithLineAsync(string pbn, NilSeat leader, string? currentTrick,
                                                    NilSeat nilSeat, NilFlags flags = NilFlags.None,
                                                    CancellationToken cancellationToken = default)
        {
            return RunAsync(() => Nil.SolveWithLine(pbn, leader, currentTrick, nilSeat, flags), cancellationToken);
        }

        /// <summary>Every legal card and whether the nil survives it, on a solver thread.</summary>
        public Task<NilSolution> ScoreMovesAsync(string pbn, NilSeat leader, string? currentTrick,
                                                 NilSeat nilSeat, NilFlags flags = NilFlags.None,
                                                 CancellationToken cancellationToken = default)
        {
            return RunAsync(() => Nil.ScoreMoves(pbn, leader, currentTrick, nilSeat, flags), cancellationToken);
        }

        /// <summary>As ScoreMovesAsync, with each card's trick counts too.</summary>
        public Task<NilSolution> ScoreMovesFullAsync(string pbn, NilSeat leader, string? currentTrick,
                                                     NilSeat nilSeat, NilFlags flags = NilFlags.None,
                                                     CancellationToken cancellationToken = default)
        {
            return RunAsync(() => Nil.ScoreMovesFull(pbn, leader, currentTrick, nilSeat, flags), cancellationToken);
        }

        public void Dispose()
        {
            if (Interlocked.Exchange(ref _disposed, 1) != 0) return;

            _queue.CompleteAdding();
            foreach (var t in _workers) t.Join(TimeSpan.FromSeconds(30));
            _queue.Dispose();
        }
    }
}
