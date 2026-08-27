// NilSolver.cs -- a managed face on the C ABI.
//
// NilSolverNative is the ABI transcribed; this is the part that is pleasant to
// call.  It owns three things the raw layer deliberately does not:
//
//   * error text.  Every entry point takes an optional char* the native side
//     writes a sentence into, and that sentence is usually the whole diagnosis
//     ("card HA is both on the trick and in N").  Throwing away the buffer and
//     surfacing only the status code turns a five-second fix into an afternoon.
//
//   * PV buffer sizing.  The PV is "N:D2 E:DA S:D5 W:D7 ..." -- four characters
//     a card and one separator, so five bytes a card is exactly enough and 13
//     tricks is the worst case.  We size for that and still handle
//     BufferTooSmall rather than trusting the arithmetic.
//
//   * the fast/full distinction, as two named methods rather than a flag.  They
//     answer different questions and one of them cannot produce a principal
//     variation, which is easier to keep straight when it is in the name.

using CardGameAcademy.Models.Enums;
using CardGameAcademy.Services;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace NilSolver
{
    /// <summary>
    /// One legal card at the root, and what playing it leads to.
    ///
    /// This is the DDS-shaped answer -- not "what should I play" but "here is
    /// every card you may play and what each one costs". Score a player's actual
    /// choice against it, or colour a hand so a learner can see which cards were
    /// safe and which threw the nil away.
    /// </summary>
    public sealed class NilMoveScore
    {
        /// <summary>0 = spades, 1 = hearts, 2 = diamonds, 3 = clubs. Same numbering as DDS.</summary>
        public int Suit { get; init; }

        /// <summary>2..14, ace high. Same numbering as DDS.</summary>
        public int Rank { get; init; }

        public Suit CGASuit => GetCGASuit(Suit);

        public Value CGAValue => GetCGAValue(Rank);

        /// <summary>
        /// Other legal cards that are this same move under a different name, as
        /// ranks on the same 2..14 scale as <see cref="Rank"/>.
        ///
        /// With the jack already played, holding the king and the queen is one
        /// move under two names: every card still in existence is above both or
        /// below both, so the two plays reach positions that differ only by
        /// swapping two labels. The solver searches one and names the other here
        /// rather than searching the same tree twice.
        ///
        /// <see cref="Rank"/> itself is NOT in this list — same convention as
        /// DDS's <c>equals</c>, so a loop written against that ports unchanged.
        /// <see cref="Expand"/> is there when you want a row per legal card.
        /// </summary>
        public IReadOnlyList<int> EqualRanks { get; init; } = Array.Empty<int>();

        /// <summary>
        /// Does the nil fail AFTER this card is played, against best play by
        /// everyone from there on?
        ///
        /// Read it from whichever side you are on: for the nil bidder or its
        /// covering partner, false means this card holds the nil together; for an
        /// opponent, true means this card breaks it. One fact rather than two,
        /// because a double dummy answer does not depend on who asked.
        /// </summary>
        public bool NilFails { get; init; }

        /// <summary>
        /// Tricks the nil bidder takes down the line this card leads to, INCLUDING
        /// the trick this card completes if it completes one.
        /// <see cref="NilSolverNative.TricksUnknown"/> after a fast solve — check
        /// <see cref="NilSolution.TrickCountsKnown"/>.
        /// </summary>
        public int NilTricks { get; init; } = NilSolverNative.TricksUnknown;

        public int NilSideTricks { get; init; } = NilSolverNative.TricksUnknown;
        public int OpponentTricks { get; init; } = NilSolverNative.TricksUnknown;

        /// <summary>
        /// True when this card achieves the position's own value — one of the
        /// moves the solver would have been content to pick. There is usually more
        /// than one, and among cards that tie, any is as good as any other.
        /// </summary>
        public bool IsBest { get; init; }

        /// <summary>Every rank this row stands for, this card's own included, ascending.</summary>
        public IEnumerable<int> Expand() => new[] { Rank }.Concat(EqualRanks).OrderBy(r => r);

        /// <summary>
        /// This row's score attached to one specific rank from its class, with
        /// <see cref="EqualRanks"/> cleared.
        ///
        /// Clearing it is the point: in an expanded list the grouping is already
        /// materialised, and leaving the mask on would let a caller expand the
        /// same class a second time. <see cref="NilSolution.Moves"/> is where the
        /// grouping still lives.
        /// </summary>
        public NilMoveScore WithRank(int rank) => new NilMoveScore
        {
            Suit = Suit,
            Rank = rank,
            EqualRanks = Array.Empty<int>(),
            NilFails = NilFails,
            NilTricks = NilTricks,
            NilSideTricks = NilSideTricks,
            OpponentTricks = OpponentTricks,
            IsBest = IsBest
        };

        /// <summary>"K♠", "T♥", "2♣" — for display. <see cref="ToString"/> is the
        /// ASCII form, which is the better one for logs.</summary>
        public string DisplayName => $"{RankChar(Rank)}{"♠♥♦♣"[Suit]}";

        /// <summary>"KS", "TH", "2C" — the card in the usual short form.</summary>
        public override string ToString() => $"{RankChar(Rank)}{"SHDC"[Suit]}";

        private static char RankChar(int rank) => rank switch
        {
            14 => 'A',
            13 => 'K',
            12 => 'Q',
            11 => 'J',
            10 => 'T',
            _ => (char)('0' + rank)
        };

        internal static NilMoveScore FromNative(in NilMove m)
        {
            var equals = new List<int>();
            for (int r = 2; r <= 14; r++)
            {
                if ((m.EqualRanks & (1 << r)) != 0) equals.Add(r);
            }

            return new NilMoveScore
            {
                Suit = m.Suit,
                Rank = m.Rank,
                EqualRanks = equals,
                NilFails = m.NilFails != 0,
                NilTricks = m.NilTricks,
                NilSideTricks = m.NilSideTricks,
                OpponentTricks = m.OpponentTricks,
                IsBest = m.IsBest != 0
            };
        }

        private Suit GetCGASuit(int suitIndex) => suitIndex switch
        {
            DDSConstants.SPADES => CardGameAcademy.Models.Enums.Suit.Spades,
            DDSConstants.HEARTS => CardGameAcademy.Models.Enums.Suit.Hearts,
            DDSConstants.DIAMONDS => CardGameAcademy.Models.Enums.Suit.Diamonds,
            DDSConstants.CLUBS => CardGameAcademy.Models.Enums.Suit.Clubs,
            _ => throw new System.NotImplementedException()
        };

        private Value GetCGAValue(int valueIndex)
        {
            return (Value)(valueIndex - 2);
        }
    }

    /// <summary>
    /// The outcome of a solve: either the numbers, or the reason there are none.
    /// </summary>
    public sealed class NilSolution
    {
        public NilStatus Status { get; init; } = NilStatus.Ok;

        /// <summary>The native side's error sentence. Empty on success.</summary>
        public string Error { get; init; } = string.Empty;

        public bool Success => Status == NilStatus.Ok;

        /// <summary>
        /// True if the opponents can force the nil bidder to take a trick against
        /// best defence. This is the answer a game client usually wants, and it is
        /// the same in both modes.
        /// </summary>
        public bool NilFails { get; init; }

        /// <summary>
        /// Tricks the nil bidder takes from here on, or
        /// <see cref="NilSolverNative.TricksUnknown"/> (-1) if this came from a
        /// fast solve. Test <see cref="TrickCountsKnown"/> before reading it.
        /// </summary>
        public int NilTricks { get; init; } = NilSolverNative.TricksUnknown;

        /// <summary>Tricks the nil bidder and its covering partner take between them.</summary>
        public int NilSideTricks { get; init; } = NilSolverNative.TricksUnknown;

        /// <summary>Tricks the opposing pair takes.</summary>
        public int OpponentTricks { get; init; } = NilSolverNative.TricksUnknown;

        /// <summary>Tricks left to play in the position. Always meaningful.</summary>
        public int TricksRemaining { get; init; }

        /// <summary>Nodes the search visited. Deterministic at a fixed table size.</summary>
        public ulong Nodes { get; init; }

        /// <summary>
        /// The line the solver found, as "N:D2 E:DA S:D5 W:D7 ...", or empty when
        /// none was asked for. Only full mode produces one.
        /// </summary>
        public string PrincipalVariation { get; init; } = string.Empty;

        /// <summary>
        /// Every legal card and what playing it leads to, or empty when the solve
        /// did not ask for one. Populated by the ScoreMoves methods.
        ///
        /// One entry per equivalence CLASS, not per card — with the jack gone, the
        /// king and the queen are one move under two names and arrive as a single
        /// row. That is the right list for choosing a move, since the members of a
        /// class are interchangeable. <see cref="AllMoves"/> is the right list for
        /// lining up against the cards in a player's hand.
        /// </summary>
        public IReadOnlyList<NilMoveScore> Moves { get; init; } = Array.Empty<NilMoveScore>();

        /// <summary>
        /// <see cref="Moves"/> with every equivalence class expanded, so there is
        /// one entry per legal CARD. Empty when Moves is.
        ///
        /// The analogue of DDS's <c>DDSSolution.AllMoves</c>, and populated the same
        /// way: each row contributes its own card plus one entry per rank in its
        /// EqualRanks. Entries come out in canonical order — spades, hearts,
        /// diamonds, clubs, ascending by rank within each — and each carries its
        /// class's scores, because that is what being in a class means.
        ///
        /// Cards from one class are genuinely indistinguishable here: they share
        /// NilFails, the trick counts and IsBest. Nothing is lost by picking any of
        /// them, which is the whole reason the solver searched only one.
        /// </summary>
        public IReadOnlyList<NilMoveScore> AllMoves => _allMoves ??= ExpandClasses(Moves);

        /// <summary>
        /// The cards from <see cref="AllMoves"/> that achieve the position's value
        /// — the ones the solver would have been content to pick. Empty when Moves
        /// is.
        ///
        /// The analogue of DDS's <c>DDSSolution.BestMoves</c>, but derived from the
        /// solver's own IsBest rather than by comparing each card's score against
        /// the position's. Same answer, and it does not go wrong in fast mode where
        /// there are no per-card scores to compare.
        /// </summary>
        public IReadOnlyList<NilMoveScore> BestMoves =>
            _bestMoves ??= AllMoves.Where(m => m.IsBest).ToArray();

        // Computed on first read rather than at construction, so a solve that never
        // asked for a move list pays nothing. Two threads racing here would each
        // build an identical list and one would win; the result is the same either
        // way, so this is deliberately not locked.
        private IReadOnlyList<NilMoveScore>? _allMoves;
        private IReadOnlyList<NilMoveScore>? _bestMoves;

        private static IReadOnlyList<NilMoveScore> ExpandClasses(IReadOnlyList<NilMoveScore> classes)
        {
            if (classes == null || classes.Count == 0) return Array.Empty<NilMoveScore>();

            var cards = new List<NilMoveScore>(classes.Count);
            foreach (var move in classes)
            {
                // Rows arrive in canonical order and a class is a contiguous run of
                // ranks, so expanding each row ascending and concatenating is
                // already sorted — no second sort needed.
                foreach (var rank in move.Expand()) cards.Add(move.WithRank(rank));
            }
            return cards;
        }

        /// <summary>
        /// False when the trick counts came back as -1, which is what a fast solve
        /// does. Guarding on this rather than on the flag keeps the check next to
        /// the read.
        /// </summary>
        public bool TrickCountsKnown => NilTricks != NilSolverNative.TricksUnknown;

        internal static NilSolution FromNative(in NilResult r, string pv)
        {
            return new NilSolution
            {
                Status = NilStatus.Ok,
                NilFails = r.NilFails != 0,
                NilTricks = r.NilTricks,
                NilSideTricks = r.NilSideTricks,
                OpponentTricks = r.OpponentTricks,
                TricksRemaining = r.TricksRemaining,
                Nodes = r.Nodes,
                PrincipalVariation = pv
            };
        }

        internal static NilSolution FromNative(in NilResult r, string pv,
                                               IReadOnlyList<NilMoveScore> moves)
        {
            return new NilSolution
            {
                Status = NilStatus.Ok,
                NilFails = r.NilFails != 0,
                NilTricks = r.NilTricks,
                NilSideTricks = r.NilSideTricks,
                OpponentTricks = r.OpponentTricks,
                TricksRemaining = r.TricksRemaining,
                Nodes = r.Nodes,
                PrincipalVariation = pv,
                Moves = moves
            };
        }

        internal static NilSolution Failure(NilStatus status, string error)
        {
            return new NilSolution { Status = status, Error = error };
        }
    }

    /// <summary>
    /// Stateless entry points to the native solver. Every call is independent and
    /// safe from any thread; see csharp/README.md for what "safe" costs in memory
    /// on a server, which is the one thing worth reading before wiring this into
    /// a request path.
    /// </summary>
    public static class Nil
    {
        // 13 tricks x 4 cards x 5 bytes a card, plus the terminator, plus slack.
        private const int PvBufferBytes = 13 * 4 * 5 + 16;
        private const int ErrBufferBytes = 256;

        /// <summary>Version string of the loaded native library.</summary>
        public static string Version => NilSolverNative.Version();

        /// <summary>
        /// Transposition table size in mebibytes for subsequent solves on the
        /// CALLING thread. Default 32. Zero is the same as
        /// <see cref="NilFlags.NoMemo"/>.
        /// </summary>
        public static void SetTableSize(uint megabytes) => NilSolverNative.nil_set_table_size(megabytes);

        /// <summary>Release the calling thread's table. Call it from a thread that is done solving.</summary>
        public static void ReleaseTable() => NilSolverNative.nil_release_table();

        /// <summary>
        /// "Can this nil still be broken?" -- the boolean alone, which is the
        /// question a game client asks on every trick. Runs the pruned search, so
        /// it reaches a full thirteen cards; the trick counts come back unknown.
        /// </summary>
        public static NilSolution CanBeBroken(string pbn, NilSeat leader, string? currentTrick,
                                              NilSeatRoles roles, NilFlags flags = NilFlags.None)
        {
            return Solve(pbn, leader, currentTrick, roles, flags | NilFlags.FastMode);
        }

        /// <summary>
        /// The full lexicographic answer: nil_fails plus the three trick counts.
        /// Exhaustive, so it is limited to <see cref="NilSolverNative.CardLimit"/>
        /// cards a hand unless you pass <see cref="NilFlags.ForceLarge"/> and mean
        /// it.
        /// </summary>
        public static NilSolution SolveFull(string pbn, NilSeat leader, string? currentTrick,
                                            NilSeatRoles roles, NilFlags flags = NilFlags.None)
        {
            return Solve(pbn, leader, currentTrick, roles, flags & ~NilFlags.FastMode);
        }

        /// <summary>
        /// As <see cref="SolveFull"/>, and also fills in
        /// <see cref="NilSolution.PrincipalVariation"/>. Returns
        /// <see cref="NilStatus.Unsupported"/> if you pass
        /// <see cref="NilFlags.FastMode"/>: that mode has no line to show, and
        /// quietly running the slow one instead would be worse than saying so.
        /// </summary>
        public static NilSolution SolveWithLine(string pbn, NilSeat leader, string? currentTrick,
                                                NilSeatRoles roles, NilFlags flags = NilFlags.None)
        {
            if (pbn == null) throw new ArgumentNullException(nameof(pbn));

            var err = new StringBuilder(ErrBufferBytes);
            var pv = new StringBuilder(PvBufferBytes);

            int[] seats = roles.ToPbnOrder(pbn);
            int rc = NilSolverNative.nil_solve_pv(pbn, (int)leader, currentTrick ?? string.Empty,
                                                  seats, (uint)flags, out NilResult native,
                                                  pv, pv.Capacity, err, err.Capacity);

            // Defensive: the arithmetic above says this cannot happen, but a
            // buffer size is exactly the kind of thing that is right until the
            // output format changes under it.
            if ((NilStatus)rc == NilStatus.BufferTooSmall)
            {
                pv = new StringBuilder(PvBufferBytes * 4);
                rc = NilSolverNative.nil_solve_pv(pbn, (int)leader, currentTrick ?? string.Empty,
                                                  seats, (uint)flags, out native,
                                                  pv, pv.Capacity, err, err.Capacity);
            }

            return rc == (int)NilStatus.Ok
                ? NilSolution.FromNative(native, pv.ToString())
                : NilSolution.Failure((NilStatus)rc, err.ToString());
        }

        /// <summary>
        /// "Which of my legal cards keep the nil alive?" -- the boolean, per card.
        ///
        /// Costs less than it looks. The position is solved first and every card
        /// is then scored against the same transposition table, so the per-card
        /// searches mostly read back work the first search already did: measured
        /// over twelve random thirteen-card deals it came to 1.0x the nodes and
        /// +0.4% of the wall time of <see cref="CanBeBroken"/>.
        /// </summary>
        public static NilSolution ScoreMoves(string pbn, NilSeat leader, string? currentTrick,
                                             NilSeatRoles roles, NilFlags flags = NilFlags.None)
        {
            return SolveMoves(pbn, leader, currentTrick, roles, flags | NilFlags.FastMode);
        }

        /// <summary>
        /// As <see cref="ScoreMoves"/>, and each card also carries the three trick
        /// counts. Exhaustive, so the nine-card limit applies unless you pass
        /// <see cref="NilFlags.ForceLarge"/>.
        /// </summary>
        public static NilSolution ScoreMovesFull(string pbn, NilSeat leader, string? currentTrick,
                                                 NilSeatRoles roles, NilFlags flags = NilFlags.None)
        {
            return SolveMoves(pbn, leader, currentTrick, roles, flags & ~NilFlags.FastMode);
        }

        /// <summary>
        /// The general form of the move list, if you would rather assemble the
        /// flags yourself.
        /// </summary>
        public static NilSolution SolveMoves(string pbn, NilSeat leader, string? currentTrick,
                                             NilSeatRoles roles, NilFlags flags)
        {
            if (pbn == null) throw new ArgumentNullException(nameof(pbn));

            var err = new StringBuilder(ErrBufferBytes);
            // MaxMoves is the most a hand can hold, so this never needs resizing
            // and there is no retry path to get wrong.
            var rows = new NilMove[NilSolverNative.MaxMoves];

            int rc = NilSolverNative.nil_solve_moves(pbn, (int)leader, currentTrick ?? string.Empty,
                                                     roles.ToPbnOrder(pbn), (uint)flags,
                                                     out NilResult native,
                                                     rows, rows.Length, out int count,
                                                     err, err.Capacity);

            if (rc != (int)NilStatus.Ok) return NilSolution.Failure((NilStatus)rc, err.ToString());

            var moves = new List<NilMoveScore>(count);
            for (int i = 0; i < count; i++) moves.Add(NilMoveScore.FromNative(rows[i]));

            return NilSolution.FromNative(native, string.Empty, moves);
        }

        /// <summary>
        /// The general form, if you would rather assemble the flags yourself.
        /// </summary>
        public static NilSolution Solve(string pbn, NilSeat leader, string? currentTrick,
                                        NilSeatRoles roles, NilFlags flags)
        {
            if (pbn == null) throw new ArgumentNullException(nameof(pbn));

            var err = new StringBuilder(ErrBufferBytes);
            int rc = NilSolverNative.nil_solve(pbn, (int)leader, currentTrick ?? string.Empty,
                                               roles.ToPbnOrder(pbn), (uint)flags,
                                               out NilResult native,
                                               err, err.Capacity);

            return rc == (int)NilStatus.Ok
                ? NilSolution.FromNative(native, string.Empty)
                : NilSolution.Failure((NilStatus)rc, err.ToString());
        }
    }
}
