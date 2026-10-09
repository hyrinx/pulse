#include "setup_shortcut.h"

#include "setup_common.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>


namespace pulse::setup {
namespace {

template <class T>
struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() { return p; }
};

}  // namespace

bool EnsureShortcut(const std::wstring& lnk, const std::wstring& target, const std::wstring& args,
                    const std::wstring& workdir, const std::wstring& description, std::wstring& error) {
    Com<IShellLinkW> link;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    if (FAILED(hr)) { error = L"IShellLink: " + Win32Error(hr); return false; }
    Com<IPersistFile> file;
    hr = link->QueryInterface(IID_PPV_ARGS(&file));
    if (FAILED(hr)) { error = L"IPersistFile: " + Win32Error(hr); return false; }

    if (GetFileAttributesW(lnk.c_str()) != INVALID_FILE_ATTRIBUTES &&
        SUCCEEDED(file.p->Load(lnk.c_str(), STGM_READ))) {
        wchar_t existing[MAX_PATH]{}, existing_args[1024]{};
        if (SUCCEEDED(link->GetPath(existing, MAX_PATH, nullptr, SLGP_RAWPATH)) &&
            SUCCEEDED(link->GetArguments(existing_args, 1024)) &&
            CompareStringOrdinal(ExpandEnv(existing).c_str(), -1, target.c_str(), -1, TRUE) == CSTR_EQUAL &&
            args == existing_args)
            return true;
    }

    CreateDirs(ParentDir(lnk));
    link->SetPath(target.c_str());
    link->SetArguments(args.c_str());
    link->SetWorkingDirectory(workdir.c_str());
    link->SetDescription(description.c_str());
    link->SetIconLocation(target.c_str(), 0);
    hr = file.p->Save(lnk.c_str(), TRUE);
    if (FAILED(hr)) { error = L"Save " + lnk + L": " + Win32Error(hr); return false; }
    return true;
}

}  // namespace pulse::setup
