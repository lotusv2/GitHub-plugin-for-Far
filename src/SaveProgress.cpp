#include "SaveProgress.hpp"
#include "plugin.hpp"

#include <string>

namespace
{
const wchar_t* WindowClassName = L"FarGitHubSaveProgress";

struct ProgressState
{
    HANDLE Ready = nullptr;
    HANDLE Thread = nullptr;
    HWND Window = nullptr;
    DWORD ThreadId = 0;
    std::wstring Text;
    DWORD TimeoutMs = 0;
};

LRESULT CALLBACK ProgressWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_CREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        const auto* state = static_cast<const ProgressState*>(create->lpCreateParams);
        CreateWindowExW(0, L"STATIC", state->Text.c_str(), WS_CHILD | WS_VISIBLE,
                        16, 18, 360, 28, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        return 0;
    }
    if (message == WM_TIMER)
    {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_CLOSE)
    {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY)
    {
        KillTimer(window, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool RegisterProgressClass()
{
    static bool registered = false;
    if (registered) return true;

    WNDCLASSEXW cls = {};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = ProgressWindowProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, IDC_WAIT);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.lpszClassName = WindowClassName;

    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    registered = true;
    return true;
}

DWORD WINAPI ProgressThreadProc(LPVOID parameter)
{
    auto* state = static_cast<ProgressState*>(parameter);
    state->ThreadId = GetCurrentThreadId();

    if (!RegisterProgressClass())
    {
        SetEvent(state->Ready);
        delete state;
        return 0;
    }

    const int width = 400;
    const int height = 85;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    state->Window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                    WindowClassName,
                                    L"GitHub",
                                    WS_POPUP | WS_CAPTION,
                                    x, y, width, height,
                                    nullptr, nullptr, GetModuleHandleW(nullptr), state);

    if (state->Window)
    {
        ShowWindow(state->Window, SW_SHOWNORMAL);
        UpdateWindow(state->Window);
        if (state->TimeoutMs != 0)
            SetTimer(state->Window, 1, state->TimeoutMs, nullptr);
    }

    SetEvent(state->Ready);

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (state->Window)
        state->Window = nullptr;

    if (state->TimeoutMs != 0)
    {
        CloseHandle(state->Ready);
        delete state;
    }
    return 0;
}
}

HANDLE ShowGitHubProgress(const wchar_t* text)
{
    auto* state = new ProgressState();
    state->Text = text ? text : L"GitHub: выполняется операция...";
    state->Ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!state->Ready)
    {
        delete state;
        return INVALID_HANDLE_VALUE;
    }

    state->Thread = CreateThread(nullptr, 0, ProgressThreadProc, state, 0, &state->ThreadId);
    if (!state->Thread)
    {
        CloseHandle(state->Ready);
        delete state;
        return INVALID_HANDLE_VALUE;
    }

    WaitForSingleObject(state->Ready, 2000);
    return reinterpret_cast<HANDLE>(state);
}

void CloseGitHubProgress(HANDLE handle)
{
    if (!handle || handle == INVALID_HANDLE_VALUE)
        return;

    auto* state = reinterpret_cast<ProgressState*>(handle);
    if (state->Window)
        PostMessageW(state->Window, WM_CLOSE, 0, 0);
    else
        PostThreadMessageW(state->ThreadId, WM_QUIT, 0, 0);

    WaitForSingleObject(state->Thread, INFINITE);
    CloseHandle(state->Thread);
    CloseHandle(state->Ready);
    delete state;
}

void ShowGitHubProgressTimed(const wchar_t* text, DWORD timeoutMs)
{
    auto* state = new ProgressState();
    state->Text = text ? text : L"GitHub: выполняется операция...";
    state->TimeoutMs = timeoutMs;
    state->Ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!state->Ready)
    {
        delete state;
        return;
    }

    state->Thread = CreateThread(nullptr, 0, ProgressThreadProc, state, 0, &state->ThreadId);
    if (!state->Thread)
    {
        CloseHandle(state->Ready);
        delete state;
        return;
    }

    CloseHandle(state->Thread);
    WaitForSingleObject(state->Ready, 2000);
}

intptr_t WINAPI ProcessEditorEventW(const ProcessEditorEventInfo* info)
{
    if (!info)
        return 0;

    if (info->Event == EE_SAVE)
        ShowGitHubProgressTimed(L"Сохранение файла...", 1200);

    return 0;
}
