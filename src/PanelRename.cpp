#include "Panel.hpp"
#include "Plugin.hpp"

#include <functional>

namespace
{
const GUID RenameDialogGuid = { 0x8b4e2f31, 0x5a27, 0x4c91, { 0x93, 0x46, 0x72, 0x1d, 0xa8, 0x35, 0x4f, 0x60 } };

bool IsNotFound(const std::wstring& error)
{
    return error.find(L"HTTP 404") != std::wstring::npos;
}
}

intptr_t ProcessRenameInput(FarGitHubPanel* panel)
{
    if (!panel || panel->Repository.empty())
        return FALSE;

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

    const std::wstring oldName = item->FileName ? item->FileName : L"";
    const bool isDirectory = (item->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    free(item);

    if (oldName.empty() || oldName == L"." || oldName == L"..")
        return FALSE;

    wchar_t buffer[4096] = {};
    wcsncpy_s(buffer, _countof(buffer), oldName.c_str(), _TRUNCATE);

    if (GPluginInfo.InputBox(
        &MainGuid,
        &RenameDialogGuid,
        L"GitHub",
        isDirectory ? L"Rename directory" : L"Rename file",
        L"GitHubRename",
        buffer,
        buffer,
        _countof(buffer),
        nullptr,
        FIB_ENABLEEMPTY) != TRUE)
    {
        return TRUE;
    }

    const std::wstring newName = buffer;
    if (newName.empty() || newName == L"." || newName == L"..")
    {
        panel->Error = L"New name is empty or invalid.";
        panel->ShowError(L"Rename");
        return TRUE;
    }

    if (newName.find(L'/') != std::wstring::npos || newName.find(L'\\') != std::wstring::npos)
    {
        panel->Error = L"New name must not contain path separators.";
        panel->ShowError(L"Rename");
        return TRUE;
    }

    const std::wstring oldPath = panel->FullPath(oldName);
    const std::wstring newPath = panel->FullPath(newName);

    if (oldName == newName)
        return TRUE;

    // Не разрешаем переименование поверх существующего объекта.
    // Иначе рекурсивное перемещение каталога может незаметно объединить два дерева.
    GitHubClient client(panel->Token, panel->Repository, panel->CurrentBranch);
    std::vector<GitHubEntry> existingEntries;
    std::wstring probeError;
    if (client.GetEntries(newPath, existingEntries, probeError))
    {
        panel->Error = L"Destination already exists: " + newName;
        panel->ShowError(L"Rename");
        return TRUE;
    }
    if (!IsNotFound(probeError))
    {
        panel->Error = probeError.empty() ? L"Unable to check rename destination." : probeError;
        panel->ShowError(L"Rename");
        return TRUE;
    }

    std::string existingContent;
    std::wstring existingSha;
    if (client.GetFile(newPath, existingContent, existingSha, probeError))
    {
        panel->Error = L"Destination already exists: " + newName;
        panel->ShowError(L"Rename");
        return TRUE;
    }
    if (!IsNotFound(probeError))
    {
        panel->Error = probeError.empty() ? L"Unable to check rename destination." : probeError;
        panel->ShowError(L"Rename");
        return TRUE;
    }

    bool success = false;
    if (isDirectory)
    {
        std::function<bool(const std::wstring&, const std::wstring&)> moveEntry;
        moveEntry = [&](const std::wstring& source, const std::wstring& destination) -> bool
        {
            std::vector<GitHubEntry> children;
            std::wstring listError;
            if (client.GetEntries(source, children, listError))
            {
                if (children.empty())
                    return client.CreateDirectoryEntry(destination, L"Rename directory " + source, panel->Error);

                for (const auto& child : children)
                {
                    if (!moveEntry(source + L"/" + child.Name, destination + L"/" + child.Name))
                        return false;
                }

                for (const auto& child : children)
                {
                    // Дочерний каталог уже очищен рекурсивным вызовом.
                    if (child.Type == L"dir")
                        continue;

                    const std::wstring childPath = source + L"/" + child.Name;
                    std::string content;
                    std::wstring sha;
                    if (!client.GetFile(childPath, content, sha, panel->Error))
                        return false;
                    if (!client.DeleteFile(childPath, sha, L"Rename " + source, panel->Error))
                        return false;
                }
                return true;
            }

            if (!IsNotFound(listError))
            {
                panel->Error = listError.empty() ? L"Unable to read rename source." : listError;
                return false;
            }

            std::string content;
            std::wstring sha;
            if (!client.GetFile(source, content, sha, panel->Error))
                return false;
            if (!client.PutFile(destination, content, {}, L"Rename " + source, panel->Error))
                return false;
            return client.DeleteFile(source, sha, L"Rename " + source, panel->Error);
        };

        success = moveEntry(oldPath, newPath);
    }
    else
    {
        success = panel->RenameEntry(oldPath, newPath);
    }

    if (!success)
    {
        panel->ShowError(L"Rename");
        return TRUE;
    }

    panel->Reload();
    panel->UpdatePanel();
    return TRUE;
}
