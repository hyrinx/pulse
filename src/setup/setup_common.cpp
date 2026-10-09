#include "setup_common.h"

#include <shlobj.h>

#include <algorithm>
#include <cwctype>

namespace pulse::setup {

std::wstring Win32Error(DWORD code) {
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
    std::wstring message = L"error " + std::to_wstring(code);
    if (text) {
        message += L": ";
        message += text;
        LocalFree(text);
    }
    while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r' || message.back() == L' '))
        message.pop_back();
    return message;
}

std::wstring NormalizeDir(std::wstring path) {
    auto blank = [](wchar_t c) { return c == L' ' || c == L'\t'; };
    while (!path.empty() && blank(path.front())) path.erase(path.begin());
    while (!path.empty() && blank(path.back())) path.pop_back();
    for (auto& c : path) if (c == L'/') c = L'\\';
    // Keep "C:\" as a root; strip any other trailing separator.
    while (path.size() > 3 && path.back() == L'\\') path.pop_back();
    return path;
}

bool EqualsNoCase(const std::wstring& a, const wchar_t* b) {
    return CompareStringOrdinal(a.c_str(), -1, b, -1, TRUE) == CSTR_EQUAL;
}

bool SameDirectory(const std::wstring& a, const std::wstring& b) {
    const auto x = NormalizeDir(a), y = NormalizeDir(b);
    return !x.empty() && !y.empty() && CompareStringOrdinal(x.c_str(), -1, y.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool PathInDirectories(const std::wstring& path, const std::vector<std::wstring>& dirs) {
    // An unreadable path counts as inside so an uninspectable process blocks setup.
    if (path.empty()) return true;
    for (const auto& d : dirs) {
        auto dir = NormalizeDir(d);
        if (dir.empty()) continue;
        if (dir.back() != L'\\') dir += L'\\';
        if (path.size() >= dir.size() &&
            CompareStringOrdinal(path.c_str(), static_cast<int>(dir.size()), dir.c_str(),
                                 static_cast<int>(dir.size()), TRUE) == CSTR_EQUAL)
            return true;
    }
    return false;
}

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) out = raw;
    CoTaskMemFree(raw);
    return out;
}

std::wstring ExpandEnv(const std::wstring& text) {
    const DWORD n = ExpandEnvironmentStringsW(text.c_str(), nullptr, 0);
    if (!n) return text;
    std::wstring out(n, L'\0');
    ExpandEnvironmentStringsW(text.c_str(), out.data(), n);
    out.resize(n - 1);
    return out;
}

std::wstring Quote(const std::wstring& s) { return L"\"" + s + L"\""; }

RunResult RunProcess(const std::wstring& exe, const std::wstring& args, bool wait, const std::wstring& cwd) {
    RunResult result;
    std::wstring command = Quote(exe);
    if (!args.empty()) command += L" " + args;
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        cwd.empty() ? nullptr : cwd.c_str(), &si, &pi))
        return result;
    result.started = true;
    CloseHandle(pi.hThread);
    if (wait) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &result.exit_code);
    }
    CloseHandle(pi.hProcess);
    return result;
}

bool IsProcessElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
                    elevation.TokenIsElevated;
    CloseHandle(token);
    return ok;
}

std::wstring ParentDir(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

bool CreateDirs(const std::wstring& dir) {
    if (dir.empty()) return false;
    const DWORD a = GetFileAttributesW(dir.c_str());
    if (a != INVALID_FILE_ATTRIBUTES) return (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const auto parent = ParentDir(dir);
    if (!parent.empty() && parent.size() < dir.size() && parent.back() != L':') CreateDirs(parent);
    return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool ReadWholeFile(const std::wstring& path, std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart < (64 << 20);
    if (ok) {
        data.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = data.empty() || (ReadFile(h, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) &&
                              read == data.size());
    }
    CloseHandle(h);
    return ok;
}

bool WriteWholeFile(const std::wstring& path, const void* data, size_t size) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(h, data, static_cast<DWORD>(size), &written, nullptr) && written == size;
    CloseHandle(h);
    return ok;
}

void RemoveTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring p = dir + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveTree(p);
            } else {
                SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (!DeleteFileW(p.c_str())) MoveFileExW(p.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
            }
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    SetFileAttributesW(dir.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (!RemoveDirectoryW(dir.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND &&
        GetLastError() != ERROR_PATH_NOT_FOUND)
        MoveFileExW(dir.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
}

std::wstring Utf8ToWide(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string WideToUtf8(const std::wstring& w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

}  // namespace pulse::setup
