// Memory for the two big tables: the main transposition table (tt.hpp) and the
// double-dummy engine's (ddtricks.hpp).
//
// WHY THE TABLES DO NOT LIVE IN A std::vector ANY MORE (optimization Q5, Sept 2026)
// -------------------------------------------------------------------------------
// Three separate costs came with `std::vector<Entry>::assign(n, Entry())`, and
// the pass that removed them measured each one:
//
//   * EAGER ZERO-FILL.  assign() writes every byte of 512 MiB + 64 MiB before
//     the first probe, on the first solve of every thread and every process.
//     An all-zero page is already a valid empty table here -- generation 0 is
//     "never written" in the main table, meta 0 is "unused" in the engine's --
//     so memory the operating system hands out already zeroed (mmap on Linux,
//     VirtualAlloc on Windows) is used as it comes, and a page is only zeroed
//     by the kernel the first time a probe or store touches it.  A one-shot
//     process that solves an easy deal never touches most of the table.
//
//   * ALIGNMENT.  A large vector comes from malloc, which on glibc returns an
//     address 16 bytes past a page boundary.  The engine's 128-byte buckets then
//     span THREE cache lines each instead of two, and a third of the main
//     table's 96-byte buckets do too.  OS pages are page aligned, so every
//     bucket starts on a line boundary.
//
//   * TLB REACH.  Both tables are probed at random.  On 4 KiB pages 512 MiB is
//     131,072 pages and 64 MiB another 16,384, far beyond any second-level TLB,
//     so nearly every probe also walks the page tables.  On 2 MiB pages they
//     are 256 and 32 pages, which fit.  profiling put the main table's probe at
//     ~17% of per-card time and the engine's fact lookups at another ~19%, and
//     both are single random reads into a big table -- the access pattern huge
//     pages exist for.
//
// HUGE PAGES ARE BEST-EFFORT AND NEVER CHANGE AN ANSWER.  On Linux the region
// is 2 MiB aligned and advised MADV_HUGEPAGE, which transparent huge pages
// honour in both the "always" and the "madvise" setting.  On Windows large
// pages need the "Lock pages in memory" privilege (SeLockMemoryPrivilege) on
// the account; when the privilege is not granted -- the usual case for a
// service account -- the allocation silently falls back to ordinary
// demand-zero pages.  Everything else falls back to calloc.  Which one was
// obtained is reported by `huge()`, for diagnostics only.
#ifndef NIL_BIGALLOC_HPP
#define NIL_BIGALLOC_HPP

#include <cstddef>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <xmmintrin.h>
#endif

namespace nil {

class BigBlock {
public:
    BigBlock() = default;
    ~BigBlock() { release(); }
    BigBlock(const BigBlock&) = delete;
    BigBlock& operator=(const BigBlock&) = delete;

    // `bytes` of zeroed memory, aligned to at least 4 KiB, replacing whatever
    // the block held.  Returns false only if every method failed.
    bool allocate(std::size_t bytes, bool want_huge);
    void release();

    void* data() const { return ptr_; }
    std::size_t bytes() const { return bytes_; }
    bool huge() const { return huge_; }
    bool requested_huge() const { return want_huge_; }

private:
    void* ptr_ = nullptr;     // what the table uses
    void* base_ = nullptr;    // what was mapped (differs when trimmed for alignment)
    std::size_t bytes_ = 0;
    std::size_t mapped_ = 0;
    int how_ = 0;             // 0 none, 1 OS pages, 2 Windows large pages, 3 calloc
    bool huge_ = false;
    bool want_huge_ = false;
};

// Hint the cache that `p` is about to be read.  A no-op where the compiler
// offers nothing.
inline void prefetch_line(const void* p) {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(p);
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    _mm_prefetch(static_cast<const char*>(p), _MM_HINT_T0);
#else
    (void)p;
#endif
}

}  // namespace nil

#endif  // NIL_BIGALLOC_HPP
