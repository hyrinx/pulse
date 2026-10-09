#include "setup_transaction.h"

#include "setup_common.h"
#include "setup_log.h"

#include <windows.h>


namespace pulse::setup {
namespace {

bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

void EnsureParent(const std::wstring& path) { CreateDirs(ParentDir(path)); }

// Journal: UTF-8 lines "<0|1>\t<relative path>", written and flushed before
// each move. had_old=1 means the previous file is (or will be) in backup.
bool AppendJournal(HANDLE journal, bool had_old, const std::wstring& rel) {
    const std::wstring line = std::wstring(had_old ? L"1\t" : L"0\t") + rel + L"\n";
    const int n = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, utf8.data(), n, nullptr, nullptr);
    DWORD written = 0;
    return WriteFile(journal, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr) &&
           written == utf8.size() && FlushFileBuffers(journal);
}

// Undo of one journaled step; safe whether or not the step completed.
void UndoStep(const std::wstring& app, const std::wstring& backup, bool had_old, const std::wstring& rel) {
    const std::wstring dst = app + L"\\" + rel, old = backup + L"\\" + rel;
    if (had_old) {
        if (Exists(old) && !MoveFileExW(old.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING))
            Log(L"Rollback could not restore " + dst + L": " + Win32Error(GetLastError()));
    } else if (Exists(dst) && !DeleteFileW(dst.c_str())) {
        Log(L"Rollback could not remove " + dst + L": " + Win32Error(GetLastError()));
    }
}

}  // namespace

FileTransaction::FileTransaction(std::wstring app_dir)
    : app_(NormalizeDir(std::move(app_dir))),
      work_(app_ + L"\\.pulse-setup"),
      staging_(work_ + L"\\staging"),
      backup_(work_ + L"\\backup"),
      journal_(work_ + L"\\journal.txt") {}

bool FileTransaction::RecoverInterrupted(const std::wstring& app_dir, std::wstring& error) {
    FileTransaction t(app_dir);
    std::string journal;
    if (!ReadWholeFile(t.journal_, journal)) return true;
    Log(L"Found an interrupted update; restoring the previous files");
    std::vector<Step> steps;
    for (size_t pos = 0; pos < journal.size();) {
        size_t end = journal.find('\n', pos);
        if (end == std::string::npos) end = journal.size();  // torn last line: its move never started
        const std::string line = journal.substr(pos, end - pos);
        pos = end + 1;
        if (line.size() < 3 || line[1] != '\t' || end == journal.size()) continue;
        steps.push_back({Utf8ToWide(line.substr(2)), line[0] == '1'});
    }
    for (auto it = steps.rbegin(); it != steps.rend(); ++it) UndoStep(t.app_, t.backup_, it->had_old, it->rel);
    if (!DeleteFileW(t.journal_.c_str())) {
        error = L"Cannot clear interrupted update journal: " + Win32Error(GetLastError());
        return false;
    }
    RemoveTree(t.work_);
    return true;
}

bool FileTransaction::Begin(std::wstring& error) {
    if (!RecoverInterrupted(app_, error)) return false;
    RemoveTree(work_);
    if (!CreateDirs(staging_)) {
        error = L"Cannot create " + staging_ + L": " + Win32Error(GetLastError());
        return false;
    }
    SetFileAttributesW(work_.c_str(), FILE_ATTRIBUTE_HIDDEN);
    return true;
}

bool FileTransaction::Commit(const std::vector<std::wstring>& files, std::wstring& error) {
    HANDLE journal = CreateFileW(journal_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    if (journal == INVALID_HANDLE_VALUE) {
        error = L"Cannot create update journal: " + Win32Error(GetLastError());
        return false;
    }
    steps_.clear();
    bool ok = true;
    for (const auto& rel : files) {
        const std::wstring src = staging_ + L"\\" + rel, dst = app_ + L"\\" + rel, old = backup_ + L"\\" + rel;
        const bool had_old = Exists(dst);
        if (!AppendJournal(journal, had_old, rel)) {
            error = L"Cannot write update journal: " + Win32Error(GetLastError());
            ok = false;
            break;
        }
        steps_.push_back({rel, had_old});
        if (had_old) {
            EnsureParent(old);
            SetFileAttributesW(dst.c_str(), FILE_ATTRIBUTE_NORMAL);
            // Renaming works even for a running exe; a file opened without
            // FILE_SHARE_DELETE fails here and the whole commit is undone.
            if (!MoveFileExW(dst.c_str(), old.c_str(), 0)) {
                error = L"Cannot replace " + dst + L": " + Win32Error(GetLastError());
                ok = false;
                break;
            }
        }
        EnsureParent(dst);
        if (!MoveFileExW(src.c_str(), dst.c_str(), 0)) {
            error = L"Cannot install " + dst + L": " + Win32Error(GetLastError());
            ok = false;
            break;
        }
    }
    CloseHandle(journal);
    if (!ok) {
        Log(L"Commit failed, rolling back: " + error);
        Rollback(steps_.size());
        return false;
    }
    // The journal's removal is the commit point.
    if (!DeleteFileW(journal_.c_str())) Log(L"Could not delete journal: " + Win32Error(GetLastError()));
    return true;
}

void FileTransaction::Rollback(size_t done) {
    for (size_t i = done; i-- > 0;) UndoStep(app_, backup_, steps_[i].had_old, steps_[i].rel);
    DeleteFileW(journal_.c_str());
    RemoveTree(work_);
}

void FileTransaction::Finish() { RemoveTree(work_); }
void FileTransaction::Abort() { RemoveTree(work_); }

}  // namespace pulse::setup
