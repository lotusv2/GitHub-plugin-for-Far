#include "SaveProgress.hpp"
#include "Plugin.hpp"

#include <atomic>
#include <functional>
#include <thread>

namespace
{
const GUID ProgressDialogGuid = { 0x3d5b7a21, 0x4c8e, 0x47a2, { 0x91, 0x35, 0x62, 0x7a, 0x18, 0x4e, 0x9b, 0x50 } };

struct ProgressContext
{
    HANDLE Dialog = INVALID_HANDLE_VALUE;
    std::function<bool()> Operation;
    std::atomic<bool> Finished{ false };
    std::atomic<bool> Result{ false };
    bool AutoDelete = false;
};

intptr_t WINAPI ProgressDialogProc(HANDLE hDlg, intptr_t message, intptr_t param1, void* param2)
{
    if (message == DN_INITDIALOG)
        return TRUE;

    if (message == DN_CLOSE)
    {
        // Пока операция не завершена, закрывать диалог нельзя.
        auto* context = static_cast<ProgressContext*>(GPluginInfo.SendDlgMessage(hDlg, DM_GETDLGDATA, 0, nullptr));
        if (!context || !context->Finished.load())
            return FALSE;
        return TRUE;
    }

    return GPluginInfo.DefDlgProc(hDlg, message, param1, param2);
}

void NotifyProgressFinished(ProgressContext* context)
{
    GPluginInfo.AdvControl(&MainGuid, ACTL_SYNCHRO, 0, context);
}

DWORD WINAPI ProgressWorkerProc(LPVOID parameter)
{
    auto* context = static_cast<ProgressContext*>(parameter);
    context->Result = context->Operation();
    context->Finished = true;
    NotifyProgressFinished(context);
    return 0;
}

HANDLE CreateProgressDialog(ProgressContext* context, const wchar_t* text, bool nonModal)
{
    FarDialogItem items[] =
    {
        { DI_DOUBLEBOX, 0, 0, 48, 4, { 0 }, nullptr, nullptr, DIF_NONE, L"GitHub", 0, 0, { 0, 0 } },
        { DI_TEXT,      2, 1, 46, 1, { 0 }, nullptr, nullptr, DIF_CENTERTEXT, text, 0, 0, { 0, 0 } },
        { DI_TEXT,      2, 2, 46, 2, { 0 }, nullptr, nullptr, DIF_CENTERTEXT, L"Пожалуйста, подождите...", 0, 0, { 0, 0 } }
    };

    const FARDIALOGFLAGS flags = FDLG_SMALLDIALOG | (nonModal ? FDLG_NONMODAL : FDLG_NONE);
    const HANDLE dialog = GPluginInfo.DialogInit(
        &MainGuid,
        &ProgressDialogGuid,
        -1,
        -1,
        48,
        4,
        nullptr,
        items,
        std::size(items),
        0,
        flags,
        ProgressDialogProc,
        context);

    if (dialog != INVALID_HANDLE_VALUE)
        GPluginInfo.SendDlgMessage(dialog, DM_SETDLGDATA, 0, context);

    return dialog;
}
}

bool RunGitHubProgress(const wchar_t* text, const std::function<bool()>& operation)
{
    ProgressContext context;
    context.Operation = operation;

    context.Dialog = CreateProgressDialog(&context, text, false);
    if (context.Dialog == INVALID_HANDLE_VALUE)
        return operation();

    HANDLE thread = CreateThread(nullptr, 0, ProgressWorkerProc, &context, 0, nullptr);
    if (!thread)
    {
        GPluginInfo.SendDlgMessage(context.Dialog, DM_CLOSE, 0, nullptr);
        GPluginInfo.DialogFree(context.Dialog);
        return operation();
    }

    GPluginInfo.DialogRun(context.Dialog);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    GPluginInfo.DialogFree(context.Dialog);
    return context.Result.load();
}

void ShowGitHubProgressTimed(const wchar_t* text, DWORD timeoutMs)
{
    auto* context = new ProgressContext();
    context->AutoDelete = true;

    context->Dialog = CreateProgressDialog(context, text, true);
    if (context->Dialog == INVALID_HANDLE_VALUE)
    {
        delete context;
        return;
    }

    std::thread([context, timeoutMs]()
    {
        Sleep(timeoutMs);
        context->Finished = true;
        NotifyProgressFinished(context);
    }).detach();
}

intptr_t WINAPI ProcessSynchroEventW(const ProcessSynchroEventInfo* info)
{
    if (!info || info->Event != SE_COMMONSYNCHRO || !info->Param)
        return 0;

    auto* context = static_cast<ProgressContext*>(info->Param);
    if (context->Dialog != INVALID_HANDLE_VALUE)
        GPluginInfo.SendDlgMessage(context->Dialog, DM_CLOSE, 0, nullptr);

    if (context->AutoDelete)
        delete context;

    return 0;
}

intptr_t WINAPI ProcessEditorEventW(const ProcessEditorEventInfo* info)
{
    if (!info)
        return 0;

    if (info->Event == EE_SAVE)
        ShowGitHubProgressTimed(L"Сохранение файла...", 1200);

    return 0;
}
