#include "SaveProgress.hpp"
#include "Plugin.hpp"
#include "GitHubClient.hpp"

#include <atomic>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <string>

namespace
{
const GUID ProgressDialogGuid = { 0x3d5b7a21, 0x4c8e, 0x47a2, { 0x91, 0x35, 0x62, 0x7a, 0x18, 0x4e, 0x9b, 0x50 } };

struct ProgressContext
{
    HANDLE Dialog = INVALID_HANDLE_VALUE;
    std::function<bool()> Operation;
    std::atomic<bool> Finished{ false };
    std::atomic<bool> Result{ false };
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

HANDLE CreateProgressDialog(ProgressContext* context, const wchar_t* text)
{
    FarDialogItem items[] =
    {
        { DI_DOUBLEBOX, 0, 0, 48, 4, { 0 }, nullptr, nullptr, DIF_NONE, L"GitHub", 0, 0, { 0, 0 } },
        { DI_TEXT,      2, 1, 46, 1, { 0 }, nullptr, nullptr, DIF_CENTERTEXT, text, 0, 0, { 0, 0 } },
        { DI_TEXT,      2, 2, 46, 2, { 0 }, nullptr, nullptr, DIF_CENTERTEXT, L"Please wait...", 0, 0, { 0, 0 } }
    };

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
        FDLG_SMALLDIALOG,
        ProgressDialogProc,
        context);

    if (dialog != INVALID_HANDLE_VALUE)
        GPluginInfo.SendDlgMessage(dialog, DM_SETDLGDATA, 0, context);

    return dialog;
}

bool ProcessEditorSave(const EditorSaveFile* saveFile)
{
    if (!saveFile || !saveFile->FileName)
        return false;

    EditorSession session;
    {
        std::lock_guard<std::mutex> lock(EditorSessionMutex);
        if (!EditorSessionActive || ActiveEditorSession.TempFile != saveFile->FileName)
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
        const wchar_t* message[] = { L"GitHub", error.c_str() };
        GPluginInfo.Message(&MainGuid, nullptr, FMSG_ERRORTYPE | FMSG_MB_OK, nullptr, message, 2, 1);
        return true;
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
    return context.Result.load();
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

void EndGitHubEditorSession()
{
    std::lock_guard<std::mutex> lock(EditorSessionMutex);
    ActiveEditorSession = {};
    EditorSessionActive = false;
}

intptr_t WINAPI ProcessSynchroEventW(const ProcessSynchroEventInfo* info)
{
    if (!info || info->StructSize < sizeof(*info) || info->Event != SE_COMMONSYNCHRO)
        return 0;

    auto* context = static_cast<ProgressContext*>(info->Param);
    if (!context || !context->Finished.load())
        return 0;

    if (context->Dialog != INVALID_HANDLE_VALUE)
        GPluginInfo.SendDlgMessage(context->Dialog, DM_CLOSE, 0, nullptr);

    return 0;
}

intptr_t WINAPI ProcessEditorEventW(const ProcessEditorEventInfo* info)
{
    if (!info || info->StructSize < sizeof(*info) || info->Event != EE_SAVE)
        return 0;

    const auto* saveFile = static_cast<const EditorSaveFile*>(info->Param);
    if (!saveFile)
        return 0;

    ProcessEditorSave(saveFile);
    return 0;
}
