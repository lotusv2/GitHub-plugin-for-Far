#include "Panel.hpp"
#include "Plugin.hpp"

namespace
{
const GUID RenameDialogGuid = { 0x8b4e2f31, 0x5a27, 0x4c91, { 0x93, 0x46, 0x72, 0x1d, 0xa8, 0x35, 0x4f, 0x60 } };
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

    if (_wcsicmp(oldName.c_str(), newName.c_str()) == 0)
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
    if (probeError.find(L"HTTP 404") == std::wstring::npos)
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
    if (probeError.find(L"HTTP 404") == std::wstring::npos)
    {
        panel->Error = probeError.empty() ? L"Unable to check rename destination." : probeError;
        panel->ShowError(L"Rename");
        return TRUE;
    }

    if (!panel->RenameEntry(oldPath, newPath))
    {
        panel->ShowError(L"Rename");
        return TRUE;
    }

    panel->Reload();
    panel->UpdatePanel();
    return TRUE;
}
