#pragma once

#include <string>

namespace pulse::setup {

// Creates or updates a .lnk. An existing shortcut that already points at
// `target` is left untouched so taskbar/Start pins survive upgrades (#72).
bool EnsureShortcut(const std::wstring& lnk, const std::wstring& target, const std::wstring& args,
                    const std::wstring& workdir, const std::wstring& description, std::wstring& error);

}  // namespace pulse::setup
