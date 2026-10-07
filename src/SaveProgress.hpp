#pragma once

#include <functional>
#include <string>

bool RunGitHubProgress(const wchar_t* text, const std::function<bool()>& operation);
void UpdateGitHubProgress(size_t current, size_t total, const std::wstring& item = {});

void BeginGitHubEditorSession(const std::wstring& tempFile,
                              const std::wstring& remotePath,
                              const std::wstring& remoteSha,
                              const std::wstring& token,
                              const std::wstring& repository,
                              const std::wstring& branch);

std::wstring EndGitHubEditorSession();

bool HandleGitHubEditorExitRequest();
