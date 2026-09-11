#pragma once

#include <functional>

bool RunGitHubProgress(const wchar_t* text, const std::function<bool()>& operation);
