#pragma once

#include <plugin.hpp>
#include "GitHubClient.hpp"

class FarGitHubPanel
{
public:
    FarGitHubPanel();

    intptr_t GetFindData(PluginPanelItem** items, size_t* count, OPERATION_MODES mode);
    void FreeFindData(PluginPanelItem* items, size_t count);
    void GetOpenPanelInfo(OpenPanelInfo* info);
    intptr_t SetDirectory(const wchar_t* directory, OPERATION_MODES mode);
    intptr_t ProcessHostFile(PluginPanelItem* items, size_t count, OPERATION_MODES mode);
    intptr_t MakeDirectory(const wchar_t* name, OPERATION_MODES mode);
    intptr_t PutFiles(PluginPanelItem* items, size_t count, const wchar_t* sourcePath, OPERATION_MODES mode);

private:
    std::wstring Token;
    std::wstring Repository;
    std::wstring CurrentPath;
    std::vector<GitHubEntry> Entries;
    std::vector<GitHubRepository> Repositories;
    std::wstring Error;

    bool Reload();
    bool ReloadRepositories();
    std::wstring FullPath(const std::wstring& name) const;
    bool EditFile(const std::wstring& path);
    void ShowError(const std::wstring& title = L"GitHub") const;
};
