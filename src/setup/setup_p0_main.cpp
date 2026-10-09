// P0 bootstrapper: console extractor used to validate the payload format and
// measure the final setup size. Replaced by the windowed setup in P2.
//
//   PulseSetup-P0.exe --info
//   PulseSetup-P0.exe --extract <folder>

#include <windows.h>

#include <cstdio>
#include <string>

#include "setup_payload.h"

namespace {
std::wstring SelfPath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0) return {};
        if (n < path.size()) { path.resize(n); return path; }
        path.resize(path.size() * 2);
    }
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    pulse::setup::Payload payload;
    std::wstring error;
    const ULONGLONG t0 = GetTickCount64();
    if (!payload.Open(SelfPath(), error)) { fwprintf(stderr, L"error: %ls\n", error.c_str()); return 2; }
    const auto& info = payload.Info();
    const ULONGLONG t1 = GetTickCount64();
    wprintf(L"version %hs, %zu files, %llu bytes (payload %llu bytes), verified in %llu ms\n",
            info.version.c_str(), info.files.size(), info.total_bytes, info.payload_size, t1 - t0);
    if (argc >= 3 && std::wstring(argv[1]) == L"--extract") {
        int last = -1;
        const bool ok = payload.ExtractTo(argv[2], [&](const pulse::setup::ExtractProgress& p) {
            const int pct = p.total_bytes ? static_cast<int>(p.done_bytes * 100 / p.total_bytes) : 0;
            if (pct != last && pct % 10 == 0) { wprintf(L"  %3d%%  %ls\n", pct, p.current->path.c_str()); last = pct; }
            return true;
        }, error);
        if (!ok) { fwprintf(stderr, L"error: %ls\n", error.c_str()); return 3; }
        wprintf(L"extracted to %ls in %llu ms\n", argv[2], GetTickCount64() - t1);
    } else {
        for (const auto& f : info.files) wprintf(L"  %10llu  %ls\n", f.size, f.path.c_str());
    }
    return 0;
}
