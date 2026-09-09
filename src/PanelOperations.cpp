#include "Panel.hpp"

intptr_t WINAPI GetFilesW(GetFilesInfo* info)
{
    if (!info || info->StructSize < sizeof(GetFilesInfo) || !info->hPanel)
        return FALSE;

    auto* panel = static_cast<FarGitHubPanel*>(info->hPanel);
    return panel->GetFiles(info->PanelItem, info->ItemsNumber, info->Move != FALSE, info->DestPath, info->OpMode);
}

intptr_t WINAPI DeleteFilesW(DeleteFilesInfo* info)
{
    if (!info || info->StructSize < sizeof(DeleteFilesInfo) || !info->hPanel)
        return FALSE;

    auto* panel = static_cast<FarGitHubPanel*>(info->hPanel);
    return panel->DeleteFiles(info->PanelItem, info->ItemsNumber, info->OpMode);
}
