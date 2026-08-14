// NilSolverNative.cs -- the raw P/Invoke surface of nil_solver.dll.
//
// This file is a one-to-one transcription of include/nil_solver/nil_solver.h and
// nothing else: no policy, no convenience, no game model.  If the header changes,
// this changes with it.  Everything friendlier lives in NilSolver.cs.
//
// Three things here are load-bearing and easy to get wrong:
//
//   * CallingConvention.Cdecl.  NIL_SOLVER_CALL is __cdecl on every platform.
//     Stdcall is the usual default people carry over from other native card
//     libraries, and getting it wrong on x86 corrupts the stack on every call
//     while x64 silently ignores the mistake -- so it works until the day
//     someone builds Win32.
//
//   * nil_solver_version returns a pointer to a static string.  Declaring the
//     return type as `string` makes the marshaller free that pointer with
//     CoTaskMemFree, which corrupts the heap.  It comes back as IntPtr and is
//     read with Marshal.PtrToStringAnsi.
//
//   * NilResult must stay in this field order.  It is LayoutKind.Sequential over
//     five int32 and one uint64, which is 32 bytes with four bytes of padding
//     before Nodes on both x86 and x64.  Reordering the fields to look tidier
//     silently reads the wrong numbers.

using System;
using System.Runtime.InteropServices;
using System.Text;

namespace NilSolver
{
    /// <summary>Seats, clockwise. These are also the indices PBN uses.</summary>
    public enum NilSeat
    {
        North = 0,
        East = 1,
        South = 2,
        West = 3
    }

    /// <summary>
    /// Bitwise options for a solve. See the header for the full argument behind
    /// each one; the summaries here are the short version.
    /// </summary>
    [Flags]
    public enum NilFlags : uint
    {
        None = 0x0u,

        /// <summary>Spades are already broken in the given position.</summary>
        SpadesBroken = 0x1u,

        /// <summary>
        /// Treat a forced spade lead (all-spade hand, spades unbroken) as breaking
        /// spades. Off by default, which is the literal reading of the rule.
        /// </summary>
        BreakOnForcedSpadeLead = 0x2u,

        /// <summary>Disable the transposition table. A diagnostic; orders of magnitude slower.</summary>
        NoMemo = 0x4u,

        /// <summary>
        /// Allow more than <see cref="NilSolverNative.CardLimit"/> cards per hand.
        /// Required for a full thirteen even in fast mode.
        /// </summary>
        ForceLarge = 0x8u,

        /// <summary>Tie-break direction: each pair takes as FEW tricks as it can, not as many.</summary>
        MinimiseOwnTricks = 0x10u,

        /// <summary>
        /// The nil has already been broken in the real game. Drops the primary
        /// objective; NilFails then reads 1 because you said so, not because it
        /// was computed.
        /// </summary>
        NilAlreadySet = 0x20u,

        /// <summary>Generate every legal card rather than one per rank-equivalent class. Diagnostic.</summary>
        NoCollapse = 0x40u,

        /// <summary>
        /// Answer the nil question and nothing else. The three trick counts come
        /// back as <see cref="NilSolverNative.TricksUnknown"/> and there is no
        /// principal variation.
        /// </summary>
        FastMode = 0x80u,

        /// <summary>Turn off the nil-safe and nil-set static proofs. Diagnostic; fast mode only.</summary>
        NoStaticBounds = 0x100u,

        /// <summary>Try moves in canonical order rather than promising-first. Diagnostic; fast mode only.</summary>
        NoOrdering = 0x200u,

        /// <summary>
        /// Do not narrow the window as a node's own moves come back, which is what makes
        /// the alpha-beta cutoff reachable. Restores the exhaustive minimax full mode ran
        /// before patch 22: same value and same principal variation, roughly six times the
        /// nodes on the corpus and up to seventy times at nine cards. Diagnostic and a
        /// control arm; inert under <see cref="FastMode"/>, whose window is already null.
        /// </summary>
        NoNarrow = 0x400u
    }

    /// <summary>Return codes. <see cref="Ok"/> is success; everything else is negative.</summary>
    public enum NilStatus
    {
        Ok = 0,
        NullArgument = -1,
        Parse = -2,
        IllegalPosition = -3,
        TooManyCards = -4,
        BufferTooSmall = -5,
        Internal = -6,

        /// <summary>The flags asked for something this entry point cannot produce -- today, a PV in fast mode.</summary>
        Unsupported = -7
    }

    /// <summary>
    /// Mirrors the C <c>nil_result</c> struct. Field order and types are fixed by
    /// the ABI; see the file header before touching either.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct NilResult
    {
        /// <summary>
        /// 1 if the opponents can force the nil bidder to take at least one trick
        /// against best defence, 0 if the nil can be held. Always 1 when
        /// <see cref="NilFlags.NilAlreadySet"/> was passed.
        /// </summary>
        public int NilFails;

        /// <summary>
        /// Tricks the nil bidder takes from this position onward.
        /// <see cref="NilSolverNative.TricksUnknown"/> in fast mode.
        /// </summary>
        public int NilTricks;

        /// <summary>Tricks the nil bidder and its covering partner take between them, NilTricks included.</summary>
        public int NilSideTricks;

        /// <summary>Tricks the opposing pair takes. NilSideTricks + OpponentTricks == TricksRemaining.</summary>
        public int OpponentTricks;

        /// <summary>Tricks left to play in the position.</summary>
        public int TricksRemaining;

        /// <summary>Nodes visited by the search, for benchmarking.</summary>
        public ulong Nodes;
    }

    /// <summary>
    /// One legal card at the root, and what playing it leads to. Mirrors the C
    /// <c>nil_move</c> struct; field order and types are fixed by the ABI.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct NilMove
    {
        /// <summary>0 = spades, 1 = hearts, 2 = diamonds, 3 = clubs.</summary>
        public int Suit;

        /// <summary>2..14, ace high.</summary>
        public int Rank;

        /// <summary>
        /// Other legal cards that are this same move under a different name, as a
        /// bitmask with bit r set for rank r — so the king is bit 13, decimal
        /// 8192. Same encoding as DDS's <c>equals</c>, including the part that
        /// trips people up: this card's OWN rank is not set. Zero means the card
        /// stands alone.
        /// </summary>
        public int EqualRanks;

        /// <summary>1 if the nil fails after this card is played. See <see cref="NilMoveScore.NilFails"/>.</summary>
        public int NilFails;

        public int NilTricks;
        public int NilSideTricks;
        public int OpponentTricks;

        /// <summary>1 if this card achieves the position's own value.</summary>
        public int IsBest;
    }

    /// <summary>
    /// The exported functions, exactly as declared. Prefer the wrapper in
    /// NilSolver.cs unless you have a reason not to.
    /// </summary>
    public static class NilSolverNative
    {
        /// <summary>
        /// Resolved as nil_solver.dll on Windows and libnil_solver.so elsewhere --
        /// .NET adds the platform's prefix and suffix, so one name covers both.
        /// </summary>
        public const string Library = "nil_solver";

        /// <summary>Cards per hand the solver attempts without <see cref="NilFlags.ForceLarge"/>.</summary>
        public const int CardLimit = 9;

        /// <summary>
        /// Legal cards a position can offer. Thirteen is the most any hand holds
        /// and the equivalent-card reduction only ever returns fewer, so a buffer
        /// of this size never needs resizing.
        /// </summary>
        public const int MaxMoves = 13;

        /// <summary>
        /// What the trick counts read in fast mode. Deliberately not zero: zero is
        /// a real answer to "how many tricks did the nil bidder take", and a caller
        /// that mistook one for the other would read a failing nil as a made one.
        /// </summary>
        public const int TricksUnknown = -1;

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int nil_solve(
            [MarshalAs(UnmanagedType.LPStr)] string pbn,
            int leader,
            [MarshalAs(UnmanagedType.LPStr)] string currentTrick,
            int nilSeat,
            uint flags,
            out NilResult result,
            StringBuilder errBuf,
            int errLen);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int nil_solve_pv(
            [MarshalAs(UnmanagedType.LPStr)] string pbn,
            int leader,
            [MarshalAs(UnmanagedType.LPStr)] string currentTrick,
            int nilSeat,
            uint flags,
            out NilResult result,
            StringBuilder pvBuf,
            int pvLen,
            StringBuilder errBuf,
            int errLen);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int nil_solve_moves(
            [MarshalAs(UnmanagedType.LPStr)] string pbn,
            int leader,
            [MarshalAs(UnmanagedType.LPStr)] string currentTrick,
            int nilSeat,
            uint flags,
            out NilResult result,
            [Out] NilMove[] moves,
            int movesCap,
            out int movesLen,
            StringBuilder errBuf,
            int errLen);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int nil_fails(
            [MarshalAs(UnmanagedType.LPStr)] string pbn,
            int leader,
            [MarshalAs(UnmanagedType.LPStr)] string currentTrick,
            int nilSeat,
            uint flags);

        /// <summary>
        /// Transposition table size in mebibytes, for subsequent solves ON THE
        /// CALLING THREAD. The table is per-thread and so is this setting, which
        /// is the fact that decides how a server should schedule solves -- see
        /// csharp/README.md.
        /// </summary>
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        public static extern void nil_set_table_size(uint megabytes);

        /// <summary>Release the calling thread's table. The next solve on that thread allocates again.</summary>
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        public static extern void nil_release_table();

        // Returns a pointer to a static string. See the file header for why this
        // is IntPtr and not string.
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr nil_solver_version();

        /// <summary>Version of the loaded native library, e.g. "0.1.0".</summary>
        public static string Version()
        {
            return Marshal.PtrToStringAnsi(nil_solver_version()) ?? string.Empty;
        }
    }
}
