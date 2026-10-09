#include "setup_log.h"

#include <windows.h>

#include <cstdio>
#include <mutex>

namespace pulse::setup {
namespace {
std::mutex g_mutex;
HANDLE g_file = INVALID_HANDLE_VALUE;
std::wstring g_path;
}  // namespace

void LogOpen(const std::wstring& path) {
    std::lock_guard lock(g_mutex);
    if (g_file != INVALID_HANDLE_VALUE) return;
    g_path = path;
    g_file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
}

const std::wstring& LogPath() { return g_path; }

void Log(const std::wstring& line) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t stamp[40];
    swprintf_s(stamp, L"%04u-%02u-%02u %02u:%02u:%02u.%03u  ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
               t.wSecond, t.wMilliseconds);
    const std::wstring text = stamp + line + L"\r\n";
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0,
                                      nullptr, nullptr);
    std::string utf8(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), n, nullptr,
                        nullptr);
    std::lock_guard lock(g_mutex);
    if (g_file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(g_file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

}  // namespace pulse::setup
