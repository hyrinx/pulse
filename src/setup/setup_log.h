#pragma once
// Append-only UTF-8 setup log (%TEMP%\Pulse-Setup-<timestamp>.log by default).

#include <string>

namespace pulse::setup {

void LogOpen(const std::wstring& path);
const std::wstring& LogPath();
void Log(const std::wstring& line);

}  // namespace pulse::setup
