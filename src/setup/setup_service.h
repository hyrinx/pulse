#pragma once
// Minimal Service Control Manager helpers for the PulseIndex service.

#include <windows.h>

#include <string>

namespace pulse::setup {

bool ServiceExists(const wchar_t* name);
// Returns ERROR_SUCCESS when stopped (or absent), otherwise a Win32 error.
DWORD StopService(const wchar_t* name, unsigned timeout_ms);
bool WaitUntilServiceGone(const wchar_t* name, unsigned timeout_ms);
std::wstring ServiceBinaryPath(const wchar_t* name);

}  // namespace pulse::setup
