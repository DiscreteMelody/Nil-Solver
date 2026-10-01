#include "nil/bigalloc.hpp"

#include <cstdint>
#include <cstdlib>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#if defined(_MSC_VER)
// OpenProcessToken, LookupPrivilegeValueW and AdjustTokenPrivileges live in
// advapi32.  CMake and the Visual Studio project link it by default; this keeps
// a hand-rolled build from failing at link time.
#pragma comment(lib, "advapi32.lib")
#endif
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#endif

namespace nil {

namespace {

#if defined(_WIN32)
// Large pages need SeLockMemoryPrivilege ENABLED in the process token, not just
// granted to the account, so it is switched on for the one call and restored.
// Any failure along the way means "no large pages", which is the answer most
// processes get; the caller falls back to ordinary pages.
void* windows_large_pages(std::size_t bytes, std::size_t& mapped) {
    const SIZE_T large = GetLargePageMinimum();
    if (!large) return nullptr;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return nullptr;
    }
    void* p = nullptr;
    LUID luid;
    if (LookupPrivilegeValueW(nullptr, L"SeLockMemoryPrivilege", &luid)) {
        TOKEN_PRIVILEGES tp{};
        TOKEN_PRIVILEGES prev{};
        DWORD prev_len = 0;
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        // AdjustTokenPrivileges "succeeds" without the privilege; only
        // GetLastError tells the two apart.
        if (AdjustTokenPrivileges(token, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), &prev, &prev_len) &&
            GetLastError() == ERROR_SUCCESS) {
            const std::size_t rounded = (bytes + large - 1) / large * large;
            p = VirtualAlloc(nullptr, rounded, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,
                             PAGE_READWRITE);
            if (p) mapped = rounded;
            AdjustTokenPrivileges(token, FALSE, &prev, 0, nullptr, nullptr);
        }
    }
    CloseHandle(token);
    return p;
}
#endif

}  // namespace

bool BigBlock::allocate(std::size_t bytes, bool want_huge) {
    release();
    if (bytes == 0) return true;
    want_huge_ = want_huge;
#if defined(_WIN32)
    if (want_huge) {
        std::size_t mapped = 0;
        if (void* p = windows_large_pages(bytes, mapped)) {
            // Large pages are committed and zeroed up front; there is no
            // demand paging for them.
            ptr_ = base_ = p;
            bytes_ = bytes;
            mapped_ = mapped;
            how_ = 2;
            huge_ = true;
            return true;
        }
    }
    // Committed but not touched: Windows zeroes each page on first access.
    if (void* p = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
        ptr_ = base_ = p;
        bytes_ = mapped_ = bytes;
        how_ = 1;
        return true;
    }
#elif defined(__unix__) || defined(__APPLE__)
    // Over-map by one huge page so the table can start on a 2 MiB boundary:
    // transparent huge pages only back aligned 2 MiB extents.
    constexpr std::size_t HUGE_PAGE = std::size_t(2) << 20;
    const std::size_t extra = want_huge ? HUGE_PAGE : 0;
    void* p = mmap(nullptr, bytes + extra, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
                   -1, 0);
    if (p != MAP_FAILED) {
        base_ = p;
        mapped_ = bytes + extra;
        ptr_ = p;
        if (want_huge) {
            const std::uintptr_t a = reinterpret_cast<std::uintptr_t>(p);
            const std::uintptr_t aligned = (a + HUGE_PAGE - 1) & ~(HUGE_PAGE - 1);
            ptr_ = reinterpret_cast<void*>(aligned);
#if defined(MADV_HUGEPAGE)
            huge_ = madvise(ptr_, bytes, MADV_HUGEPAGE) == 0;
#endif
        }
        bytes_ = bytes;
        how_ = 1;
        return true;
    }
#endif
    // Last resort.  calloc still hands back zeroed memory, and for a block this
    // size most C libraries get it from the OS as demand-zero pages anyway.
    if (void* q = std::calloc(1, bytes)) {
        ptr_ = base_ = q;
        bytes_ = mapped_ = bytes;
        how_ = 3;
        return true;
    }
    return false;
}

void BigBlock::release() {
    if (!base_) return;
    switch (how_) {
#if defined(_WIN32)
        case 1:
        case 2:
            VirtualFree(base_, 0, MEM_RELEASE);
            break;
#elif defined(__unix__) || defined(__APPLE__)
        case 1:
            munmap(base_, mapped_);
            break;
#endif
        default:
            std::free(base_);
            break;
    }
    ptr_ = base_ = nullptr;
    bytes_ = mapped_ = 0;
    how_ = 0;
    huge_ = false;
}

}  // namespace nil
