#include "Panel.hpp"
#include "Plugin.hpp"
#include "Settings.hpp"
#include "SaveProgress.hpp"

#include <windows.h>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <cwchar>
#include <functional>

namespace
{
const GUID SearchDialogGuid = { 0x7a5f3c21, 0x6b4d, 0x4f92, { 0x8c, 0x31, 0x45, 0x72, 0x9a, 0x16, 0x3e, 0x54 } };
const GUID BranchMenuGuid = { 0x6d8b2a14, 0x4f31, 0x47c6, { 0x91, 0x28, 0x5a, 0x73, 0xb4, 0x0c, 0x2e, 0x61 } };
const GUID SaveDialogGuid = { 0x4f1a9c72, 0x5e3d, 0x4b81, { 0x93, 0x27, 0x6a, 0x41, 0x8d, 0x2e, 0x57, 0x19 } };

bool ContainsInsensitive(const std::wstring& value, const std::wstring& query)
{
    if (query.empty()) return true;
    std::wstring left = value;
    std::wstring right = query;
    std::transform(left.begin(), left.end(), left.begin(), towlower);
    std::transform(right.begin(), right.end(), right.begin(), towlower);
    return left.find(right) != std::wstring::npos;
}

bool IsNotFound(const std::wstring& error)
{
    return error.find(L"HTTP 404") != std::wstring::npos;
}

std::wstring JoinLocalPath(const std::wstring& base, const std::wstring& name)
{
    if (base.empty()) return name;
    if (base.back() == L'\\') return base + name;
    return base + L'\\' + name;
}

intptr_t WINAPI SavingDialogProc(HANDLE hDlg, intptr_t message, intptr_t param1, void* param2)
{
    if (message == DN_INITDIALOG) return TRUE;
    return GPluginInfo.DefDlgProc(hDlg, message, param1, param2);
}

HANDLE ShowSavingDialog()
{
    FarDialogItem items[] =
    {
        { DI_DOUBLEBOX, 0, 0, 34, 4, { 0 }, nullptr, nullptr, DIF_NONE, L"GitHub", 0, 0, { 0, 0 } },
        { DI_TEXT,      2, 1, 32, 1, { 0 }, nullptr, nullptr, DIF_NONE, L"Saving changes to GitHub...", 0, 0, { 0, 0 } },
        { DI_TEXT,      2, 2, 32, 2, { 0 }, nullptr, nullptr, DIF_NONE, L"Please wait.", 0, 0, { 0, 0 } }
    };
    return GPluginInfo.DialogInit(&MainGuid, &SaveDialogGuid, -1, -1, 34, 4, nullptr, items, std::size(items), 0, FDLG_NONMODAL, SavingDialogProc, nullptr);
}

void CloseSavingDialog(HANDLE hDlg)
{
    if (hDlg != nullptr && hDlg != INVALID_HANDLE_VALUE)
        GPluginInfo.SendDlgMessage(hDlg, DM_CLOSE, 0, nullptr);
}
}

FarGitHubPanel::FarGitHubPanel() { ReloadSettings(); }

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
    if (it == Favorites.end()) Favorites.push_back(fullName);
    else Favorites.erase(it);
    if (!settings.SaveFavorites(Favorites)) { Error = L"Unable to save favorite repositories."; return false; }
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
    if (Token.empty()) { Error = L"GitHub token is not configured. Open F11 -> GitHub -> Settings."; return false; }
    GitHubClient client(Token);
    if (!client.GetRepositories(Repositories, Error)) return false;
    std::vector<const GitHubRepository*> matches;
    for (const auto& repository : Repositories)
        if (ContainsInsensitive(repository.Name, SearchText) || ContainsInsensitive(repository.FullName, SearchText)) matches.push_back(&repository);
    std::stable_sort(matches.begin(), matches.end(), [this](const auto* left, const auto* right)
    {
        const bool lf = IsFavorite(left->FullName), rf = IsFavorite(right->FullName);
        if (lf != rf) return lf > rf;
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
    if (Repository.empty()) return false;
    GitHubClient client(Token, Repository);
    return client.GetBranches(Branches, Error);
}

bool FarGitHubPanel::Reload()
{
    if (Repository.empty()) return ReloadRepositories();
    if (Token.empty()) { Error = L"GitHub token is not configured. Open F11 -> GitHub -> Settings."; Entries.clear(); return false; }
    GitHubClient client(Token, Repository, CurrentBranch);
    return client.GetEntries(CurrentPath, Entries, Error);
}

void FarGitHubPanel::UpdatePanel() const
{
    GPluginInfo.PanelControl(PANEL_ACTIVE, FCTL_UPDATEPANEL, 0, nullptr);
}

bool FarGitHubPanel::SearchRepositories()
{
    if (!Repository.empty()) return false;
    wchar_t buffer[1024] = {};
    if (!SearchText.empty()) wcsncpy_s(buffer, SearchText.c_str(), _TRUNCATE);
    if (GPluginInfo.InputBox(&MainGuid, &SearchDialogGuid, L"GitHub", L"Search repositories", L"GitHubRepositorySearch", buffer, buffer, std::size(buffer), nullptr, FIB_ENABLEEMPTY) != TRUE) return false;
    SearchText = buffer;
    if (!ReloadRepositories()) { ShowError(); return false; }
    UpdatePanel();
    return true;
}

bool FarGitHubPanel::SelectBranch()
{
    if (Repository.empty()) return false;
    if (!ReloadBranches()) { ShowError(); return false; }
    if (Branches.empty()) { Error = L"No branches found."; ShowError(); return false; }
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
    const intptr_t selected = GPluginInfo.Menu(&MainGuid, &BranchMenuGuid, -1, -1, 0, FMENU_AUTOHIGHLIGHT, L"GitHub branches", L"Ctrl+Shift+B", nullptr, nullptr, nullptr, items.data(), items.size());
    if (selected < 0 || static_cast<size_t>(selected) >= Branches.size()) return false;
    const std::wstring branch = Branches[static_cast<size_t>(selected)].Name;
    if (branch == CurrentBranch) return false;
    CurrentBranch = branch;
    CurrentPath.clear();
    if (!Reload()) { ShowError(); return false; }
    UpdatePanel();
    return true;
}

intptr_t FarGitHubPanel::ProcessInput(const INPUT_RECORD& record)
{
    if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) return FALSE;
    const auto& key = record.Event.KeyEvent;
    const DWORD state = key.dwControlKeyState;
    const bool ctrl = (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    const bool shift = (state & SHIFT_PRESSED) != 0;
    if (!ctrl && !shift && key.wVirtualKeyCode == VK_F6 && !Repository.empty()) return ProcessRenameInput(this);
    if (ctrl && key.wVirtualKeyCode == 'B' && shift && !Repository.empty()) return SelectBranch() ? TRUE : FALSE;
    if (ctrl && key.wVirtualKeyCode == 'F' && !shift && Repository.empty()) return SearchRepositories() ? TRUE : FALSE;
    if (ctrl && key.wVirtualKeyCode == 'B' && !shift && Repository.empty())
    {
        const size_t size = static_cast<size_t>(GPluginInfo.PanelControl(PANEL_ACTIVE, FCTL_GETCURRENTPANELITEM, 0, nullptr));
        if (!size) return FALSE;
        auto* item = static_cast<PluginPanelItem*>(malloc(size));
        if (!item) return FALSE;
        FarGetPluginPanelItem request = { sizeof(request), size, item };
        const bool ok = GPluginInfo.PanelControl(PANEL_ACTIVE, FCTL_GETCURRENTPANELITEM, 0, &request) != FALSE;
        if (!ok) { free(item); return FALSE; }
        const std::wstring name = item->FileName ? item->FileName : L"";
        free(item);
        for (const auto& repository : Repositories)
        {
            if (_wcsicmp(repository.Name.c_str(), name.c_str()) == 0)
            {
                if (!ToggleFavorite(repository.FullName)) { ShowError(); return FALSE; }
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
    if (!Reload()) { ShowError(); return -1; }
    *count = Entries.size();
    if (!*count) { *items = nullptr; return 0; }
    auto* result = static_cast<PluginPanelItem*>(calloc(*count, sizeof(PluginPanelItem)));
    if (!result) return -1;
    for (size_t i = 0; i < *count; ++i)
    {
        result[i].FileName = _wcsdup(Entries[i].Name.c_str());
        result[i].FileSize = Entries[i].Size;
        result[i].AllocationSize = Entries[i].Size;
        if (Entries[i].Type == L"dir" || Entries[i].Type == L"repo") result[i].FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
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
            for (size_t j = 0; j < items[i].CustomColumnNumber; ++j) free(const_cast<wchar_t*>(items[i].CustomColumnData[j]));
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
    const std::wstring dir(directory);

    if (dir == L"\\" || dir.empty())
    {
        if (!Repository.empty())
        {
            const std::wstring oldRepository = Repository;
            const std::wstring oldBranch = CurrentBranch;
            const std::wstring oldDefaultBranch = DefaultBranch;
            const std::wstring oldPath = CurrentPath;
            const std::vector<GitHubBranch> oldBranches = Branches;
            Repository.clear();
            CurrentBranch.clear();
            DefaultBranch.clear();
            CurrentPath.clear();
            Branches.clear();
            if (Reload()) return TRUE;
            Repository = oldRepository;
            CurrentBranch = oldBranch;
            DefaultBranch = oldDefaultBranch;
            CurrentPath = oldPath;
            Branches = oldBranches;
            return FALSE;
        }
        const std::wstring oldPath = CurrentPath;
        CurrentPath.clear();
        if (Reload()) return TRUE;
        CurrentPath = oldPath;
        return FALSE;
    }

    if (dir == L"..")
    {
        if (!CurrentPath.empty())
        {
            const std::wstring oldPath = CurrentPath;
            const auto pos = CurrentPath.find_last_of(L'/');
            CurrentPath = pos == std::wstring::npos ? L"" : CurrentPath.substr(0, pos);
            if (Reload()) return TRUE;
            CurrentPath = oldPath;
            return FALSE;
        }
        if (!Repository.empty())
        {
            const std::wstring oldRepository = Repository;
            const std::wstring oldBranch = CurrentBranch;
            const std::wstring oldDefaultBranch = DefaultBranch;
            const std::vector<GitHubBranch> oldBranches = Branches;
            Repository.clear();
            CurrentBranch.clear();
            DefaultBranch.clear();
            CurrentPath.clear();
            Branches.clear();
            if (Reload()) return TRUE;
            Repository = oldRepository;
            CurrentBranch = oldBranch;
            DefaultBranch = oldDefaultBranch;
            CurrentPath = oldPath;
            Branches = oldBranches;
            return FALSE;
        }
        return TRUE;
    }

    if (Repository.empty())
    {
        for (const auto& repository : Repositories)
        {
            if (_wcsicmp(repository.Name.c_str(), dir.c_str()) == 0)
            {
                const std::wstring oldRepository = Repository;
                const std::wstring oldBranch = CurrentBranch;
                const std::wstring oldDefaultBranch = DefaultBranch;
                const std::wstring oldPath = CurrentPath;
                const std::vector<GitHubBranch> oldBranches = Branches;
                Repository = repository.FullName;
                DefaultBranch = repository.DefaultBranch;
                CurrentBranch = DefaultBranch;
                CurrentPath.clear();
                if (!ReloadBranches())
                {
                    Repository = oldRepository;
                    CurrentBranch = oldBranch;
                    DefaultBranch = oldDefaultBranch;
                    CurrentPath = oldPath;
                    Branches = oldBranches;
                    ShowError();
                    return FALSE;
                }
                if (Reload()) return TRUE;
                Repository = oldRepository;
                CurrentBranch = oldBranch;
                DefaultBranch = oldDefaultBranch;
                CurrentPath = oldPath;
                Branches = oldBranches;
                return FALSE;
            }
        }
        Error = L"Repository not found: " + dir;
        ShowError();
        return FALSE;
    }

    const std::wstring oldPath = CurrentPath;
    if (!CurrentPath.empty()) CurrentPath += L'/';
    CurrentPath += dir;
    if (Reload()) return TRUE;
    CurrentPath = oldPath;
    return FALSE;
}

bool FarGitHubPanel::EditFile(const std::wstring& path)
{
    GitHubClient client(Token, Repository, CurrentBranch);
    std::string content;
    std::wstring sha;
    if (!client.GetFile(path, content, sha, Error)) { ShowError(); return false; }
    wchar_t tempPath[MAX_PATH] = {}, tempFile[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempPath);
    if (!GetTempFileNameW(tempPath, L"gh", 0, tempFile)) { Error = L"Unable to create temporary file"; ShowError(); return false; }
    { std::ofstream file(tempFile, std::ios::binary); if (!file) { DeleteFileW(tempFile); Error = L"Unable to create temporary file"; ShowError(); return false; } file.write(content.data(), static_cast<std::streamsize>(content.size())); }
    BeginGitHubEditorSession(tempFile, path, sha, Token, Repository, CurrentBranch);
    const intptr_t rc = GPluginInfo.Editor(tempFile, path.c_str(), 0, 0, -1, -1, 0, 1, 1, CP_DEFAULT);
    EndGitHubEditorSession();
    bool success = true;
    if (rc == EEC_MODIFIED)
    {
        std::ifstream file(tempFile, std::ios::binary);
        std::string updated((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (!client.PutFile(path, updated, sha, L"Update " + path, Error)) { ShowError(); success = false; }
    }
    DeleteFileW(tempFile);
    return success;
}

intptr_t FarGitHubPanel::ProcessHostFile(PluginPanelItem* items, size_t count, OPERATION_MODES)
{
    if (!items || count == 0 || Repository.empty() || (items[0].FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return 0;
    return EditFile(FullPath(items[0].FileName)) ? TRUE : FALSE;
}

intptr_t FarGitHubPanel::MakeDirectory(const wchar_t* name, OPERATION_MODES)
{
    if (!name || !*name || Repository.empty()) return FALSE;
    GitHubClient client(Token, Repository, CurrentBranch);
    if (!client.CreateDirectoryEntry(FullPath(name), L"Create directory " + std::wstring(name), Error)) { ShowError(); return FALSE; }
    Reload();
    return TRUE;
}

intptr_t FarGitHubPanel::PutFiles(PluginPanelItem* items, size_t count, const wchar_t* sourcePath, OPERATION_MODES)
{
    if (!items || !count || !sourcePath || Repository.empty()) return FALSE;
    GitHubClient client(Token, Repository, CurrentBranch);
    std::function<bool(const std::wstring&, const std::wstring&)> uploadEntry;
    uploadEntry = [&](const std::wstring& localPath, const std::wstring& remotePath) -> bool
    {
        const DWORD attributes = GetFileAttributesW(localPath.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            Error = L"Unable to access local path: " + localPath;
            return false;
        }
        if (attributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            WIN32_FIND_DATAW data = {};
            const std::wstring pattern = JoinLocalPath(localPath, L"*");
            HANDLE handle = FindFirstFileW(pattern.c_str(), &data);
            if (handle == INVALID_HANDLE_VALUE)
            {
                Error = L"Unable to enumerate local directory: " + localPath;
                return false;
            }
            bool hasEntries = false;
            bool success = true;
            do
            {
                const std::wstring name = data.cFileName;
                if (name == L"." || name == L"..") continue;
                hasEntries = true;
                const std::wstring childLocal = JoinLocalPath(localPath, name);
                const std::wstring childRemote = remotePath + L"/" + name;
                if (!uploadEntry(childLocal, childRemote)) { success = false; break; }
            }
            while (FindNextFileW(handle, &data));
            FindClose(handle);
            if (!success) return false;
            if (!hasEntries) return client.CreateDirectoryEntry(remotePath, L"Create directory " + remotePath, Error);
            return true;
        }
        std::ifstream file(localPath, std::ios::binary);
        if (!file) { Error = L"Unable to read local file: " + localPath; return false; }
        const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::wstring sha;
        std::string oldContent;
        std::wstring lookupError;
        if (!client.GetFile(remotePath, oldContent, sha, lookupError))
        {
            if (!IsNotFound(lookupError)) { Error = lookupError.empty() ? L"Unable to check remote file." : lookupError; return false; }
            sha.clear();
        }
        if (!client.PutFile(remotePath, content, sha, L"Upload " + remotePath, Error)) return false;
        return true;
    };
    for (size_t i = 0; i < count; ++i)
    {
        const std::wstring local = JoinLocalPath(sourcePath, items[i].FileName);
        const std::wstring remote = FullPath(items[i].FileName);
        if (!uploadEntry(local, remote)) { ShowError(); return FALSE; }
        items[i].Flags &= ~PPIF_SELECTED;
    }
    Reload();
    return TRUE;
}

intptr_t FarGitHubPanel::GetFiles(PluginPanelItem* items, size_t count, bool move, const wchar_t* destinationPath, OPERATION_MODES)
{
    if (!items || !count || !destinationPath || Repository.empty()) return FALSE;
    GitHubClient client(Token, Repository, CurrentBranch);
    std::function<bool(const std::wstring&, const std::wstring&)> downloadEntry;
    downloadEntry = [&](const std::wstring& remote, const std::wstring& local) -> bool
    {
        std::vector<GitHubEntry> children;
        std::wstring probeError;
        if (!client.GetEntries(remote, children, probeError))
        {
            if (!IsNotFound(probeError)) { Error = probeError.empty() ? L"Unable to determine remote entry type." : probeError; return false; }
            std::string content;
            std::wstring sha;
            if (!client.GetFile(remote, content, sha, Error)) return false;
            std::ofstream file(local, std::ios::binary);
            if (!file) { Error = L"Unable to create local file: " + local; return false; }
            file.write(content.data(), static_cast<std::streamsize>(content.size()));
            if (!file) { Error = L"Unable to write local file: " + local; return false; }
            return true;
        }
        if (!CreateDirectoryW(local.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) { Error = L"Unable to create local directory: " + local; return false; }
        for (const auto& child : children)
            if (!downloadEntry(remote + L"/" + child.Name, JoinLocalPath(local, child.Name))) return false;
        return true;
    };
    for (size_t i = 0; i < count; ++i)
    {
        const std::wstring remote = FullPath(items[i].FileName);
        const std::wstring local = JoinLocalPath(destinationPath, items[i].FileName);
        if (!downloadEntry(remote, local)) { ShowError(); return FALSE; }
        if (move)
        {
            Error.clear();
            if (!DeleteFiles(&items[i], 1, OPM_SILENT)) return FALSE;
        }
        items[i].Flags &= ~PPIF_SELECTED;
    }
    Reload();
    return TRUE;
}

bool FarGitHubPanel::RenameEntry(const std::wstring& oldPath, const std::wstring& newPath)
{
    GitHubClient client(Token, Repository, CurrentBranch);
    std::function<bool(const std::wstring&, const std::wstring&)> renameEntry;
    renameEntry = [&](const std::wstring& oldName, const std::wstring& newName) -> bool
    {
        std::vector<GitHubEntry> children;
        std::wstring listError;
        if (client.GetEntries(oldName, children, listError))
        {
            if (children.empty()) return client.CreateDirectoryEntry(newName, L"Rename directory " + oldName, Error);
            for (const auto& child : children)
                if (!renameEntry(oldName + L"/" + child.Name, newName + L"/" + child.Name)) return false;
            for (const auto& child : children)
            {
                const std::wstring path = oldName + L"/" + child.Name;
                std::string ignored;
                std::wstring sha;
                if (!client.GetFile(path, ignored, sha, Error)) return false;
                if (!client.DeleteFile(path, sha, L"Rename " + oldName, Error)) return false;
            }
            return true;
        }
        if (!IsNotFound(listError)) { Error = listError.empty() ? L"Unable to read rename source." : listError; return false; }
        std::string content;
        std::wstring sha;
        if (!client.GetFile(oldName, content, sha, Error)) return false;
        if (!client.PutFile(newName, content, {}, L"Rename " + oldName, Error)) return false;
        return client.DeleteFile(oldName, sha, L"Rename " + oldName, Error);
    };
    return renameEntry(oldPath, newPath);
}

intptr_t FarGitHubPanel::DeleteFiles(PluginPanelItem* items, size_t count, OPERATION_MODES opMode)
{
    if (!items || !count || Repository.empty()) return FALSE;
    GitHubClient client(Token, Repository, CurrentBranch);
    std::function<bool(const std::wstring&)> removeEntry;
    removeEntry = [&](const std::wstring& path) -> bool
    {
        std::vector<GitHubEntry> children;
        std::wstring listError;
        if (client.GetEntries(path, children, listError))
        {
            for (const auto& child : children) if (!removeEntry(path + L"/" + child.Name)) return false;
            return true;
        }
        if (!IsNotFound(listError)) { Error = listError.empty() ? L"Unable to read delete source." : listError; return false; }
        std::string content;
        std::wstring sha;
        if (!client.GetFile(path, content, sha, Error)) return false;
        return client.DeleteFile(path, sha, L"Delete " + path, Error);
    };
    if (!(opMode & OPM_SILENT))
    {
        const wchar_t* text[] = { L"GitHub", L"Delete selected item(s)?" };
        if (GPluginInfo.Message(&MainGuid, nullptr, FMSG_WARNING | FMSG_MB_YESNO, nullptr, text, 2, 1) != 0) return FALSE;
    }
    for (size_t i = 0; i < count; ++i)
    {
        if (!removeEntry(FullPath(items[i].FileName))) { ShowError(); return FALSE; }
        items[i].Flags &= ~PPIF_SELECTED;
    }
    Reload();
    return TRUE;
}
