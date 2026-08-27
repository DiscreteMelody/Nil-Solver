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
    /// What one seat is doing. Replaced a nil seat plus an already-set flag,
    /// because two scalars can only ever describe ONE nil and real spades puts
    /// two on the table often enough to change the optimal line.
    /// </summary>
    public enum NilSeatRole
    {
        /// <summary>A nil bidder that has not yet taken a trick.</summary>
        Nil = 0,

        /// <summary>
        /// A nil bidder whose nil is already broken. What the retired
        /// NilFlags.NilAlreadySet used to say: the primary objective is dropped
        /// and only the tie-break matters.
        /// </summary>
        NilSet = 1,

        /// <summary>The nil bidder's partner, covering it.</summary>
        Cover = 2,

        /// <summary>A seat on a side with no nil bid.</summary>
        Opponent = 3
    }

    /// <summary>
    /// What all four seats are doing. Held by ABSOLUTE seat, so North is always
    /// North here whatever the deal string says.
    /// </summary>
    /// <remarks>
    /// The native side wants these clockwise from the seat the PBN names, in the
    /// same order as its hands. <see cref="ToPbnOrder"/> does that rotation and
    /// the wrapper in NilSolver.cs calls it for you, so a caller building roles
    /// with <see cref="ForNil"/> or the constructor never thinks about anchors.
    /// Use <see cref="FromPbnOrder"/> only when you already have the values in
    /// the deal string's order.
    ///
    /// WHAT IS ACCEPTED TODAY: exactly one nil, exactly one cover, the cover
    /// across from the nil bidder, and the other two opposing. Anything else
    /// comes back from the solver as <see cref="NilStatus.IllegalPosition"/>,
    /// except a well-formed layout holding two nils, which is
    /// <see cref="NilStatus.Unsupported"/> -- a legal deal this build cannot
    /// answer yet, not a mistake in the call.
    /// </remarks>
    public readonly struct NilSeatRoles : IEquatable<NilSeatRoles>
    {
        private readonly NilSeatRole _north;
        private readonly NilSeatRole _east;
        private readonly NilSeatRole _south;
        private readonly NilSeatRole _west;

        public NilSeatRoles(NilSeatRole north, NilSeatRole east, NilSeatRole south, NilSeatRole west)
        {
            _north = north;
            _east = east;
            _south = south;
            _west = west;
        }

        public NilSeatRole North => _north;
        public NilSeatRole East => _east;
        public NilSeatRole South => _south;
        public NilSeatRole West => _west;

        public NilSeatRole this[NilSeat seat]
        {
            get
            {
                switch (seat)
                {
                    case NilSeat.North: return _north;
                    case NilSeat.East: return _east;
                    case NilSeat.South: return _south;
                    case NilSeat.West: return _west;
                    default: throw new ArgumentOutOfRangeException(nameof(seat));
                }
            }
        }

        /// <summary>
        /// The arrangement a nil seat and an already-set flag used to describe:
        /// that seat bidding, its partner covering, the other pair opposing.
        /// </summary>
        public static NilSeatRoles ForNil(NilSeat nilSeat, bool alreadySet = false)
        {
            var roles = new NilSeatRole[4];
            for (int i = 0; i < 4; i++) roles[i] = NilSeatRole.Opponent;
            roles[(int)nilSeat] = alreadySet ? NilSeatRole.NilSet : NilSeatRole.Nil;
            roles[((int)nilSeat + 2) & 3] = NilSeatRole.Cover;
            return new NilSeatRoles(roles[0], roles[1], roles[2], roles[3]);
        }

        /// <summary>The seat a PBN string is named for; its hands run clockwise from there.</summary>
        public static NilSeat PbnAnchor(string pbn)
        {
            if (pbn == null) throw new ArgumentNullException(nameof(pbn));
            string t = pbn.TrimStart();
            int seat = t.Length >= 2 && t[1] == ':' ? "NESW".IndexOf(char.ToUpperInvariant(t[0])) : -1;
            if (seat < 0)
            {
                throw new ArgumentException(
                    "PBN deal must start with a seat letter and a colon, e.g. 'N:'", nameof(pbn));
            }
            return (NilSeat)seat;
        }

        /// <summary>
        /// Read four roles given CLOCKWISE FROM THE SEAT <paramref name="pbn"/>
        /// NAMES -- the order the native side and the corpus both use.
        /// </summary>
        public static NilSeatRoles FromPbnOrder(string pbn, params NilSeatRole[] clockwiseFromAnchor)
        {
            if (clockwiseFromAnchor == null) throw new ArgumentNullException(nameof(clockwiseFromAnchor));
            if (clockwiseFromAnchor.Length != 4)
            {
                throw new ArgumentException("expected exactly four seat roles",
                                            nameof(clockwiseFromAnchor));
            }
            int anchor = (int)PbnAnchor(pbn);
            var roles = new NilSeatRole[4];
            for (int offset = 0; offset < 4; offset++)
            {
                roles[(anchor + offset) & 3] = clockwiseFromAnchor[offset];
            }
            return new NilSeatRoles(roles[0], roles[1], roles[2], roles[3]);
        }

        /// <summary>The array to hand to the native side, in that deal string's order.</summary>
        public int[] ToPbnOrder(string pbn)
        {
            int anchor = (int)PbnAnchor(pbn);
            var wire = new int[4];
            for (int offset = 0; offset < 4; offset++)
            {
                wire[offset] = (int)this[(NilSeat)((anchor + offset) & 3)];
            }
            return wire;
        }

        public bool IsNil(NilSeat seat)
        {
            NilSeatRole role = this[seat];
            return role == NilSeatRole.Nil || role == NilSeatRole.NilSet;
        }

        /// <summary>Is this seat on the side protecting a nil, rather than attacking one?</summary>
        public bool OnNilSide(NilSeat seat) => this[seat] != NilSeatRole.Opponent;

        /// <summary>The seat holding a nil. Throws if none does.</summary>
        public NilSeat NilSeat
        {
            get
            {
                for (int i = 0; i < 4; i++)
                {
                    if (IsNil((NilSeat)i)) return (NilSeat)i;
                }
                throw new InvalidOperationException("no seat bid nil (" + Describe() + ")");
            }
        }

        /// <summary>The seat covering the nil. Throws if none does.</summary>
        public NilSeat CoverSeat
        {
            get
            {
                for (int i = 0; i < 4; i++)
                {
                    if (this[(NilSeat)i] == NilSeatRole.Cover) return (NilSeat)i;
                }
                throw new InvalidOperationException("no cover partner (" + Describe() + ")");
            }
        }

        public bool NilAlreadySet
        {
            get
            {
                for (int i = 0; i < 4; i++)
                {
                    if (this[(NilSeat)i] == NilSeatRole.NilSet) return true;
                }
                return false;
            }
        }

        /// <summary>Absolute and unambiguous: "N=Nil E=Opponent S=Cover W=Opponent".</summary>
        public string Describe()
        {
            return string.Format("N={0} E={1} S={2} W={3}", _north, _east, _south, _west);
        }

        public override string ToString() => Describe();

        public bool Equals(NilSeatRoles other)
        {
            return _north == other._north && _east == other._east
                && _south == other._south && _west == other._west;
        }

        public override bool Equals(object? obj) => obj is NilSeatRoles other && Equals(other);

        public override int GetHashCode()
        {
            return (int)_north | ((int)_east << 2) | ((int)_south << 4) | ((int)_west << 6);
        }

        public static bool operator ==(NilSeatRoles a, NilSeatRoles b) => a.Equals(b);

        public static bool operator !=(NilSeatRoles a, NilSeatRoles b) => !a.Equals(b);
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

        /// <summary>Disable the transposition table. A diagnostic; orders of magnitude slower.</summary>
        NoMemo = 0x4u,

        /// <summary>
        /// Allow more than <see cref="NilSolverNative.CardLimit"/> cards per hand.
        /// Required for a full thirteen even in fast mode.
        /// </summary>
        ForceLarge = 0x8u,

        /// <summary>Tie-break direction: each pair takes as FEW tricks as it can, not as many.</summary>
        MinimiseOwnTricks = 0x10u,

        // Bit 0x20u is RETIRED AND BURNED, not free. It was NilAlreadySet, which
        // is now NilSeatRole.NilSet in the roles passed alongside. The bit stays
        // unassigned rather than recycled so an old caller gets an ignored flag
        // rather than a silently different objective.

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
        NoNarrow = 0x400u,

        /// <summary>
        /// Do not spend a fast-mode presolve to bound full mode's root window. The nil
        /// question is ~1000x cheaper than the full one and its answer bounds it: a nil
        /// that cannot be forced puts the packed value out of reach of the range where it
        /// can. Same value and same principal variation; diagnostic and a control arm.
        /// Inert under <see cref="FastMode"/> and when the nil is already set.
        /// </summary>
        NoPresolve = 0x800u,

        /// <summary>
        /// Search the forced final trick instead of evaluating it. At a trick boundary
        /// with one card per hand the trick is fully determined, so five nodes collapse
        /// to one. Same value and same principal variation; diagnostic and a control arm.
        /// </summary>
        NoLastTrick = 0x2000u,

        /// <summary>
        /// Do not spend the two static proofs in full mode, where they yield a fail-soft
        /// bound rather than settling the node outright. Same value and same principal
        /// variation; diagnostic and a control arm. Implied by
        /// <see cref="NoStaticBounds"/>.
        /// </summary>
        NoFullStatic = 0x4000u,

        /// <summary>
        /// Consult the transposition table at every ply rather than only at a trick
        /// boundary. Building the key is O(live cards) and three nodes in four are
        /// mid-trick, so the table's own cost dominates; the hit rate one ply into a
        /// trick is 5-11% against 62-70% at a boundary. Restores the every-ply behaviour
        /// the solver had before patch 30, which is 2.3x to 3.6x slower at 11 to 13
        /// cards. Same value and same principal variation; diagnostic and a control arm.
        /// </summary>
        TtAllPlies = 0x8000u,

        /// <summary>
        /// Do not answer a full-mode node from the reachable range of the tricks left.
        /// A trick is worth a fixed amount to the nil bidder, another to the cover
        /// partner and nothing to either opponent, so a subtree with t tricks left has a
        /// value range computable from t alone; when that whole range sits on one side
        /// of the window the node is answered without reading a card. Worth 9-22% of
        /// nodes in full mode. Same value and same principal variation; diagnostic and a
        /// control arm. Inert under <see cref="FastMode"/>.
        /// </summary>
        NoTargetBounds = 0x10000u,

        /// <summary>
        /// Do not put one card from each present suit at the head of the move list. By
        /// default the solver tries one card from every suit it may legally choose among
        /// before returning to the canonical order, which is DDS section 5's "good
        /// mixture of moves (i.e. not all cards from the same suit first)". It spends
        /// nothing to decide the order -- the same moves are searched -- and is worth
        /// 5.4% to 9.0% of nodes at 11 to 13 cards. Applies in both modes, and inert on a
        /// seat following suit. Same value and, in full mode, the same principal
        /// variation; diagnostic and a control arm.
        /// </summary>
        NoSuitMix = 0x20000u,

        /// <summary>
        /// Do not tighten the reachable-range bound with the opponents' forced tricks.
        /// DDS section 4: where one hand holds the top outstanding spades as a run from
        /// the top, each of those cards wins the trick it is played on however all four
        /// players play, so those tricks cannot fall to the other side and the range the
        /// reach bound covers shrinks. Same value and same principal variation;
        /// diagnostic and a control arm. Inert under <see cref="FastMode"/>.
        /// </summary>
        NoLaterTricks = 0x40000u,

        /// <summary>
        /// Do not let a transposition-table entry that matches the position without
        /// settling the window shorten the node. By default such a match supplies the
        /// threshold the node's own cutoff test reads, when it is tighter than the one
        /// the window carries: beta at a maximising node, alpha at a minimising one. The
        /// window itself is untouched, so children are searched under exactly what the
        /// caller asked. Worth 0.5% to 1.2% of nodes in full mode, most of it at 13
        /// cards. Same value and same principal variation; diagnostic and a control arm.
        /// Inert under <see cref="FastMode"/>, which has no partial entries.
        /// </summary>
        NoTtNarrow = 0x80000u
    }

    /// <summary>Return codes. <see cref="Ok"/> is success; everything else is negative.</summary>
    /// <remarks>
    /// These had drifted from the header. -4 was once NIL_ERR_TOO_MANY_CARDS,
    /// reporting a refusal that no longer exists; the header dropped it and moved
    /// everything below up by one, and this mirror was not moved with it. Every
    /// code from -4 down therefore read as the wrong name, and
    /// <see cref="Unsupported"/> could not be produced at all -- which matters
    /// now that a roles array holding two nils returns exactly that.
    /// </remarks>
    public enum NilStatus
    {
        Ok = 0,
        NullArgument = -1,
        Parse = -2,
        IllegalPosition = -3,
        BufferTooSmall = -4,
        Internal = -5,

        /// <summary>
        /// The call asked for something this build cannot produce: a principal
        /// variation in fast mode, or a roles array with more than one nil in it.
        /// </summary>
        Unsupported = -6
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
            [In] int[] seats,
            uint flags,
            out NilResult result,
            StringBuilder errBuf,
            int errLen);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int nil_solve_pv(
            [MarshalAs(UnmanagedType.LPStr)] string pbn,
            int leader,
            [MarshalAs(UnmanagedType.LPStr)] string currentTrick,
            [In] int[] seats,
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
            [In] int[] seats,
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
            [In] int[] seats,
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
