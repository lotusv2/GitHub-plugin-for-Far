#pragma once

#include <windows.h>

#include <functional>

bool RunGitHubProgress(const wchar_t* text, const std::function<bool()>& operation);
void ShowGitHubProgressTimed(const wchar_t* text, DWORD timeoutMs);
