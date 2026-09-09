#include "Plugin.hpp"
#include "Panel.hpp"

#include <memory>

PluginStartupInfo GPluginInfo = {};
FarStandardFunctions GFarFunctions = {};

const GUID MainGuid = { 0x7f0d7c51, 0x6e8a, 0x4c2a, { 0x9d, 0x53, 0x41, 0x1b, 0x2a, 0x9c, 0x8e, 0x10 } };
const GUID MenuGuid = { 0x5b7b4c22, 0x6f0f, 0x4b13, { 0xa0, 0x14, 0x91, 0x34, 0x12, 0x88, 0x51, 0x20 } };

static constexpr VersionInfo PluginVersion = { 0, 1, 0, 0, VS_PRIVATE };
static std::wstring PluginTitle = L"GitHub for Far";
static std::unique_ptr<FarGitHubPanel> ActivePanel;

const wchar_t* GetPluginMessage(int)
{
    return PluginTitle.c_str();
}

void WINAPI GetGlobalInfoW(GlobalInfo* info)
{
    info->StructSize = sizeof(*info);
    info->MinFarVersion = { FARMANAGERVERSION_MAJOR, FARMANAGERVERSION_MINOR, FARMANAGERVERSION_REVISION, FARMANAGERVERSION_BUILD, FARMANAGERVERSION_STAGE };
    info->Version = PluginVersion;
    info->Guid = MainGuid;
    info->Title = PluginTitle.c_str();
    info->Description = L"Browse and edit GitHub repositories from Far Manager";
    info->Author = L"lotusv2";
}

void WINAPI SetStartupInfoW(const PluginStartupInfo* info)
{
    GPluginInfo = *info;
    GFarFunctions = *info->FSF;
    GPluginInfo.FSF = &GFarFunctions;
}

void WINAPI GetPluginInfoW(PluginInfo* info)
{
    info->StructSize = sizeof(*info);
    info->Flags = PF_NONE;
    static const wchar_t* menu[] = { L"GitHub" };
    info->PluginMenu.Guids = &MenuGuid;
    info->PluginMenu.Strings = menu;
    info->PluginMenu.Count = 1;
    info->CommandPrefix = L"gh";
}

HANDLE WINAPI OpenW(const OpenInfo*)
{
    ActivePanel = std::make_unique<FarGitHubPanel>();
    return ActivePanel.get();
}

void WINAPI ClosePanelW(const ClosePanelInfo*)
{
    ActivePanel.reset();
}

intptr_t WINAPI GetFindDataW(GetFindDataInfo* info)
{
    return static_cast<FarGitHubPanel*>(info->hPanel)->GetFindData(&info->PanelItem, &info->ItemsNumber, info->OpMode);
}

void WINAPI FreeFindDataW(const FreeFindDataInfo* info)
{
    static_cast<FarGitHubPanel*>(info->hPanel)->FreeFindData(info->PanelItem, info->ItemsNumber);
}

void WINAPI GetOpenPanelInfoW(OpenPanelInfo* info)
{
    static_cast<FarGitHubPanel*>(info->hPanel)->GetOpenPanelInfo(info);
}

intptr_t WINAPI SetDirectoryW(const SetDirectoryInfo* info)
{
    return static_cast<FarGitHubPanel*>(info->hPanel)->SetDirectory(info->Dir, info->OpMode);
}

intptr_t WINAPI ProcessHostFileW(const ProcessHostFileInfo* info)
{
    return static_cast<FarGitHubPanel*>(info->hPanel)->ProcessHostFile(info->PanelItem, info->ItemsNumber, info->OpMode);
}

intptr_t WINAPI MakeDirectoryW(MakeDirectoryInfo* info)
{
    return static_cast<FarGitHubPanel*>(info->hPanel)->MakeDirectory(info->Name, info->OpMode);
}

intptr_t WINAPI PutFilesW(const PutFilesInfo* info)
{
    return static_cast<FarGitHubPanel*>(info->hPanel)->PutFiles(info->PanelItem, info->ItemsNumber, info->SrcPath, info->OpMode);
}

intptr_t WINAPI ConfigureW(const ConfigureInfo*)
{
    const wchar_t* text[] = { L"GitHub for Far", L"Configuration is currently controlled by environment variables:", L"FAR_GITHUB_TOKEN", L"FAR_GITHUB_REPOSITORY" };
    GPluginInfo.Message(&MainGuid, nullptr, FMSG_LEFTALIGN | FMSG_MB_OK, nullptr, text, 4, 1);
    return TRUE;
}

void WINAPI ExitFARW(const ExitInfo*)
{
    ActivePanel.reset();
}
