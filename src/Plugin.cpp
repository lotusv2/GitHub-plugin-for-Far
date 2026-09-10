#include "Plugin.hpp"
#include "Panel.hpp"
#include "GitHubClient.hpp"
#include "Settings.hpp"

#include <memory>
#include <iterator>
#include <cwchar>
#include <string>

PluginStartupInfo GPluginInfo = {};
FarStandardFunctions GFarFunctions = {};

const GUID MainGuid = { 0x7f0d7c51, 0x6e8a, 0x4c2a, { 0x9d, 0x53, 0x41, 0x1b, 0x2a, 0x9c, 0x8e, 0x10 } };
const GUID MenuGuid = { 0x5b7b4c22, 0x6f0f, 0x4b13, { 0xa0, 0x14, 0x91, 0x34, 0x12, 0x88, 0x51, 0x20 } };
const GUID SettingsDialogGuid = { 0x4c2e1d9a, 0x8b4f, 0x4d65, { 0x91, 0x27, 0x62, 0x3d, 0x7a, 0x0e, 0x54, 0x19 } };

static constexpr VersionInfo PluginVersion = { 0, 5, 0, 0, VS_PRIVATE };
static std::wstring PluginTitle = L"GitHub for Far";
static std::unique_ptr<FarGitHubPanel> ActivePanel;

enum SettingsDialogItem
{
    SDI_TOKEN = 2,
    SDI_STATUS = 3,
    SDI_TEST = 5,
    SDI_SAVE = 6,
    SDI_CLEAR = 7,
    SDI_CANCEL = 8
};

struct SettingsDialogState
{
    GitHubSettings Settings;
};

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

    // Регистрируем тот же пункт в меню конфигурации плагинов Far Manager.
    info->PluginConfig.Guids = &MenuGuid;
    info->PluginConfig.Strings = menu;
    info->PluginConfig.Count = 1;

    info->CommandPrefix = L"gh";
}

HANDLE WINAPI OpenW(const OpenInfo* info)
{
    const wchar_t* entered[] = { L"GitHub for Far", L"OpenW() entered." };
    GPluginInfo.Message(&MainGuid, nullptr, FMSG_MB_OK, nullptr, entered, 2, 1);

    std::wstring diagnostic;
    if (info)
    {
        diagnostic = L"OpenFrom=" + std::to_wstring(static_cast<int>(info->OpenFrom)) +
                     L"; StructSize=" + std::to_wstring(info->StructSize);
    }
    else
    {
        diagnostic = L"OpenInfo is null.";
    }

    const wchar_t* openInfoMessage[] = { L"GitHub for Far", diagnostic.c_str() };
    GPluginInfo.Message(&MainGuid, nullptr, FMSG_MB_OK, nullptr, openInfoMessage, 2, 1);

    ActivePanel = std::make_unique<FarGitHubPanel>();

    const wchar_t* created[] = { L"GitHub for Far", L"FarGitHubPanel created." };
    GPluginInfo.Message(&MainGuid, nullptr, FMSG_MB_OK, nullptr, created, 2, 1);

    const std::wstring handleText = L"Handle=" + std::to_wstring(reinterpret_cast<uintptr_t>(ActivePanel.get()));
    const wchar_t* handleMessage[] = { L"GitHub for Far", handleText.c_str() };
    GPluginInfo.Message(&MainGuid, nullptr, FMSG_MB_OK, nullptr, handleMessage, 2, 1);

    return ActivePanel.get();
}

void WINAPI ClosePanelW(const ClosePanelInfo* info)
{
    const std::wstring diagnostic = !info ? L"ClosePanel: info=null" :
        L"ClosePanel: hPanel=" + std::to_wstring(reinterpret_cast<uintptr_t>(info->hPanel));
    const wchar_t* message[] = { L"GitHub for Far", diagnostic.c_str() };
    GPluginInfo.Message(&MainGuid, nullptr, FMSG_MB_OK, nullptr, message, 2, 1);
    ActivePanel.reset();
}

intptr_t WINAPI GetFindDataW(GetFindDataInfo* info)
{
    const std::wstring diagnostic = !info ? L"GetFindData: info=null" :
        L"GetFindData: hPanel=" + std::to_wstring(reinterpret_cast<uintptr_t>(info->hPanel));
    const wchar_t* message[] = { L"GitHub for Far", diagnostic.c_str() };
    GPluginInfo.Message(&MainGuid, nullptr, FMSG_MB_OK, nullptr, message, 2, 1);

    if (!info || !info->hPanel)
        return -1;

    // Временный ABI-тест: не вызываем код панели и не работаем с GitHub.
    info->PanelItem = nullptr;
    info->ItemsNumber = 0;
    return 1;
}

void WINAPI FreeFindDataW(const FreeFindDataInfo* info)
{
    (void)info;
}

void WINAPI GetOpenPanelInfoW(OpenPanelInfo* info)
{
    if (!info || !info->hPanel)
        return;

    info->StructSize = sizeof(*info);
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

intptr_t WINAPI ProcessPanelInputW(const ProcessPanelInputInfo* info)
{
    auto* panel = static_cast<FarGitHubPanel*>(info->hPanel);
    if (!panel)
        return FALSE;

    const auto& key = info->Rec.Event.KeyEvent;
    const DWORD state = key.dwControlKeyState;
    const bool ctrl = (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    const bool shift = (state & SHIFT_PRESSED) != 0;

    if (info->Rec.EventType == KEY_EVENT && key.bKeyDown &&
        key.wVirtualKeyCode == VK_F6 && !ctrl && !shift)
    {
        return ProcessRenameInput(panel);
    }

    return panel->ProcessInput(info->Rec);
}

intptr_t WINAPI MakeDirectoryW(MakeDirectoryInfo* info)
{
    return static_cast<FarGitHubPanel*>(info->hPanel)->MakeDirectory(info->Name, info->OpMode);
}

intptr_t WINAPI PutFilesW(const PutFilesInfo* info)
{
    return static_cast<FarGitHubPanel*>(info->hPanel)->PutFiles(info->PanelItem, info->ItemsNumber, info->SrcPath, info->OpMode);
}

static std::wstring GetDialogText(HANDLE dialog, intptr_t item)
{
    wchar_t buffer[4096] = {};
    FarDialogItemData data = { sizeof(data), std::size(buffer) - 1, buffer };
    GPluginInfo.SendDlgMessage(dialog, DM_GETTEXT, item, &data);
    const size_t length = data.PtrLength < std::size(buffer) ? data.PtrLength : std::size(buffer) - 1;
    buffer[length] = L'\0';
    return buffer;
}

static void SetDialogText(HANDLE dialog, intptr_t item, const std::wstring& text)
{
    FarDialogItemData data = { sizeof(data), text.size(), const_cast<wchar_t*>(text.c_str()) };
    GPluginInfo.SendDlgMessage(dialog, DM_SETTEXT, item, &data);
}

static void SetDialogStatus(HANDLE dialog, const std::wstring& text)
{
    SetDialogText(dialog, SDI_STATUS, text);
}

static bool TestDialogToken(HANDLE dialog, std::wstring& token, std::wstring& login)
{
    token = GetDialogText(dialog, SDI_TOKEN);
    if (token.empty())
    {
        SetDialogStatus(dialog, L"Token is empty.");
        return false;
    }

    GitHubClient client(token);
    std::wstring error;
    if (!client.TestConnection(login, error))
    {
        SetDialogStatus(dialog, L"Connection failed: " + error);
        return false;
    }

    SetDialogStatus(dialog, L"Connected as " + login + L".");
    return true;
}

static intptr_t WINAPI SettingsDialogProc(HANDLE dialog, intptr_t message, intptr_t param1, void* param2)
{
    // Param2 содержит пользовательские данные только для DN_INITDIALOG.
    // Для остальных сообщений его значение зависит от конкретного события.
    if (message == DN_INITDIALOG)
    {
        GPluginInfo.SendDlgMessage(dialog, DM_SETDLGDATA, 0, param2);
        return GPluginInfo.DefDlgProc(dialog, message, param1, param2);
    }

    const intptr_t dialogData = GPluginInfo.SendDlgMessage(dialog, DM_GETDLGDATA, 0, nullptr);
    auto* state = reinterpret_cast<SettingsDialogState*>(dialogData);

    if (message == DN_BTNCLICK)
    {
        if (param1 == SDI_TEST)
        {
            std::wstring token;
            std::wstring login;
            TestDialogToken(dialog, token, login);
            return TRUE;
        }

        if (param1 == SDI_CLEAR)
        {
            if (!state)
            {
                SetDialogStatus(dialog, L"Settings state is unavailable.");
                return TRUE;
            }

            if (state->Settings.ClearToken())
            {
                SetDialogText(dialog, SDI_TOKEN, L"");
                SetDialogStatus(dialog, L"Token removed.");
                if (ActivePanel)
                    ActivePanel->ReloadSettings();
            }
            else
            {
                SetDialogStatus(dialog, L"Unable to remove the saved token.");
            }
            return TRUE;
        }

        if (param1 == SDI_SAVE)
        {
            if (!state)
            {
                SetDialogStatus(dialog, L"Settings state is unavailable.");
                return TRUE;
            }

            std::wstring token;
            std::wstring login;
            if (!TestDialogToken(dialog, token, login))
                return TRUE;

            if (!state->Settings.SaveToken(token))
            {
                SetDialogStatus(dialog, L"Unable to save the encrypted token.");
                return TRUE;
            }

            if (ActivePanel)
                ActivePanel->ReloadSettings();

            return FALSE;
        }
    }

    return GPluginInfo.DefDlgProc(dialog, message, param1, param2);
}

static intptr_t ShowSettingsDialog()
{
    GitHubSettings settings;
    std::wstring token;
    settings.LoadToken(token);

    std::wstring status = token.empty() ? L"Token is not configured." : L"Token is configured.";

    FarDialogItem items[] =
    {
        { DI_DOUBLEBOX, 0, 0, 69, 11, {}, nullptr, nullptr, 0, L"GitHub settings", 0, 0, { 0, 0 } },
        { DI_TEXT,      2, 1, 67, 1, {}, nullptr, nullptr, 0, L"GitHub Fine-grained Personal Access Token:", 0, 0, { 0, 0 } },
        { DI_PSWEDIT,   2, 2, 67, 2, {}, nullptr, nullptr, 0, token.c_str(), 2047, 0, { 0, 0 } },
        { DI_TEXT,      2, 4, 67, 4, {}, nullptr, nullptr, 0, status.c_str(), 0, 0, { 0, 0 } },
        { DI_TEXT,      2, 6, 67, 6, {}, nullptr, nullptr, 0, L"Test the token before saving it.", 0, 0, { 0, 0 } },
        { DI_BUTTON,   13, 8, 24, 8, {}, nullptr, nullptr, DIF_DEFAULTBUTTON, L"Test", 0, 0, { 0, 0 } },
        { DI_BUTTON,   27, 8, 39, 8, {}, nullptr, nullptr, 0, L"Save", 0, 0, { 0, 0 }, },
        { DI_BUTTON,   42, 8, 53, 8, {}, nullptr, nullptr, 0, L"Clear", 0, 0, { 0, 0 } },
        { DI_BUTTON,   56, 8, 65, 8, {}, nullptr, nullptr, 0, L"Cancel", 0, 0, { 0, 0 } }
    };

    SettingsDialogState state;
    state.Settings = settings;

    HANDLE dialog = GPluginInfo.DialogInit(
        &MainGuid,
        &SettingsDialogGuid,
        -1, -1, 69, 11,
        nullptr,
        items,
        std::size(items),
        0,
        FDLG_NONE,
        SettingsDialogProc,
        &state);

    if (dialog == INVALID_HANDLE_VALUE)
        return FALSE;

    const intptr_t result = GPluginInfo.DialogRun(dialog);
    GPluginInfo.DialogFree(dialog);
    return result == SDI_SAVE ? TRUE : FALSE;
}

intptr_t WINAPI ConfigureW(const ConfigureInfo*)
{
    return ShowSettingsDialog();
}

void WINAPI ExitFARW(const ExitInfo*)
{
    ActivePanel.reset();
}
