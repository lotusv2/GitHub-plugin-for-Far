#include "Panel.hpp"
#include "Plugin.hpp"
#include "Settings.hpp"

#include <windows.h>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <cwchar>

namespace
{
const GUID SearchDialogGuid = { 0x7a5f3c21, 0x6b4d, 0x4f92, { 0x8c, 0x31, 0x45, 0x72, 0x9a, 0x16, 0x3e, 0x54 } };
const GUID BranchMenuGuid = { 0x6d8b2a14, 0x4f31, 0x47c6, { 0x91, 0x28, 0x5a, 0x73, 0xb4, 0x0c, 0x2e, 0x61 } };

bool ContainsInsensitive(const std::wstring& value, const std::wstring& query)
{
    if (query.empty())
        return true;

    std::wstring valueLower = value;
    std::wstring queryLower = query;
    std::transform(valueLower.begin(), valueLower.end(), valueLower.begin(), towlower);
    std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), towlower);
    return valueLower.find(queryLower) != std::wstring::npos;
}
}

FarGitHubPanel::FarGitHubPanel()
{
    ReloadSettings();
}

void FarGitHubPanel::ReloadSettings()
{
    GitHubSettings settings;
    settings.LoadToken(Token);
    LoadFavorites();
    Error.clear();
    Reload();
}

bool FarGitHubPanel::LoadFavorites()
{
    GitHubSettings settings;
    return settings.LoadFavorites(Favorites);
}

bool FarGitHubPanel::IsFavorite(const std::wstring& fullName) const
{
    return std::find(Favorites.begin(), Favorites.end(), fullName) != Favorites.end();
}

bool FarGitHubPanel::ToggleFavorite(const std::wstring& fullName)
{
    GitHubSettings settings;
    const auto it = std::find(Favorites.begin(), Favorites.end(), fullName);
    if (it == Favorites.end())
        Favorites.push_back(fullName);
    else
        Favorites.erase(it);

    if (!settings.SaveFavorites(Favorites))
    {
        Error = L"Unable to save favorite repositories.";
        return false;
    }
    return true;
}

void FarGitHubPanel::ShowError(const std::wstring& title) const
{
    const wchar_t* text[] = { title.c_str(), Error.c_str() };
    GPluginInfo.Message(&MainGuid, nullptr, FMSG_ERRORTYPE | FMSG_MB_OK, nullptr, text, 2, 1);
}

bool FarGitHubPanel::ReloadRepositories()
{
    Entries.clear();
    Repositories.clear();
    if (Token.empty())
    {
        Error = L"GitHub token is not configured. Open F11 -> GitHub -> Settings.";
        return false;
    }

    GitHubClient client(Token);
    if (!client.GetRepositories(Repositories, Error))
        return false;

    std::vector<const GitHubRepository*> matches;
    for (const auto& repository : Repositories)
    {
        if (ContainsInsensitive(repository.Name, SearchText) ||
            ContainsInsensitive(repository.FullName, SearchText))
        {
            matches.push_back(&repository);
        }
    }

    std::stable_sort(matches.begin(), matches.end(), [this](const auto* left, const auto* right)
    {
        const bool leftFavorite = IsFavorite(left->FullName);
        const bool rightFavorite = IsFavorite(right->FullName);
        if (leftFavorite != rightFavorite)
            return leftFavorite > rightFavorite;
        return _wcsicmp(left->Name.c_str(), right->Name.c_str()) < 0;
    });

    Entries.reserve(matches.size());
    for (const auto* repository : matches)
    {
        GitHubEntry entry;
        entry.Name = repository->Name;
        entry.Type = L"repo";
        entry.Sha = repository->FullName;
        Entries.push_back(entry);
    }
    return true;
}

bool FarGitHubPanel::ReloadBranches()
{
    Branches.clear();
    if (Repository.empty())
        return false;

    GitHubClient client(Token, Repository);
    if (!client.GetBranches(Branches, Error))
        return false;
    return true;
}

bool FarGitHubPanel::Reload()
{
    if (Repository.empty())
        return ReloadRepositories();

    if (Token.empty())
    {
        Error = L"GitHub token is not configured. Open F11 -> GitHub -> Settings.";
        Entries.clear();
        return false;
    }

    GitHubClient client(Token, Repository, CurrentBranch);
    return client.GetEntries(CurrentPath, Entries, Error);
}

void FarGitHubPanel::UpdatePanel() const
{
    GPluginInfo.PanelControl(PANEL_ACTIVE, FCTL_UPDATEPANEL, 0, nullptr);
}

bool FarGitHubPanel::SearchRepositories()
{
    if (!Repository.empty())
        return false;

    wchar_t buffer[1024] = {};
    if (!SearchText.empty())
        wcsncpy_s(buffer, SearchText.c_str(), _TRUNCATE);

    const intptr_t result = GPluginInfo.InputBox(
        &MainGuid,
        &SearchDialogGuid,
        L"GitHub",
        L"Search repositories",
        L"GitHubRepositorySearch",
        buffer,
        buffer,
        std::size(buffer),
        nullptr,
        FIB_ENABLEEMPTY);

    if (result != TRUE)
        return false;

    SearchText = buffer;
    if (!ReloadRepositories())
    {
        ShowError();
        return false;
    }
    UpdatePanel();
    return true;
}

bool FarGitHubPanel::SelectBranch()
{
    if (Repository.empty())
        return false;

    if (!ReloadBranches())
    {
        ShowError();
        return false;
    }
    if (Branches.empty())
    {
        Error = L"No branches found.";
        ShowError();
        return false;
    }

    std::vector<std::wstring> labels;
    std::vector<FarMenuItem> items(Branches.size());
    labels.reserve(Branches.size());

    for (size_t i = 0; i < Branches.size(); ++i)
    {
        labels.push_back(Branches[i].Name + (Branches[i].Protected ? L" [protected]" : L""));
        items[i] = {};
        items[i].Flags = Branches[i].Name == CurrentBranch ? MIF_SELECTED : MIF_NONE;
        items[i].Text = labels.back().c_str();
    }

    const intptr_t selected = GPluginInfo.Menu(
        &MainGuid,
        &BranchMenuGuid,
        -1,
        -1,
        0,
        FMENU_AUTOHIGHLIGHT,
        L"GitHub branches",
        L"Ctrl+Shift+B",
        nullptr,
        nullptr,
        nullptr,
        items.data(),
        items.size());

    if (selected < 0 || static_cast<size_t>(selected) >= Branches.size())
        return false;

    const std::wstring branch = Branches[static_cast<size_t>(selected)].Name;
    if (branch == CurrentBranch)
        return false;

    CurrentBranch = branch;
    CurrentPath.clear();
    if (!Reload())
    {
        ShowError();
        return false;
    }
    UpdatePanel();
    return true;
}

intptr_t FarGitHubPanel::ProcessInput(const INPUT_RECORD& record)
{
    if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown)
        return FALSE;

    const auto& key = record.Event.KeyEvent;
    const DWORD state = key.dwControlKeyState;
    const bool ctrl = (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    const bool shift = (state & SHIFT_PRESSED) != 0;
    if (!ctrl)
        return FALSE;

    if (key.wVirtualKeyCode == 'B' && shift && !Repository.empty())
        return SelectBranch() ? TRUE : FALSE;

    if (key.wVirtualKeyCode == 'F' && !shift && Repository.empty())
        return SearchRepositories() ? TRUE : FALSE;

    if (key.wVirtualKeyCode == 'B' && !shift && Repository.empty())
    {
        const size_t size = static_cast<size_t>(GPluginInfo.PanelControl(PANEL_ACTIVE, FCTL_GETCURRENTPANELITEM, 0, nullptr));
        if (!size)
            return FALSE;

        auto* item = static_cast<PluginPanelItem*>(malloc(size));
        if (!item)
            return FALSE;

        FarGetPluginPanelItem request = { sizeof(request), size, item };
        const bool ok = GPluginInfo.PanelControl(PANEL_ACTIVE, FCTL_GETCURRENTPANELITEM, 0, &request) != FALSE;
        if (!ok)
        {
            free(item);
            return FALSE;
        }

        const std::wstring name = item->FileName ? item->FileName : L"";
        free(item);

        for (const auto& repository : Repositories)
        {
            if (_wcsicmp(repository.Name.c_str(), name.c_str()) == 0)
            {
                if (!ToggleFavorite(repository.FullName))
                {
                    ShowError();
                    return FALSE;
                }
                ReloadRepositories();
                UpdatePanel();
                return TRUE;
            }
        }
    }

    return FALSE;
}

std::wstring FarGitHubPanel::FullPath(const std::wstring& name) const
{
    return CurrentPath.empty() ? name : CurrentPath + L"/" + name;
}

intptr_t FarGitHubPanel::GetFindData(PluginPanelItem** items, size_t* count, OPERATION_MODES)
{
    if (!Reload())
    {
        ShowError();
        return -1;
    }

    *count = Entries.size();
    if (!*count) { *items = nullptr; return 0; }

    auto* result = static_cast<PluginPanelItem*>(calloc(*count, sizeof(PluginPanelItem)));
    if (!result) return -1;

    for (size_t i = 0; i < *count; ++i)
    {
        result[i].FileName = _wcsdup(Entries[i].Name.c_str());
        result[i].FileSize = Entries[i].Size;
        result[i].AllocationSize = Entries[i].Size;
        if (Entries[i].Type == L"dir" || Entries[i].Type == L"repo")
            result[i].FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        result[i].UserData.Data = _wcsdup(Entries[i].Sha.c_str());

        if (Repository.empty())
        {
            auto** columns = static_cast<const wchar_t**>(calloc(1, sizeof(const wchar_t*)));
            if (columns)
            {
                columns[0] = _wcsdup(IsFavorite(Entries[i].Sha) ? L"*" : L"");
                result[i].CustomColumnData = columns;
                result[i].CustomColumnNumber = 1;
            }
        }
    }
    *items = result;
    return 0;
}

void FarGitHubPanel::FreeFindData(PluginPanelItem* items, size_t count)
{
    if (!items) return;
    for (size_t i = 0; i < count; ++i)
    {
        free(const_cast<wchar_t*>(items[i].FileName));
        free(items[i].UserData.Data);

        if (items[i].CustomColumnData)
        {
            for (size_t j = 0; j < items[i].CustomColumnNumber; ++j)
                free(const_cast<wchar_t*>(items[i].CustomColumnData[j]));
            free(const_cast<wchar_t**>(items[i].CustomColumnData));
        }
    }
    free(items);
}

void FarGitHubPanel::GetOpenPanelInfo(OpenPanelInfo* info)
{
    info->StructSize = sizeof(*info);
    info->Flags = OPIF_ADDDOTS | OPIF_SHORTCUT;
    info->CurDir = CurrentPath.c_str();

    static std::wstring title;
    if (Repository.empty())
    {
        info->HostFile = L"GitHub";
        title = SearchText.empty() ? L"GitHub repositories" : L"GitHub repositories: " + SearchText;
        info->Format = L"N,C0";
    }
    else
    {
        info->HostFile = Repository.c_str();
        title = L"GitHub: " + Repository + L" [" + CurrentBranch + L"]";
        info->Format = L"N";
    }
    info->PanelTitle = title.c_str();
}

intptr_t FarGitHubPanel::SetDirectory(const wchar_t* directory, OPERATION_MODES)
{
    if (!directory) return FALSE;
    std::wstring dir(directory);

    if (dir == L"\\" || dir.empty())
    {
        if (!Repository.empty())
        {
            Repository.clear();
            CurrentBranch.clear();
            DefaultBranch.clear();
            CurrentPath.clear();
            Branches.clear();
            return Reload() ? TRUE : FALSE;
        }
        CurrentPath.clear();
        return Reload() ? TRUE : FALSE;
    }

    if (dir == L"..")
    {
        if (!CurrentPath.empty())
        {
            const auto pos = CurrentPath.find_last_of(L'/');
            CurrentPath = pos == std::wstring::npos ? L"" : CurrentPath.substr(0, pos);
            return Reload() ? TRUE : FALSE;
        }
        if (!Repository.empty())
        {
            Repository.clear();
            CurrentBranch.clear();
            DefaultBranch.clear();
            Branches.clear();
            return Reload() ? TRUE : FALSE;
        }
        return TRUE;
    }

    if (Repository.empty())
    {
        for (const auto& repository : Repositories)
        {
            if (_wcsicmp(repository.Name.c_str(), dir.c_str()) == 0)
            {
                Repository = repository.FullName;
                DefaultBranch = repository.DefaultBranch;
                CurrentBranch = DefaultBranch;
                CurrentPath.clear();
                if (!ReloadBranches())
                {
                    Repository.clear();
                    CurrentBranch.clear();
                    DefaultBranch.clear();
                    ShowError();
                    return FALSE;
                }
                return Reload() ? TRUE : FALSE;
            }
        }
        Error = L"Repository not found: " + dir;
        ShowError();
        return FALSE;
    }

    if (!CurrentPath.empty()) CurrentPath += L'/';
    CurrentPath += dir;
    return Reload() ? TRUE : FALSE;
}

bool FarGitHubPanel::EditFile(const std::wstring& path)
{
    GitHubClient client(Token, Repository, CurrentBranch);
    std::string content;
    std::wstring sha;
    if (!client.GetFile(path, content, sha, Error))
    {
        ShowError();
        return false;
    }

    wchar_t tempPath[MAX_PATH] = {};
    wchar_t tempFile[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempPath);
    if (!GetTempFileNameW(tempPath, L"gh", 0, tempFile))
    {
        Error = L"Unable to create temporary file";
        ShowError();
        return false;
    }

    {
        std::ofstream file(tempFile, std::ios::binary);
        if (!file)
        {
            DeleteFileW(tempFile);
            Error = L"Unable to create temporary file";
            ShowError();
            return false;
        }
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    const intptr_t rc = GPluginInfo.Editor(tempFile, path.c_str(), 0, 0, -1, -1, 0, 1, 1, CP_DEFAULT);
    bool success = true;
    if (rc == EEC_MODIFIED)
    {
        std::ifstream file(tempFile, std::ios::binary);
        std::string updated((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (!client.PutFile(path, updated, sha, L"Update " + path, Error))
        {
            ShowError();
            success = false;
        }
    }

    DeleteFileW(tempFile);
    return success;
}

intptr_t FarGitHubPanel::ProcessHostFile(PluginPanelItem* items, size_t count, OPERATION_MODES)
{
    if (!items || count == 0 || Repository.empty()) return 0;
    if (items[0].FileAttributes & FILE_ATTRIBUTE_DIRECTORY) return 0;
    return EditFile(FullPath(items[0].FileName)) ? TRUE : FALSE;
}

intptr_t FarGitHubPanel::MakeDirectory(const wchar_t* name, OPERATION_MODES)
{
    if (!name || !*name || Repository.empty()) return FALSE;

    GitHubClient client(Token, Repository, CurrentBranch);
    if (!client.CreateDirectoryEntry(FullPath(name), L"Create directory " + std::wstring(name), Error))
    {
        ShowError();
        return FALSE;
    }
    Reload();
    return TRUE;
}

intptr_t FarGitHubPanel::PutFiles(PluginPanelItem* items, size_t count, const wchar_t* sourcePath, OPERATION_MODES)
{
    if (!items || !count || !sourcePath || Repository.empty()) return FALSE;

    GitHubClient client(Token, Repository, CurrentBranch);
    for (size_t i = 0; i < count; ++i)
    {
        if (items[i].FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        const std::wstring local = std::wstring(sourcePath) + items[i].FileName;
        std::ifstream file(local, std::ios::binary);
        if (!file)
        {
            Error = L"Unable to read local file: " + local;
            ShowError();
            return FALSE;
        }
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        const std::wstring remote = FullPath(items[i].FileName);
        std::wstring sha;
        std::string oldContent;
        client.GetFile(remote, oldContent, sha, Error);
        if (!client.PutFile(remote, content, sha, L"Upload " + std::wstring(items[i].FileName), Error))
        {
            ShowError();
            return FALSE;
        }
    }
    Reload();
    return TRUE;
}
