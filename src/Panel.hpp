#pragma once

#include <windows.h>
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
    intptr_t ProcessInput(const INPUT_RECORD& record);
    intptr_t MakeDirectory(const wchar_t* name, OPERATION_MODES mode);
    intptr_t PutFiles(PluginPanelItem* items, size_t count, const wchar_t* sourcePath, OPERATION_MODES mode);
    intptr_t GetFiles(PluginPanelItem* items, size_t count, bool move, const wchar_t* destinationPath, OPERATION_MODES mode);
    intptr_t DeleteFiles(PluginPanelItem* items, size_t count, OPERATION_MODES mode);
    void ReloadSettings();

    std::wstring GetDiagnosticState() const
    {
        return L"Repository=[" + Repository + L"] CurrentPath=[" + CurrentPath + L"] Error=[" + Error + L"]";
    }

private:
    friend intptr_t ProcessRenameInput(FarGitHubPanel* panel);

    std::wstring Token;
    std::wstring Repository;
    std::wstring CurrentBranch;
    std::wstring DefaultBranch;
    std::wstring CurrentPath;
    std::wstring SearchText;
    std::vector<GitHubEntry> Entries;
    std::vector<GitHubRepository> Repositories;
    std::vector<GitHubBranch> Branches;
    std::vector<std::wstring> Favorites;
    std::wstring Error;

    bool Reload();
    bool ReloadRepositories();
    bool ReloadBranches();
    bool LoadFavorites();
    bool IsFavorite(const std::wstring& fullName) const;
    bool ToggleFavorite(const std::wstring& fullName);
    bool SearchRepositories();
    bool SelectBranch();
    void UpdatePanel() const;
    std::wstring FullPath(const std::wstring& name) const;
    bool EditFile(const std::wstring& path);
    bool RenameEntry(const std::wstring& oldPath, const std::wstring& newPath);
    void ShowError(const std::wstring& title = L"GitHub") const;
};

intptr_t ProcessRenameInput(FarGitHubPanel* panel);
