#include "Panel.hpp"
#include "Plugin.hpp"
#include "Settings.hpp"

#include <windows.h>
#include <fstream>
#include <iterator>

FarGitHubPanel::FarGitHubPanel()
{
    GitHubSettings settings;
    settings.LoadToken(Token);
    Reload();
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

    Entries.reserve(Repositories.size());
    for (const auto& repository : Repositories)
    {
        GitHubEntry entry;
        entry.Name = repository.Name;
        entry.Type = L"repo";
        entry.Sha = repository.FullName;
        Entries.push_back(entry);
    }
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

    GitHubClient client(Token, Repository);
    return client.GetEntries(CurrentPath, Entries, Error);
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
        title = L"GitHub repositories";
    }
    else
    {
        info->HostFile = Repository.c_str();
        title = L"GitHub: " + Repository;
    }
    info->PanelTitle = title.c_str();
    info->Format = L"gh";
}

intptr_t FarGitHubPanel::SetDirectory(const wchar_t* directory, OPERATION_MODES)
{
    if (!directory) return FALSE;
    const std::wstring dir(directory);

    if (dir == L"\\" || dir.empty())
    {
        if (!Repository.empty())
        {
            Repository.clear();
            CurrentPath.clear();
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
                CurrentPath.clear();
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
    GitHubClient client(Token, Repository);
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

    GitHubClient client(Token, Repository);
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

    GitHubClient client(Token, Repository);
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
