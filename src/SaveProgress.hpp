#pragma once

#include <windows.h>

HANDLE ShowGitHubProgress(const wchar_t* text);
void CloseGitHubProgress(HANDLE handle);
void ShowGitHubProgressTimed(const wchar_t* text, DWORD timeoutMs);
