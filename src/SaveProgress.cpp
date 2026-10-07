#include "SaveProgress.hpp"
#include "Plugin.hpp"
#include "GitHubClient.hpp"

#include <atomic>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <new>
#include <string>

namespace
{
const GUID ProgressDialogGuid = { 0x3d5b7a21, 0x4c8e, 0x47a2, { 0x91, 0x35, 0x62, 0x7a, 0x18, 0x4e, 0x9b, 0x50 } };

enum class SyncRequestType : unsigned long
{
    Progress = 0x47504850,
    ProgressUpdate = 0x47504855,
    EditorSave = 0x47504853,
    EditorExit = 0x47504845
};

struct SyncRequest
{
    SyncRequestType Type;
};

struct ProgressContext : SyncRequest
{
    HANDLE Dialog = INVALID_HANDLE_VALUE;
    std::function<bool()> Operation;
    std::atomic<bool> Finished{ false };
    std::atomic<bool> Result{ false };
    std::atomic<size_t> Current{ 0 };
    std::atomic<size_t> Total{ 0 };
    std::mutex TextMutex;
    std::wstring Item;

    ProgressContext()
    {
        Type = SyncRequestType::Progress;
    }
};

struct ProgressUpdateRequest : SyncRequest
{
    ProgressContext* Context = nullptr;
    ProgressUpdateRequest() { Type = SyncRequestType::ProgressUpdate; }
};

struct EditorSaveRequest : SyncRequest
{
    EditorSaveRequest()
    {
        Type = SyncRequestType::EditorSave;
    }
};

struct EditorExitRequest : SyncRequest
{
    EditorExitRequest()
    {
        Type = SyncRequestType::EditorExit;
    }
};

struct EditorSession
{
    std::wstring TempFile;
    std::wstring RemotePath;
    std::wstring RemoteSha;
    std::wstring Token;
    std::wstring Repository;
    std::wstring Branch;
};

std::mutex ProgressContextMutex;
ProgressContext* ActiveProgressContext = nullptr;
std::mutex EditorSessionMutex;
EditorSession ActiveEditorSession;
bool EditorSessionActive = false;

intptr_t WINAPI ProgressDialogProc(HANDLE hDlg, intptr_t message, intptr_t param1, void* param2)
{
    if (message == DN_INITDIALOG)
        return TRUE;

    if (message == DN_CLOSE)
    {
        auto* context = reinterpret_cast<ProgressContext*>(GPluginInfo.SendDlgMessage(hDlg, DM_GETDLGDATA, 0, nullptr));
        if (!context || !context->Finished.load())
            return FALSE;
        return TRUE;
    }

    return GPluginInfo.DefDlgProc(hDlg, message, param1, param2);
}

DWORD WINAPI ProgressWorkerProc(LPVOID parameter)
{
    auto* context = static_cast<ProgressContext*>(parameter);
    context->Result = context->Operation();
    context->Finished = true;

    // Передаём завершение операции в главный поток Far Manager.
    GPluginInfo.AdvControl(&MainGuid, ACTL_SYNCHRO, 0, context);
    return 0;
}

DWORD WINAPI EditorSaveRequestProc(LPVOID parameter)
{
    auto* request = static_cast<EditorSaveRequest*>(parameter);

    // Переносим сетевую операцию из ProcessEditorInputW в главный поток Far Manager.
    GPluginInfo.AdvControl(&MainGuid, ACTL_SYNCHRO, 0, request);
    return 0;
}

DWORD WINAPI EditorExitRequestProc(LPVOID parameter)
{
    auto* request = static_cast<EditorExitRequest*>(parameter);

    // Выполняем запрос выхода после возврата Far из ProcessEditorInputW.
    GPluginInfo.AdvControl(&MainGuid, ACTL_SYNCHRO, 0, request);
    return 0;
}

HANDLE CreateProgressDialog(ProgressContext* context, const wchar_t* text)
{
    FarDialogItem items[] =
    {
        { DI_DOUBLEBOX, 0, 0, 58, 6, { 0 }, nullptr, nullptr, DIF_NONE, L"GitHub", 0, 0, { 0, 0 } },
        { DI_TEXT,      2, 1, 56, 1, { 0 }, nullptr, nullptr, DIF_CENTERTEXT, text, 0, 0, { 0, 0 } },
        { DI_TEXT,      2, 2, 56, 2, { 0 }, nullptr, nullptr, DIF_CENTERTEXT, L"Please wait...", 0, 0, { 0, 0 } },
        { DI_TEXT,      2, 3, 56, 3, { 0 }, nullptr, nullptr, DIF_CENTERTEXT, L"[                              ]", 0, 0, { 0, 0 } },
        { DI_TEXT,      2, 4, 56, 4, { 0 }, nullptr, nullptr, DIF_CENTERTEXT, L"", 0, 0, { 0, 0 } }
    };

    const HANDLE dialog = GPluginInfo.DialogInit(
        &MainGuid,
        &ProgressDialogGuid,
        -1,
        -1,
        58,
        6,
        nullptr,
        items,
        std::size(items),
        0,
        FDLG_SMALLDIALOG,
        ProgressDialogProc,
        context);

    if (dialog != INVALID_HANDLE_VALUE)
        GPluginInfo.SendDlgMessage(dialog, DM_SETDLGDATA, 0, context);

    return dialog;
}

bool SaveActiveEditorToGitHub()
{
    EditorSession session;
    {
        std::lock_guard<std::mutex> lock(EditorSessionMutex);
        if (!EditorSessionActive)
            return false;
        session = ActiveEditorSession;
    }

    std::ifstream file(session.TempFile, std::ios::binary);
    if (!file)
        return false;

    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    GitHubClient client(session.Token, session.Repository, session.Branch);
    std::wstring error;

    const bool saved = RunGitHubProgress(
        L"Сохранение изменений на GitHub...",
        [&client, &session, &content, &error]()
        {
            return client.PutFile(session.RemotePath, content, session.RemoteSha, L"Update " + session.RemotePath, error);
        });

    if (!saved)
    {
        const wchar_t* message[] = { L"GitHub", error.empty() ? L"Не удалось сохранить изменения на GitHub." : error.c_str() };
        GPluginInfo.Message(&MainGuid, nullptr, FMSG_ERRORTYPE | FMSG_MB_OK, nullptr, message, 2, 1);
        return false;
    }

    std::string refreshedContent;
    std::wstring refreshedSha;
    if (client.GetFile(session.RemotePath, refreshedContent, refreshedSha, error))
    {
        std::lock_guard<std::mutex> lock(EditorSessionMutex);
        if (EditorSessionActive && ActiveEditorSession.TempFile == session.TempFile)
            ActiveEditorSession.RemoteSha = refreshedSha;
    }

    return true;
}
}

bool RunGitHubProgress(const wchar_t* text, const std::function<bool()>& operation)
{
    ProgressContext context;
    context.Operation = operation;
    {
        std::lock_guard<std::mutex> lock(ProgressContextMutex);
        ActiveProgressContext = &context;
    }
    context.Dialog = CreateProgressDialog(&context, text);

    if (context.Dialog == INVALID_HANDLE_VALUE)
        return operation();

    HANDLE thread = CreateThread(nullptr, 0, ProgressWorkerProc, &context, 0, nullptr);
    if (!thread)
    {
        context.Finished = true;
        GPluginInfo.SendDlgMessage(context.Dialog, DM_CLOSE, 0, nullptr);
        GPluginInfo.DialogFree(context.Dialog);
        return operation();
    }

    GPluginInfo.DialogRun(context.Dialog);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    GPluginInfo.DialogFree(context.Dialog);
    {
        std::lock_guard<std::mutex> lock(ProgressContextMutex);
        if (ActiveProgressContext == &context) ActiveProgressContext = nullptr;
    }
    return context.Result.load();
}

void UpdateGitHubProgress(size_t current, size_t total, const std::wstring& item)
{
    ProgressContext* context = nullptr;
    {
        std::lock_guard<std::mutex> lock(ProgressContextMutex);
        context = ActiveProgressContext;
    }
    if (!context) return;
    context->Current = current;
    context->Total = total;
    {
        std::lock_guard<std::mutex> lock(context->TextMutex);
        context->Item = item;
    }
    auto* request = new (std::nothrow) ProgressUpdateRequest();
    if (!request) return;
    request->Context = context;
    GPluginInfo.AdvControl(&MainGuid, ACTL_SYNCHRO, 0, request);
}

void BeginGitHubEditorSession(const std::wstring& tempFile,
                              const std::wstring& remotePath,
                              const std::wstring& remoteSha,
                              const std::wstring& token,
                              const std::wstring& repository,
                              const std::wstring& branch)
{
    std::lock_guard<std::mutex> lock(EditorSessionMutex);
    ActiveEditorSession.TempFile = tempFile;
    ActiveEditorSession.RemotePath = remotePath;
    ActiveEditorSession.RemoteSha = remoteSha;
    ActiveEditorSession.Token = token;
    ActiveEditorSession.Repository = repository;
    ActiveEditorSession.Branch = branch;
    EditorSessionActive = true;
}

std::wstring EndGitHubEditorSession()
{
    std::lock_guard<std::mutex> lock(EditorSessionMutex);
    const std::wstring remoteSha = ActiveEditorSession.RemoteSha;
    ActiveEditorSession = {};
    EditorSessionActive = false;
    return remoteSha;
}

bool HandleGitHubEditorExitRequest()
{
    EditorInfo editorInfo = { sizeof(editorInfo) };
    if (!GPluginInfo.EditorControl(-1, ECTL_GETINFO, 0, &editorInfo))
        return true;

    if ((editorInfo.CurState & ECSTATE_MODIFIED) == 0)
    {
        GPluginInfo.EditorControl(-1, ECTL_QUIT, 0, nullptr);
        return true;
    }

    const wchar_t* message[] =
    {
        L"GitHub",
        L"Файл изменён. Сохранить изменения на GitHub?",
        L"Сохранить",
        L"Не сохранять",
        L"Отмена"
    };

    const intptr_t result = GPluginInfo.Message(
        &MainGuid,
        nullptr,
        FMSG_MB_YESNOCANCEL,
        nullptr,
        message,
        std::size(message),
        3);

    if (result == 2 || result < 0)
        return true;

    if (result == 0)
    {
        if (!GPluginInfo.EditorControl(-1, ECTL_SAVEFILE, 0, nullptr))
        {
            const wchar_t* error[] = { L"GitHub", L"Не удалось сохранить файл в редакторе." };
            GPluginInfo.Message(&MainGuid, nullptr, FMSG_ERRORTYPE | FMSG_MB_OK, nullptr, error, 2, 1);
            return true;
        }

        if (!SaveActiveEditorToGitHub())
            return true;
    }

    // Закрываем редактор только после успешного сохранения на GitHub
    // или после явного выбора «Не сохранять».
    GPluginInfo.EditorControl(-1, ECTL_QUIT, 0, nullptr);
    return true;
}

intptr_t WINAPI ProcessSynchroEventW(const ProcessSynchroEventInfo* info)
{
    if (!info || info->StructSize < sizeof(*info) || info->Event != SE_COMMONSYNCHRO || !info->Param)
        return 0;

    auto* request = static_cast<SyncRequest*>(info->Param);

    if (request->Type == SyncRequestType::ProgressUpdate)
    {
        auto* update = static_cast<ProgressUpdateRequest*>(request);
        auto* context = update->Context;
        delete update;
        if (!context || context->Dialog == INVALID_HANDLE_VALUE) return 0;

        const size_t current = context->Current.load();
        const size_t total = context->Total.load();
        const size_t percent = total ? (current * 100 / total) : 0;
        const size_t filled = total ? (percent * 30 / 100) : 0;
        std::wstring bar = L"[" + std::wstring(filled, L'#') + std::wstring(30 - filled, L'-') + L"] " + std::to_wstring(percent) + L"%";
        std::wstring item;
        {
            std::lock_guard<std::mutex> lock(context->TextMutex);
            item = context->Item;
        }
        std::wstring status = std::to_wstring(current) + L" / " + std::to_wstring(total);
        if (!item.empty()) status += L"  " + item;
        FarDialogItemData data = { sizeof(data), bar.size() + 1, const_cast<wchar_t*>(bar.c_str()) };
        GPluginInfo.SendDlgMessage(context->Dialog, DM_SETTEXTPTR, 2, const_cast<wchar_t*>(status.c_str()));
        GPluginInfo.SendDlgMessage(context->Dialog, DM_SETTEXTPTR, 3, const_cast<wchar_t*>(bar.c_str()));
        return 0;
    }

    if (request->Type == SyncRequestType::EditorSave)
    {
        auto* saveRequest = static_cast<EditorSaveRequest*>(request);
        delete saveRequest;
        SaveActiveEditorToGitHub();
        return 0;
    }

    if (request->Type == SyncRequestType::EditorExit)
    {
        auto* exitRequest = static_cast<EditorExitRequest*>(request);
        delete exitRequest;
        HandleGitHubEditorExitRequest();
        return 0;
    }

    if (request->Type != SyncRequestType::Progress)
        return 0;

    auto* context = static_cast<ProgressContext*>(request);
    if (!context->Finished.load())
        return 0;

    if (context->Dialog != INVALID_HANDLE_VALUE)
        GPluginInfo.SendDlgMessage(context->Dialog, DM_CLOSE, 0, nullptr);

    return 0;
}

intptr_t WINAPI ProcessEditorInputW(const ProcessEditorInputInfo* info)
{
    if (!info || info->StructSize < sizeof(*info))
        return 0;

    const INPUT_RECORD& record = info->Rec;
    if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown)
        return 0;

    const auto& key = record.Event.KeyEvent;
    const DWORD state = key.dwControlKeyState;
    const bool ctrl = (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    const bool alt = (state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
    const bool shift = (state & SHIFT_PRESSED) != 0;

    if (!ctrl && !alt && !shift && (key.wVirtualKeyCode == VK_ESCAPE || key.wVirtualKeyCode == VK_F10))
    {
        auto* request = new (std::nothrow) EditorExitRequest();
        if (!request)
        {
            const wchar_t* message[] = { L"GitHub", L"Не удалось запланировать выход из редактора." };
            GPluginInfo.Message(&MainGuid, nullptr, FMSG_ERRORTYPE | FMSG_MB_OK, nullptr, message, 2, 1);
            return 1;
        }

        HANDLE thread = CreateThread(nullptr, 0, EditorExitRequestProc, request, 0, nullptr);
        if (!thread)
        {
            delete request;
            const wchar_t* message[] = { L"GitHub", L"Не удалось запланировать выход из редактора." };
            GPluginInfo.Message(&MainGuid, nullptr, FMSG_ERRORTYPE | FMSG_MB_OK, nullptr, message, 2, 1);
            return 1;
        }

        CloseHandle(thread);
        return 1;
    }

    if (key.wVirtualKeyCode != VK_F2 || ctrl || alt || shift)
        return 0;

    // Сначала сохраняем буфер штатной командой Far. Сетевую операцию нельзя
    // выполнять непосредственно внутри ProcessEditorInputW: этот callback
    // вызывается из цикла обработки клавиши редактора.
    if (!GPluginInfo.EditorControl(-1, ECTL_SAVEFILE, 0, nullptr))
    {
        const wchar_t* message[] = { L"GitHub", L"Не удалось сохранить файл в редакторе." };
        GPluginInfo.Message(&MainGuid, nullptr, FMSG_ERRORTYPE | FMSG_MB_OK, nullptr, message, 2, 1);
        return 1;
    }

    auto* request = new (std::nothrow) EditorSaveRequest();
    if (!request)
    {
        const wchar_t* message[] = { L"GitHub", L"Не удалось запланировать сохранение на GitHub." };
        GPluginInfo.Message(&MainGuid, nullptr, FMSG_ERRORTYPE | FMSG_MB_OK, nullptr, message, 2, 1);
        return 1;
    }

    HANDLE thread = CreateThread(nullptr, 0, EditorSaveRequestProc, request, 0, nullptr);
    if (!thread)
    {
        delete request;
        const wchar_t* message[] = { L"GitHub", L"Не удалось запустить сохранение на GitHub." };
        GPluginInfo.Message(&MainGuid, nullptr, FMSG_ERRORTYPE | FMSG_MB_OK, nullptr, message, 2, 1);
        return 1;
    }

    CloseHandle(thread);
    return 1;
}
