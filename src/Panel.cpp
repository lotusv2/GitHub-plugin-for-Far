#include "Panel.hpp"
#include "Plugin.hpp"

#include <windows.h>
#include <fstream>
#include <algorithm>

FarGitHubPanel::FarGitHubPanel()
{
    wchar_t buffer[4096] = {};
    DWORD size = GetEnvironmentVariableW(L"FAR_GITHUB_TOKEN", buffer, 4096);
    if (size) Token.assign(buffer, size);
    size = GetEnvironmentVariableW(L"FAR_GITHUB_REPOSITORY", buffer, 4096);
    if (size) Repository.assign(buffer, size);
    Reload();
}

bool FarGitHubPanel::Reload()
{
    if (Token.empty() || Repository.empty())
    {
        Error = L"Set FAR_GITHUB_TOKEN and FAR_GITHUB_REPOSITORY environment variables.";
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
        const wchar_t* text[] = { L"GitHub plugin", Error.c_str() };
        GPluginInfo.Message(&MainGuid, nullptr, FMSG_WARNING | FMSG_MB_OK, nullptr, text, 2, 1);
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
        if (Entries[i].Type == L"dir") result[i].FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
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
    info->HostFile = Repository.c_str();
    static std::wstring title;
    title = L"GitHub: " + Repository;
    info->PanelTitle = title.c_str();
    info->Format = L"gh";
}

intptr_t FarGitHubPanel::SetDirectory(const wchar_t* directory, OPERATION_MODES)
{
    if (!directory) return FALSE;
    std::wstring dir(directory);
    if (dir == L"..")
    {
        auto pos = CurrentPath.find_last_of(L'/');
        CurrentPath = pos == std::wstring::npos ? L"" : CurrentPath.substr(0, pos);
    }
    else if (dir == L"\\" || dir.empty())
    {
        CurrentPath.clear();
    }
    else
    {
        if (!CurrentPath.empty()) CurrentPath += L'/';
        CurrentPath += dir;
    }
    return Reload() ? TRUE : FALSE;
}

bool FarGitHubPanel::EditFile(const std::wstring& path)
{
    GitHubClient client(Token, Repository);
    std::string content;
    std::wstring sha, error;
    if (!client.GetFile(path, content, sha, error))
    {
        const wchar_t* text[] = { L"GitHub", error.c_str() };
        GPluginInfo.Message(&MainGuid, nullptr, FMSG_ERRORTYPE | FMSG_MB_OK, nullptr, text, 2, 1);
        return false;
    }

    wchar_t tempPath[MAX_PATH] = {};
    wchar_t tempFile[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempPath);
    GetTempFileNameW(tempPath, L"gh", 0, tempFile);
    {
        std::ofstream file(tempFile, std::ios::binary);
        file.write(content.data(), (std::streamsize)content.size());
    }

    intptr_t rc = GPluginInfo.Editor(tempFile, path.c_str(), 0, 0, -1, -1, 0, 1, 1, CP_DEFAULT);
    if (rc == EEC_MODIFIED)
    {
        std::ifstream file(tempFile, std::ios::binary);
        std::string updated((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (!client.PutFile(path, updated, sha, L"Update " + path, error))
        {
            const wchar_t* text[] = { L"GitHub", error.c_str() };
            GPluginInfo.Message(&MainGuid, nullptr, FMSG_ERRORTYPE | FMSG_MB_OK, nullptr, text, 2, 1);
            DeleteFileW(tempFile);
            return false;
        }
    }
    DeleteFileW(tempFile);
    return true;
}

intptr_t FarGitHubPanel::ProcessHostFile(PluginPanelItem* items, size_t count, OPERATION_MODES)
{
    if (!items || count == 0) return -1;
    if (items[0].FileAttributes & FILE_ATTRIBUTE_DIRECTORY) return -1;
    return EditFile(FullPath(items[0].FileName)) ? TRUE : -1;
}

intptr_t FarGitHubPanel::MakeDirectory(const wchar_t* name, OPERATION_MODES)
{
    if (!name || !*name) return FALSE;
    GitHubClient client(Token, Repository);
    std::wstring error;
    if (!client.CreateDirectory(FullPath(name), L"Create directory " + std::wstring(name), error))
    {
        const wchar_t* text[] = { L"GitHub", error.c_str() };
        GPluginInfo.Message(&MainGuid, nullptr, FMSG_ERRORTYPE | FMSG_MB_OK, nullptr, text, 2, 1);
        return FALSE;
    }
    Reload();
    return TRUE;
}
