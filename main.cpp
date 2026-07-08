// ==============================================
// main.cpp 20260420
// ==============================================
#include "main.h"

#include "resource.h"
#include "utils.h"

#include <windows.h>

#ifdef _MSC_VER
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "UxTheme.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(linker, \
"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

// ==============================================
// 전역 변수
// ==============================================
AppState* g_app = nullptr;



// ==============================================
// wWinMain 진입점
// ==============================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    if (!InitializeMainRuntime())
    {
        MessageBoxW(nullptr, L"KTin 초기화에 실패했습니다.", L"KTin", MB_ICONERROR | MB_OK);
        return 1;
    }

    static AppState app;
    g_app = &app;

    if (!RegisterMainWindowClasses(hInstance))
    {
        MessageBoxW(nullptr, L"KTin 창 클래스를 등록하지 못했습니다.", L"KTin", MB_ICONERROR | MB_OK);
        CleanupMainRuntime();
        g_app = nullptr;
        return 1;
    }

    HWND hwnd = CreateMainAppWindow(hInstance);
    if (!hwnd)
    {
        MessageBoxW(nullptr, L"KTin 메인 창을 생성하지 못했습니다.", L"KTin", MB_ICONERROR | MB_OK);
        CleanupMainRuntime();
        g_app = nullptr;
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    g_app->useCustomMudFont = true;

    g_app->hFontLog = GetCurrentAppFont(16, FW_NORMAL);

    // 자동 팝업
    if (g_app->autoShowQuickConnect)
        PostMessageW(hwnd, WM_COMMAND, ID_MENU_FILE_QUICK_CONNECT, 0);
    else if (g_app->autoShowAddressBook)
        PostMessageW(hwnd, WM_COMMAND, ID_MENU_FILE_ADDRESSBOOK, 0);

    const int exitCode = RunMainMessageLoop();

    CleanupMainRuntime();
    g_app = nullptr;
    return exitCode;
}

// ==============================================
// merged from app_state.cpp
// ==============================================
#include "app_state.h"

#include "terminal_buffer.h"

AppState::AppState() = default;
AppState::~AppState() = default;

FindState g_findState;

// ==============================================
// merged from main_runtime.cpp
// ==============================================
#include "main.h"

#include <commctrl.h>

namespace
{
    HMODULE g_hRichEdit = nullptr;
}

bool InitializeMainRuntime()
{
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_LINK_CLASS;

    // Common Controls v6 manifest가 누락된 환경에서도 메인 창은 반드시 뜨게 한다.
    // SysLink 등 일부 보조 컨트롤 초기화가 실패하더라도 기본 Win32 컨트롤로 계속 진행한다.
    if (!InitCommonControlsEx(&icc))
    {
        INITCOMMONCONTROLSEX fallback{};
        fallback.dwSize = sizeof(fallback);
        fallback.dwICC = ICC_WIN95_CLASSES;
        InitCommonControlsEx(&fallback);
    }

    // RichEdit은 일부 보조 창에서만 필요하다. 로드 실패를 프로그램 기동 실패로 처리하면
    // 더블클릭 실행 시 아무 창도 뜨지 않는 회귀가 된다.
    EnsureRichEditLoaded();
    return true;
}

bool EnsureRichEditLoaded()
{
    if (g_hRichEdit)
        return true;

    g_hRichEdit = LoadLibraryW(L"Msftedit.dll");
    if (!g_hRichEdit)
        g_hRichEdit = LoadLibraryW(L"Riched20.dll");

    return g_hRichEdit != nullptr;
}

void CleanupMainRuntime()
{
    if (g_hRichEdit)
    {
        FreeLibrary(g_hRichEdit);
        g_hRichEdit = nullptr;
    }
}

int RunMainMessageLoop()
{
    MSG msg{};
    BOOL result = 0;
    while ((result = GetMessageW(&msg, nullptr, 0, 0)) > 0)
    {
        if (DispatchModelessDialogMessage(msg))
            continue;

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (result < 0)
        return -1;

    return static_cast<int>(msg.wParam);
}

// ==============================================
// merged from main_message_loop.cpp
// ==============================================
#include "main.h"

#include "app_state.h"
#include "memo.h"

bool DispatchModelessDialogMessage(MSG& msg)
{
    if (g_findState.hwndDialog && IsWindow(g_findState.hwndDialog) &&
        IsDialogMessageW(g_findState.hwndDialog, &msg))
    {
        return true;
    }

    if (g_memoFind.hwndDialog && IsWindow(g_memoFind.hwndDialog) &&
        IsDialogMessageW(g_memoFind.hwndDialog, &msg))
    {
        return true;
    }

    if (g_app && g_app->hwndSymbol && IsWindow(g_app->hwndSymbol) &&
        IsDialogMessageW(g_app->hwndSymbol, &msg))
    {
        return true;
    }

    return false;
}

// ==============================================
// merged from main_timers.cpp
// ==============================================
#include "main.h"

#include "address_book.h"
#include "chat_capture.h"
#include "constants.h"
#include "settings.h"
#include "utils.h"
#include "win_util.h"

#include <memory>
#include <string>

void CancelMainTimers(HWND hwnd)
{
    if (!hwnd)
        return;

    KillWinTimer(hwnd, ID_TIMER_SESSION_UPTIME);
    KillWinTimer(hwnd, ID_TIMER_DEFER_SAVE);
    KillWinTimer(hwnd, ID_TIMER_LOG_REDRAW);
    KillWinTimer(hwnd, ID_TIMER_KEEPALIVE);
    KillWinTimer(hwnd, ID_TIMER_AUTORECONNECT);
    KillWinTimer(hwnd, ID_TIMER_SWITCH_CONNECT);
    KillWinTimer(hwnd, ID_TIMER_SWITCH_QUICK_CONNECT);
    KillWinTimer(hwnd, ID_TIMER_USER_ENGINE);
}

void ScheduleLogRedraw(HWND hwnd)
{
    if (!hwnd || !g_app || g_app->logRedrawPending)
        return;

    g_app->logRedrawPending = true;
    StartWinTimer(hwnd, ID_TIMER_LOG_REDRAW, 30);
}

bool HandleMainTimer(HWND hwnd, WPARAM timerId)
{
    switch (timerId)
    {
    case ID_TIMER_SESSION_UPTIME:
        UpdateSessionUptimeValue();
        return true;

    case ID_TIMER_DEFER_SAVE:
        KillWinTimer(hwnd, ID_TIMER_DEFER_SAVE);
        SaveWindowSettings(hwnd);
        return true;

    case ID_TIMER_LOG_REDRAW:
        KillWinTimer(hwnd, ID_TIMER_LOG_REDRAW);
        if (g_app)
            g_app->logRedrawPending = false;
        if (g_app && g_app->hwndLog && IsWindow(g_app->hwndLog))
            InvalidateTerminalDirtyRows(g_app->hwndLog);
        return true;

    case ID_TIMER_KEEPALIVE:
        SendKeepAliveNow();
        return true;

    case ID_TIMER_AUTORECONNECT:
        KillWinTimer(hwnd, ID_TIMER_AUTORECONNECT);
        if (g_app && g_app->hasActiveSession && g_app->activeSession.autoReconnect)
        {
            AddressBookEntry entry = g_app->activeSession;
            g_app->isConnected = false;
            ConnectAddressBookEntry(entry);
        }
        return true;

    case ID_TIMER_SWITCH_CONNECT:
        KillWinTimer(hwnd, ID_TIMER_SWITCH_CONNECT);
        if (g_app && g_app->hasPendingConnect)
        {
            AddressBookEntry entry = g_app->pendingConnectEntry;
            g_app->hasPendingConnect = false;
            ConnectAddressBookEntry(entry);
        }
        return true;

    case ID_TIMER_SWITCH_QUICK_CONNECT:
        KillWinTimer(hwnd, ID_TIMER_SWITCH_QUICK_CONNECT);
        if (g_app && g_app->hasPendingQuickConnect)
        {
            std::wstring charsetCmd = g_app->pendingQuickCharsetCommand;
            std::wstring sessionCmd = g_app->pendingQuickConnectCommand;

            g_app->pendingQuickCharsetCommand.clear();
            g_app->pendingQuickConnectCommand.clear();
            g_app->hasPendingQuickConnect = false;

            if (!Trim(charsetCmd).empty())
                SendRawCommandToMud(charsetCmd);
            if (!Trim(sessionCmd).empty())
            {
                SendRawCommandToMud(sessionCmd);
                MarkKnownTinTinSession(L"new");
            }
        }
        return true;
    }

    return false;
}

// ==============================================
// merged from ui_resources.cpp
// ==============================================
#include "main.h"

#include "win_util.h"

namespace
{
    HBRUSH BuildBrush(HBRUSH& slot, COLORREF color)
    {
        UniqueGdiObject next(CreateSolidBrush(color));
        if (!next.IsValid())
            return slot;

        ResetGdiObjectRef(slot);
        slot = static_cast<HBRUSH>(next.Release());
        return slot;
    }
}

HBRUSH GetMainBackBrush()
{
    if (!g_app)
        return GetSysColorBrush(COLOR_WINDOW);

    const COLORREF color = g_app->mainBackColor;
    if (!g_app->hbrMainBack || g_app->hbrMainBackColor != color)
    {
        BuildBrush(g_app->hbrMainBack, color);
        g_app->hbrMainBackColor = color;
    }

    return g_app->hbrMainBack ? g_app->hbrMainBack : GetSysColorBrush(COLOR_WINDOW);
}

HBRUSH GetInputContainerBrush()
{
    if (!g_app)
        return GetSysColorBrush(COLOR_WINDOW);

    if (!g_app->hbrInputContainer)
        BuildBrush(g_app->hbrInputContainer, g_app->inputStyle.backColor);

    return g_app->hbrInputContainer ? g_app->hbrInputContainer : GetSysColorBrush(COLOR_WINDOW);
}

HBRUSH GetInputEditBrush()
{
    if (!g_app)
        return GetSysColorBrush(COLOR_WINDOW);

    if (!g_app->hbrInputEdit)
        BuildBrush(g_app->hbrInputEdit, g_app->inputStyle.backColor);

    return g_app->hbrInputEdit ? g_app->hbrInputEdit : GetSysColorBrush(COLOR_WINDOW);
}

void ResetInputBrushCache(COLORREF color)
{
    if (!g_app)
        return;

    BuildBrush(g_app->hbrInputContainer, color);
    BuildBrush(g_app->hbrInputEdit, color);
    BuildBrush(g_app->hbrInputEditActive, color);
}

void CleanupAppGdiResources()
{
    if (!g_app)
        return;

    ResetGdiObjectRef(g_app->hFontLog);
    ResetGdiObjectRef(g_app->hFontInput);
    ResetGdiObjectRef(g_app->hFontChat);

    ResetGdiObjectRef(g_app->hbrInputContainer);
    ResetGdiObjectRef(g_app->hbrInputEdit);
    ResetGdiObjectRef(g_app->hbrInputEditActive);
    ResetGdiObjectRef(g_app->hbrMainBack);
    g_app->hbrMainBackColor = CLR_INVALID;
}

// ==============================================
// merged from session_state.cpp
// ==============================================
#include "main.h"

#include "constants.h"
#include "variables.h"
#include "win_util.h"

#include <ctime>
#include <cwchar>

namespace
{
    VariableItem* FindSessionVariable(const wchar_t* name)
    {
        if (!g_app || !name)
            return nullptr;

        for (auto& var : g_app->variables)
        {
            if (_wcsicmp(var.name.c_str(), name) == 0)
                return &var;
        }
        return nullptr;
    }
}

void SetSessionVariableValue(const std::wstring& name, const std::wstring& value)
{
    if (!g_app)
        return;

    if (VariableItem* existing = FindSessionVariable(name.c_str()))
    {
        existing->value = value;
        return;
    }

    VariableItem var;
    var.enabled = true;
    var.type = 0;
    var.name = name;
    var.value = value;
    g_app->variables.push_back(var);
}

void ResetSessionUptimeValue()
{
    if (VariableItem* uptime = FindSessionVariable(L"uptime"))
        uptime->value = L"00:00:00";
}

void UpdateSessionUptimeValue()
{
    if (!g_app || !g_app->isSessionActive)
        return;

    const time_t now = time(nullptr);
    int diff = static_cast<int>(now - g_app->sessionStartTime);
    if (diff < 0)
        diff = 0;

    wchar_t buf[32] = {};
    swprintf(buf, 32, L"%02d:%02d:%02d", diff / 3600, (diff % 3600) / 60, diff % 60);
    SetSessionVariableValue(L"uptime", buf);

    if (g_app->hwndStatusBar)
        InvalidateRect(g_app->hwndStatusBar, nullptr, FALSE);
}

void SetSessionActiveState(HWND hwnd, bool active)
{
    if (!g_app)
        return;

    if (active)
    {
        if (!g_app->isSessionActive)
        {
            g_app->isSessionActive = true;
            g_app->sessionStartTime = time(nullptr);
            StartWinTimer(hwnd, ID_TIMER_SESSION_UPTIME, 1000);
        }
        return;
    }

    g_app->isSessionActive = false;
    g_app->autoLoginWindowActive = false;
    g_app->keepAliveBlockedUntilTick = 0;
    KillWinTimer(hwnd, ID_TIMER_SESSION_UPTIME);
    ResetSessionUptimeValue();
}

// ==============================================
// merged from global_shortcuts.cpp
// ==============================================
#include "main.h"

#include "resource.h"

bool HandleGlobalMenuShortcut(HWND hwnd, UINT msg, WPARAM wParam)
{
    if (!g_app)
        return false;

    const bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
    const bool alt   = (GetKeyState(VK_MENU)    & 0x8000) != 0;

    HWND target = g_app->hwndMain ? g_app->hwndMain : hwnd;

    if (msg == WM_SYSKEYDOWN && alt && !ctrl && !shift)
    {
        switch (static_cast<int>(wParam))
        {
        case 'Q':
            SendMessageW(target, WM_COMMAND, ID_MENU_FILE_QUICK_CONNECT, 0);
            return true;
        case 'A':
            SendMessageW(target, WM_COMMAND, ID_MENU_FILE_ADDRESSBOOK, 0);
            return true;
        case 'V':
            SendMessageW(target, WM_COMMAND, ID_MENU_EDIT_MEMO, 0);
            return true;
        case 'X':
            SendMessageW(target, WM_COMMAND, ID_MENU_EXIT, 0);
            return true;
        case 'S':
            SendMessageW(target, WM_COMMAND, ID_MENU_FILE_READ_SCRIPT, 0);
            return true;
        }
    }

    if (msg == WM_KEYDOWN && ctrl && !alt && !shift)
    {
        if (wParam == 'F')
        {
            SendMessageW(target, WM_COMMAND, ID_MENU_FIND_DIALOG, 0);
            return true;
        }
        if (wParam == VK_F9)
        {
            SendMessageW(target, WM_COMMAND, ID_MENU_FILE_ZAP, 0);
            return true;
        }
    }

    if (msg == WM_KEYDOWN && !ctrl && !alt && !shift && wParam == VK_F4)
    {
        SendMessageW(target, WM_COMMAND, ID_MENU_VIEW_SYMBOLS, 0);
        return true;
    }

    return false;
}

// ==============================================
// merged from main_layout.cpp
// ==============================================
#include "main.h"

#include "constants.h"
#include "input.h"
#include "process_manager.h"
#include "shortcut_bar.h"
#include "status_bar.h"
#include "terminal_buffer.h"
#include "utils.h"
#include "win_util.h"

void LayoutChildren(HWND hwnd)
{
    if (!g_app || !hwnd) return;

    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int width = RectWidth(rc);
    const int height = RectHeight(rc);
    if (width <= 0 || height <= 0) return;

    const int menuHeight = (!g_app->menuHidden) ? g_app->customMenuHeight : 0;
    const int chatHeight = 0;
    const int chatSeparatorHeight = 0;

    const int inputHeight = max(58, GetInputAreaHeight());
    const int shortcutHeight = g_app->shortcutBarVisible ? SHORTCUT_BAR_HEIGHT : 0;
    const int statusHeight = GetStatusBarHeight();
    const int logHeight = max(80, height - menuHeight - shortcutHeight - inputHeight - statusHeight - chatHeight - chatSeparatorHeight);
    const int logTop = menuHeight;
    const int shortcutTop = logTop + logHeight;
    const int inputTop = shortcutTop + shortcutHeight;
    const int statusTop = inputTop + inputHeight;

    HDWP hdwp = BeginDeferWindowPos(5);
    if (hdwp && g_app->hwndLog && IsWindow(g_app->hwndLog))
        hdwp = DeferWindowPos(hdwp, g_app->hwndLog, nullptr, 0, logTop, width, logHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    if (hdwp && g_app->hwndShortcutBar && IsWindow(g_app->hwndShortcutBar))
        hdwp = DeferWindowPos(hdwp, g_app->hwndShortcutBar, nullptr, 0, shortcutTop, width, shortcutHeight > 0 ? shortcutHeight : 0, SWP_NOZORDER | SWP_NOACTIVATE | (g_app->shortcutBarVisible && shortcutHeight > 0 ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    if (hdwp && g_app->hwndInput && IsWindow(g_app->hwndInput))
        hdwp = DeferWindowPos(hdwp, g_app->hwndInput, nullptr, 0, inputTop, width, inputHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    if (hdwp && g_app->hwndStatusBar && IsWindow(g_app->hwndStatusBar))
        hdwp = DeferWindowPos(hdwp, g_app->hwndStatusBar, nullptr, 0, statusTop, width, statusHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    if (hdwp)
        EndDeferWindowPos(hdwp);

    if (g_app->termBuffer)
    {
        TerminalBufferMetrics metrics = g_app->termBuffer->GetMetrics();
        if (metrics.height != g_app->screenRows || metrics.width != g_app->screenCols)
        {
            g_app->termBuffer->Resize(g_app->screenCols, g_app->screenRows);
            if (g_app->proc.hPC)
                ResizePseudoConsoleToLogWindow();
        }
    }

    if (g_app->hwndShortcutBar)
        ShowWindow(g_app->hwndShortcutBar, g_app->shortcutBarVisible ? SW_SHOW : SW_HIDE);

    if (g_app->shortcutBarVisible && g_app->hwndShortcutBar)
    {
        const int buttonWidth = max(28, (width - 8 - 4 * (SHORTCUT_BUTTON_COUNT - 1)) / SHORTCUT_BUTTON_COUNT);
        const int buttonHeight = max(20, shortcutHeight - 6);
        for (int i = 0, x = 4; i < SHORTCUT_BUTTON_COUNT; ++i, x += buttonWidth + 4)
        {
            if (g_app->hwndShortcutButtons[i])
            {
                MoveWindow(g_app->hwndShortcutButtons[i], x, 3, buttonWidth, buttonHeight, TRUE);
                ShowWindow(g_app->hwndShortcutButtons[i], SW_SHOW);
            }
        }
    }
    else
    {
        for (int i = 0; i < SHORTCUT_BUTTON_COUNT; ++i)
        {
            if (g_app->hwndShortcutButtons[i])
                ShowWindow(g_app->hwndShortcutButtons[i], SW_HIDE);
        }
    }

    LayoutInputEdits();
    InvalidateRect(hwnd, nullptr, FALSE);
}

// ==============================================
// merged from main_shutdown.cpp
// ==============================================
#include "main.h"

#include "address_book.h"
#include "auto_login.h"
#include "chat_capture.h"
#include "dialogs.h"
#include "highlight.h"
#include "numpad.h"
#include "process_manager.h"
#include "settings.h"
#include "terminal_buffer.h"
#include "timer.h"
#include "utils.h"
#include "variables.h"
#include "win_util.h"

bool ShutdownMainWindow(HWND hwnd)
{
    HideTrayIcon(hwnd);

    CancelMainTimers(hwnd);

    UnloadEmbeddedFont();
    CleanupCachedUiFonts();
    CleanupDialogResources();

    SaveWindowSettings(hwnd);
    SaveKeepAliveSettings();
    SaveShortcutSettings();
    SaveInputHistorySettings();
    SaveCaptureLogSettings();
    SaveScreenSizeSettings();
    SaveChatCaptureSettings();
    SaveAddressBook();
    SaveFontRenderSettings();
    SaveHighlightSettings();
    SaveAutoLoginSettings();
    StopCaptureLog();
    CloseChatLog();
    StopProcessAndThread();
    SaveVariableSettings();
    SaveNumpadSettings();
    SaveGeneralSettings();
    SaveTimerSettings();

    if (g_app)
        g_app->termBuffer.reset();

    UnregisterHotKey(hwnd, ID_HOTKEY_FIND_DIALOG);
    UnregisterHotKey(hwnd, ID_HOTKEY_FIND_NEXT);
    UnregisterHotKey(hwnd, ID_HOTKEY_FIND_PREV);

    if (g_app)
        ResetMenuRef(g_app->hMainMenu);

    CleanupAppGdiResources();

    CleanupMainRuntime();

    PostQuitMessage(0);
    return true;
}

// ==============================================
// merged from main_startup.cpp
// ==============================================
#include "main.h"

#include "abbreviation.h"
#include "address_book.h"
#include "auto_login.h"
#include "chat_capture.h"
#include "functionkey.h"
#include "highlight.h"
#include "input.h"
#include "memo.h"
#include "numpad.h"
#include "process_manager.h"
#include "settings.h"
#include "shortcut_bar.h"
#include "status_bar.h"
#include "terminal_buffer.h"
#include "theme.h"
#include "timer.h"
#include "utils.h"
#include "variables.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <windows.h>

#include <new>

namespace
{
    void DestroyWindowIfOpen(HWND& hwnd)
    {
        if (hwnd && IsWindow(hwnd))
            DestroyWindow(hwnd);
        hwnd = nullptr;
    }

    void CleanupPartialMainCreate()
    {
        if (!g_app)
            return;

        for (HWND& edit : g_app->hwndEdit)
            DestroyWindowIfOpen(edit);

        DestroyWindowIfOpen(g_app->hwndShortcutBar);
        DestroyWindowIfOpen(g_app->hwndStatusBar);
        DestroyWindowIfOpen(g_app->hwndInput);
        DestroyWindowIfOpen(g_app->hwndLog);

        g_app->termBuffer.reset();

        CleanupAppGdiResources();
        StopProcessAndThread();
    }

    LRESULT FailMainCreate(HWND hwnd, const wchar_t* message)
    {
        CleanupPartialMainCreate();
        MessageBoxW(hwnd, message, L"오류", MB_ICONERROR | MB_OK);
        PostQuitMessage(1);
        return -1;
    }
}

LRESULT HandleMainCreate(HWND hwnd)
{
    // [중요] 다른 폰트 로직이 돌아가기 전에 가장 먼저 등록해야 합니다!
    RegisterEmbeddedFont(); 
    g_app->hwndMain = hwnd;
    g_app->mainBackColor = RGB(45, 45, 48);        
    
    LoadFontRenderSettings();
    LoadKeepAliveSettings();
    LoadInputHistorySettings();
    LoadCaptureLogSettings();
    LoadChatCaptureSettings();
    LoadScreenSizeSettings();
    LoadAddressBook();
    LoadQuickConnectHistory();
    LoadHighlightSettings();
    
    // 안전판: 장시간 실행 먹통의 주 원인이 되는 실시간 트리거/채팅 캡처/자동 갈무리는 기본 비활성화합니다.
    // 찾기, 지난/현재 화면 복사/저장, 주소록, 자동 로그인, 단축키, 메모장 등은 유지합니다.
    g_hiState.active = false;
    g_hiState.rules.clear();
    g_app->chatCaptureEnabled = false;
    g_app->chatVisible = false;
    // 전체 수신 갈무리는 화면 캡처/정규식 기능이 아니므로 사용자 설정을 유지합니다.
    g_app->chatTimestampEnabled = false;
    
    // 자동 로그인은 실제 접속 명령(#session/#ses/#connect, 빠른연결, 주소록 연결)이
    // 발생했을 때만 60초 동안 검사합니다.
    // 프로그램 시작 직후에는 아직 서버 세션이 없을 수 있으므로 여기서 자동 로그인 창을
    // 열지 않습니다. 그렇지 않으면 60초 뒤 접속유지가 로그인 전/미접속 상태에서
    // 실행될 수 있습니다.
    LoadAutoLoginSettings();
    LoadVariableSettings();
    LoadNumpadSettings();
    LoadGeneralSettings();
    LoadShortcutSettings();
    LoadAbbreviationSettings();
    LoadTimerSettings();
    
    // ★ 여기서 LoadWindowSettings를 호출하여 파일 상태를 완벽히 가져옴
    LoadWindowSettings(hwnd);
    
    // 1. 로그창 스타일 설정 (함수 호출 한 줄로 끝!)
    InitStyleFont(g_app->logStyle.font, hwnd, 12);
    g_app->logStyle.font.lfCharSet = HANGEUL_CHARSET;
    g_app->logStyle.font.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    g_app->logStyle.font.lfOutPrecision = OUT_TT_ONLY_PRECIS;
    
    g_app->logStyle.textColor = RGB(220, 220, 220);
    g_app->logStyle.backColor = RGB(0, 0, 0);
    
    // 2. 입력창 스타일 설정 (함수 호출 한 줄로 끝!)
    InitStyleFont(g_app->inputStyle.font, hwnd, 12);
    g_app->inputStyle.textColor = RGB(230, 230, 230);
    g_app->inputStyle.backColor = RGB(20, 20, 20);
    
    g_app->chatStyle = g_app->logStyle;
    
    int initialLogRows = g_app->screenRows;
    
    g_app->termBuffer.reset(new (std::nothrow) TerminalBuffer(g_app->screenCols, initialLogRows));
    if (!g_app->termBuffer)
        return FailMainCreate(hwnd, L"터미널 버퍼 초기화에 실패했습니다.");
    
    g_app->hwndLog = CreateWindowExW(0, kTerminalWindowClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 100, 100, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    
    g_app->hwndInput = CreateWindowExW(0, kInputContainerClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 100, 100, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    g_app->hwndStatusBar = CreateWindowExW(0, kStatusBarClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 100, STATUS_BAR_HEIGHT, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    // WM_CREATE 내부의 hwndShortcutBar 생성 부분
    g_app->hwndShortcutBar = CreateWindowExW(
        0,
        kShortcutBarClass,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 100, SHORTCUT_BAR_HEIGHT,
        hwnd,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    
    if (!g_app->hwndLog || !g_app->hwndInput || !g_app->hwndStatusBar || !g_app->hwndShortcutBar)
        return FailMainCreate(hwnd, L"메인 UI 컨트롤 생성에 실패했습니다.");

    SendMessageW(g_app->hwndShortcutBar, WM_SETFONT, (WPARAM)g_app->hFontInput, TRUE);
    
    CreateMainMenu(hwnd);
    
    ApplyStyles();
    
    for (int i = 0; i < INPUT_ROWS; ++i)
    {
        g_app->hwndEdit[i] = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NOHIDESEL, 0, 0, 100, 24, g_app->hwndInput, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!g_app->hwndEdit[i])
            return FailMainCreate(hwnd, L"입력창 생성에 실패했습니다.");
        SendMessageW(g_app->hwndEdit[i], WM_SETFONT, (WPARAM)g_app->hFontInput, TRUE);
        SendMessageW(g_app->hwndEdit[i], EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
        SetWindowTheme(g_app->hwndEdit[i], L"", L"");
        g_app->oldEditProc[i] = (WNDPROC)SetWindowLongPtrW(g_app->hwndEdit[i], GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    }
    
    FitWindowToScreenGrid(hwnd, g_app->screenCols, g_app->screenRows, false);
    if (g_app->mainAlwaysOnTop)
    {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    LayoutChildren(hwnd);
    
    // TinTin++/ConPTY 백엔드는 창이 화면에 올라간 뒤 비동기로 시작한다.
    // 백엔드 실패가 메인 창 생성 실패로 이어지면 더블클릭 실행 시 아무 창도 뜨지 않는 회귀가 된다.
    PostMessageW(hwnd, WM_APP_START_BACKEND, 0, 0);

    g_app->activeEditIndex = 0;
    SetFocus(g_app->hwndEdit[0]);
    SendMessageW(g_app->hwndEdit[0], EM_SETSEL, -1, -1);
    EnsureVisibleEditCaret(g_app->hwndEdit[0]);
    InitializeShortcutButtons();
    ApplyShortcutButtons(hwnd);
    SetInputViewLatest();
    
    if (!g_app->history.empty())
    {
        int n = (int)g_app->history.size();
        for (int i = 0; i < INPUT_ROWS; ++i) g_app->displayedHistoryIndex[i] = -1;
        if (n == 1) g_app->displayedHistoryIndex[1] = n - 1;
        else { g_app->displayedHistoryIndex[0] = n - 2; g_app->displayedHistoryIndex[1] = n - 1; }
        g_app->displayedHistoryIndex[2] = -1;
        ApplyInputView();
        FocusInputRow(2);
    }
    
    ApplyKeepAliveTimer(hwnd);
    
    if (g_app && g_app->captureLogEnabled)
        StartCaptureLog();
    
    ResetSessionUptimeValue();
    
    return 0;
}

// ==============================================
// merged from main_events.cpp
// ==============================================
#include "main.h"

#include "address_book.h"
#include "chat_capture.h"
#include "constants.h"
#include "log_tail.h"
#include "process_manager.h"
#include "settings.h"
#include "status_bar.h"
#include "terminal_buffer.h"
#include "utils.h"
#include "win_util.h"

#include "highlight.h"
#include "numpad.h"
#include "timer.h"

#include <windowsx.h>

#include <memory>
#include <string>


namespace
{
    void RestoreMainWindow(HWND hwnd)
    {
        HideTrayIcon(hwnd);
        ShowWindow(hwnd, SW_SHOW);
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
    }

}

bool HandleMainInitMenuPopup(HMENU menu)
{
    if (!g_app || !menu || !g_app->termBuffer)
        return true;

    const bool hasHistory = g_app->termBuffer->HasHistory();
    const UINT state = hasHistory ? MF_ENABLED : MF_GRAYED;
    EnableMenuItem(menu, ID_MENU_EDIT_COPY_PAST, MF_BYCOMMAND | state);
    EnableMenuItem(menu, ID_MENU_EDIT_SAVE_PAST, MF_BYCOMMAND | state);
    ModifyMenuW(menu, ID_MENU_CAPTURE_TOGGLE, MF_BYCOMMAND | MF_STRING,
                ID_MENU_CAPTURE_TOGGLE,
                g_app->captureLogEnabled ? L"갈무리 켜짐" : L"갈무리 꺼짐");
    CheckMenuItem(menu, ID_MENU_CAPTURE_TOGGLE,
                  MF_BYCOMMAND | (g_app->captureLogEnabled ? MF_CHECKED : MF_UNCHECKED));
    return true;
}

bool HandleMainMenuMouseDown(HWND hwnd, LPARAM lParam)
{
    if (!g_app)
        return false;

    if (!g_app->menuHidden)
    {
        const int hit = HitTestCustomMenuBar(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (hit >= 0)
        {
            ShowCustomMenuPopup(hwnd, hit);
            return true;
        }
        return false;
    }

    g_app->menuHidden = false;
    LayoutChildren(hwnd);
    InvalidateRect(hwnd, nullptr, TRUE);
    return true;
}

bool HandleMainMenuMouseMove(HWND hwnd, LPARAM lParam)
{
    if (!g_app || g_app->menuHidden)
        return false;

    const int hit = HitTestCustomMenuBar(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    if (hit != g_app->hotMenuIndex)
    {
        g_app->hotMenuIndex = hit;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    return false;
}

bool HandleMainHiddenMenuContext(HWND hwnd, UINT msg, LPARAM lParam)
{
    if (!g_app || !g_app->menuHidden)
        return false;

    UniqueMenu menu(CreatePopupMenu());
    if (!menu.IsValid())
        return false;

    AppendMenuW(menu.Get(), MF_STRING, ID_LOG_SHOW_MENU, L"상단 메뉴 보이기");

    POINT pt{};
    if (msg == WM_CONTEXTMENU && lParam != static_cast<LPARAM>(-1))
    {
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
    }
    else
    {
        GetCursorPos(&pt);
    }

    TrackPopupMenu(menu.Get(), TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
                   pt.x, pt.y, 0, hwnd, nullptr);
    return true;
}

namespace
{
    RECT s_lastMainWindowRect = {};
    bool s_hasLastMainWindowRect = false;

    void TrackMainWindowRect(HWND hwnd, bool saveNow)
    {
        if (!hwnd || IsIconic(hwnd))
            return;

        RECT now{};
        if (GetWindowRect(hwnd, &now))
        {
            if (s_hasLastMainWindowRect)
                TailNotifyMainWindowMoved(hwnd, s_lastMainWindowRect, now);

            s_lastMainWindowRect = now;
            s_hasLastMainWindowRect = true;
        }

        if (saveNow)
            SaveWindowSettings(hwnd);
        else
            QueueSaveWindowSettings(hwnd);
    }
}

bool HandleMainSize(HWND hwnd)
{
    LayoutChildren(hwnd);
    QueueSaveWindowSettings(hwnd);

    if (g_app && g_app->hwndShortcutBar)
        InvalidateRect(g_app->hwndShortcutBar, nullptr, FALSE);

    if (g_app && g_app->hwndInput)
        InvalidateRect(g_app->hwndInput, nullptr, FALSE);

    return true;
}

bool HandleMainMove(HWND hwnd)
{
    TrackMainWindowRect(hwnd, false);
    return true;
}

bool HandleMainExitSizeMove(HWND hwnd)
{
    TrackMainWindowRect(hwnd, true);
    return true;
}

bool HandleMainLogChunk(HWND hwnd, LPARAM)
{
    if (g_app)
        ScheduleLogRedraw(hwnd);
    return true;
}


void HandleMainVarUpdate(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    std::unique_ptr<std::wstring> name(reinterpret_cast<std::wstring*>(wParam));
    std::unique_ptr<std::wstring> value(reinterpret_cast<std::wstring*>(lParam));
    if (!g_app || !name || !value)
        return;

    if (_wcsicmp(name->c_str(), L"session_active") == 0)
        SetSessionActiveState(hwnd, *value == L"1");

    SetSessionVariableValue(*name, *value);

    if (g_app->hwndStatusBar)
        InvalidateRect(g_app->hwndStatusBar, nullptr, TRUE);
}


bool HandleMainStartBackend(HWND hwnd)
{
    if (!g_app || g_app->shuttingDown)
        return true;

    if (g_app->proc.process || g_app->readerThread.joinable())
        return true;

    g_app->shuttingDown = false;

    if (!StartTinTinProcess())
    {
        MessageBoxW(hwnd, L"TinTin++ 백엔드(bin\\tt++.exe 또는 ConPTY)를 시작하지 못했습니다.\nGUI는 계속 사용할 수 있습니다.", L"KTin", MB_ICONWARNING | MB_OK);
        return true;
    }

    try
    {
        g_app->readerThread = std::thread(ReaderThreadProc, hwnd, g_app->proc.stdoutRead);
    }
    catch (...)
    {
        StopProcessAndThread();
        g_app->shuttingDown = false;
        MessageBoxW(hwnd, L"TinTin++ 출력 읽기 스레드를 시작하지 못했습니다.\nGUI는 계속 사용할 수 있습니다.", L"KTin", MB_ICONWARNING | MB_OK);
        return true;
    }

    return true;
}

bool HandleMainTrayIcon(HWND hwnd, LPARAM lParam)
{
    if (lParam == WM_LBUTTONUP)
    {
        RestoreMainWindow(hwnd);
        return true;
    }

    if (lParam != WM_RBUTTONUP && lParam != WM_CONTEXTMENU)
        return true;

    POINT pt{};
    GetCursorPos(&pt);

    UniqueMenu menu(CreatePopupMenu());
    if (!menu.IsValid())
        return true;

    AppendMenuW(menu.Get(), MF_OWNERDRAW | MF_STRING, 1001, L"Ktin:TinTin++ GUI 열기(&O)");
    AppendMenuW(menu.Get(), MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu.Get(), MF_OWNERDRAW | MF_STRING, ID_MENU_EXIT, L"완전히 종료하기(&X)");

    SetForegroundWindow(hwnd);
    const int cmd = TrackPopupMenu(
        menu.Get(),
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);

    if (cmd == 1001)
        RestoreMainWindow(hwnd);
    else if (cmd == ID_MENU_EXIT)
        SendMessageW(hwnd, WM_COMMAND, ID_MENU_EXIT, 0);

    return true;
}

bool HandleMainPaint(HWND hwnd)
{
    ScopedPaintDC paint(hwnd);
    HDC hdc = paint.Get();
    if (!hdc)
        return true;

    RECT rc{};
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, GetMainBackBrush());
    DrawCustomMenuBar(hdc, hwnd);
    return true;
}

bool HandleMainFocus(HWND hwnd)
{
    (void)hwnd;

    if (!g_app)
        return true;

    int idx = g_app->activeEditIndex;
    if (idx < 0 || idx >= INPUT_ROWS)
        idx = 0;

    g_app->activeEditIndex = idx;

    if (g_app->hwndEdit[idx] && GetFocus() != g_app->hwndEdit[idx])
        SetFocus(g_app->hwndEdit[idx]);

    return true;
}

bool HandleMainClose(HWND hwnd)
{
    if (g_app && g_app->closeToTray)
    {
        SaveWindowSettings(hwnd);
        ShowTrayIcon(hwnd);
        ShowWindow(hwnd, SW_HIDE);
        return true;
    }

    DestroyWindow(hwnd);
    return true;
}

bool HandleMainProcessExit(HWND hwnd)
{
    (void)hwnd;

    if (g_app && !g_app->shuttingDown && g_app->hwndInput)
        EnableWindow(g_app->hwndInput, FALSE);
    return true;
}

bool HandleMainDestroy(HWND hwnd)
{
    return ShutdownMainWindow(hwnd);
}

// ==============================================
// merged from main_commands.cpp
// ==============================================
#include "main.h"

#include "constants.h"
#include "types.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "theme.h"
#include "highlight.h"
#include "variables.h"
#include "abbreviation.h"
#include "settings.h"
#include "functionkey.h"
#include "numpad.h"
#include "chat_capture.h"
#include "shortcut_bar.h"
#include "status_bar.h"
#include "memo.h"
#include "dialogs.h"
#include "auto_login.h"
#include "input.h"
#include "timer.h"
#include "address_book.h"
#include "log_tail.h"
#include "win_util.h"
#include "resource.h"

#include <shellapi.h>
#include <memory>
#include <new>
#include <string>
#include <thread>

namespace {

void LaunchNewKtinWindow(HWND owner)
{
    wchar_t exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH) || exePath[0] == L'\0')
    {
        MessageBoxW(owner, L"실행 파일 경로를 찾을 수 없습니다.", L"새 창 띄우기", MB_OK | MB_ICONERROR);
        return;
    }

    HINSTANCE r = ShellExecuteW(owner, L"open", exePath, nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32)
        MessageBoxW(owner, L"새 KTin 창을 실행하지 못했습니다.", L"새 창 띄우기", MB_OK | MB_ICONERROR);
}

void RefocusActiveInput()
{
    if (!g_app)
        return;

    int idx = g_app->activeEditIndex;
    if (idx < 0 || idx >= INPUT_ROWS)
        idx = 0;

    HWND hEdit = g_app->hwndEdit[idx];
    if (!hEdit)
        return;

    SetFocus(hEdit);
    SendMessageW(hEdit, EM_SETSEL, -1, -1);
    EnsureVisibleEditCaret(hEdit);
}

enum class HistoryExportMode
{
    CopyPast,
    SavePast
};

struct HistoryExportResult
{
    HistoryExportMode mode = HistoryExportMode::CopyPast;
    std::wstring text;
    std::wstring path;
    bool ok = false;
    bool empty = false;
};

bool ChooseHistorySavePath(HWND hwnd, std::wstring& outPath)
{
    wchar_t fileName[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"텍스트 파일 (*.txt)\0*.txt\0모든 파일 (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"txt";

    if (!GetSaveFileNameW(&ofn))
        return false;

    outPath.assign(fileName);
    return !outPath.empty();
}

void PostHistoryExportResult(HWND hwnd, HistoryExportResult* result)
{
    if (!result)
        return;

    if (!IsWindow(hwnd) ||
        !PostMessageW(hwnd, WM_APP_HISTORY_EXPORT_DONE, 0, reinterpret_cast<LPARAM>(result)))
    {
        delete result;
    }
}

void StartHistoryCopyAsync(HWND hwnd)
{
    if (!g_app || !g_app->termBuffer)
        return;

    std::shared_ptr<TerminalBuffer> buffer = g_app->termBuffer;
    std::thread([hwnd, buffer]() {
        HistoryExportResult* result = new (std::nothrow) HistoryExportResult();
        if (!result)
            return;

        result->mode = HistoryExportMode::CopyPast;
        result->text = buffer ? buffer->GetHistoryText() : std::wstring();
        result->empty = result->text.empty();
        result->ok = !result->empty;
        PostHistoryExportResult(hwnd, result);
    }).detach();
}

void StartHistorySaveAsync(HWND hwnd)
{
    if (!g_app || !g_app->termBuffer)
        return;

    std::wstring path;
    if (!ChooseHistorySavePath(hwnd, path))
        return;

    std::shared_ptr<TerminalBuffer> buffer = g_app->termBuffer;
    std::thread([hwnd, buffer, path]() {
        HistoryExportResult* result = new (std::nothrow) HistoryExportResult();
        if (!result)
            return;

        result->mode = HistoryExportMode::SavePast;
        result->path = path;
        std::wstring text = buffer ? buffer->GetHistoryText() : std::wstring();
        result->empty = text.empty();
        if (!result->empty)
            result->ok = WriteUtf8BomTextFile(path, WideToUtf8(text));

        PostHistoryExportResult(hwnd, result);
    }).detach();
}

} // namespace

bool HandleMainHistoryExportDone(HWND hwnd, LPARAM lParam)
{
    std::unique_ptr<HistoryExportResult> result(reinterpret_cast<HistoryExportResult*>(lParam));
    if (!result)
        return true;

    if (result->mode == HistoryExportMode::CopyPast)
    {
        if (result->empty)
        {
            MessageBoxW(hwnd, L"복사할 지난 화면 내용이 없습니다.", L"알림", MB_OK | MB_ICONWARNING);
            return true;
        }

        CopyToClipboard(hwnd, result->text);
        return true;
    }

    if (result->empty)
    {
        MessageBoxW(hwnd, L"저장할 지난 화면 내용이 없습니다.", L"알림", MB_OK | MB_ICONWARNING);
        return true;
    }

    MessageBoxW(hwnd,
        result->ok ? L"파일이 성공적으로 저장되었습니다." : L"파일 저장 중 오류가 발생했습니다.",
        result->ok ? L"저장 완료" : L"저장 실패",
        MB_OK | (result->ok ? MB_ICONINFORMATION : MB_ICONERROR));
    return true;
}


bool HandleMainCommand(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)msg;
    (void)lParam;

switch (LOWORD(wParam))
        {
        case ID_MENU_EDIT_COPY_PAST:
            StartHistoryCopyAsync(hwnd);
            return true;
        case ID_MENU_EDIT_SAVE_PAST:
            StartHistorySaveAsync(hwnd);
            return true;
        case ID_MENU_EDIT_COPY_CUR:
            if (g_app && g_app->termBuffer) CopyToClipboard(hwnd, g_app->termBuffer->GetCurrentScreenText());
            return true;
        case ID_MENU_EDIT_SAVE_CUR:
            if (g_app && g_app->termBuffer) SaveTextToFile(hwnd, g_app->termBuffer->GetCurrentScreenText());
            return true;

        case ID_MENU_CAPTURE_TOGGLE:
        {
            if (g_app)
            {
                g_app->captureLogEnabled = !g_app->captureLogEnabled;
                if (g_app->captureLogEnabled)
                    StartCaptureLog();
                else
                    StopCaptureLog();
                SaveCaptureLogSettings();
                UpdateMenuToggleStates();
            }
            return true;
        }
        case ID_MENU_CAPTURE_OPEN_FOLDER:
            OpenCaptureLogFolder(hwnd);
            return true;
        case ID_MENU_CAPTURE_CLOSE_ALL:
            CloseAllCaptureTailWindows();
            if (g_app && g_app->hwndMain) CreateMainMenu(g_app->hwndMain);
            return true;
        case ID_MENU_CAPTURE_TAIL_ALL:
            ShowCaptureTailWindow(hwnd, 0);
            return true;
        case ID_MENU_CAPTURE_TAIL_CHAT:
            ShowCaptureTailWindow(hwnd, 1);
            return true;
        case ID_MENU_CAPTURE_TAIL_AUCTION:
            ShowCaptureTailWindow(hwnd, 2);
            return true;
        case ID_MENU_CAPTURE_TAIL_ITEM:
            ShowCaptureTailWindow(hwnd, 3);
            return true;
        case ID_MENU_CAPTURE_TAIL_CUSTOM:
            ShowCaptureTailWindow(hwnd, 4);
            return true;
        case ID_MENU_CAPTURE_TAIL_TALK:
            ShowCaptureTailWindow(hwnd, 5);
            return true;
        case ID_MENU_CAPTURE_TAIL_EXP:
            ShowCaptureTailWindow(hwnd, 6);
            return true;
        case ID_MENU_CAPTURE_TAIL_USER1:
            ShowCaptureTailWindow(hwnd, 7);
            return true;
        case ID_MENU_CAPTURE_TAIL_USER2:
            ShowCaptureTailWindow(hwnd, 8);
            return true;
        case ID_MENU_CAPTURE_TAIL_USER3:
            ShowCaptureTailWindow(hwnd, 9);
            return true;
        case ID_MENU_CAPTURE_FILTER_SETTINGS:
            PromptTailFilterSettings(hwnd);
            return true;

        case ID_MENU_SETTINGS:
        {
            ShowSettingsDialog(hwnd);
            ApplyStyles();
            return true;
        }
        case ID_MENU_STYLE_LOG_FONT:
            if (ChooseFontOnly(hwnd, g_app->logStyle.font)) { ApplyStyles(); QueueSaveWindowSettings(hwnd); }
            return true;
        case ID_MENU_VIEW_SMOOTH_FONT:
        {
            g_app->smoothFontEnabled = !g_app->smoothFontEnabled;
            SaveFontRenderSettings();
            ApplyStyles();
            RefocusActiveInput();
            return true;
        }
        case ID_MENU_STYLE_LOG_COLOR:
            if (ChooseColorOnly(hwnd, g_app->logStyle.textColor)) { ApplyStyles(); QueueSaveWindowSettings(hwnd); }
            return true;
        case ID_MENU_STYLE_INPUT_FONT:
            if (ChooseFontOnly(hwnd, g_app->inputStyle.font)) { ApplyStyles(); QueueSaveWindowSettings(hwnd); }
            return true;
        case ID_MENU_STYLE_INPUT_COLOR:
            if (ChooseColorOnly(hwnd, g_app->inputStyle.textColor)) { ApplyStyles(); QueueSaveWindowSettings(hwnd); }
            return true;
        case ID_MENU_BG_MAIN:
            if (ChooseBackgroundColor(hwnd, g_app->logStyle.backColor)) { ApplyStyles(); QueueSaveWindowSettings(hwnd); }
            return true;
        case ID_MENU_BG_INPUT:
            if (ChooseBackgroundColor(hwnd, g_app->inputStyle.backColor)) {
                ApplyStyles();
                if (g_app->hwndInput) { InvalidateRect(g_app->hwndInput, nullptr, FALSE); }
                for (int i = 0; i < INPUT_ROWS; ++i) {
                    if (g_app->hwndEdit[i]) { InvalidateRect(g_app->hwndEdit[i], nullptr, FALSE); }
                }
                QueueSaveWindowSettings(hwnd);
            }
            return true;
        case ID_MENU_EDIT_MEMO:
            OpenMemoWindow(hwnd);
            return true;
        case ID_MENU_VIEW_HIDE_MENU:
        {
            if (g_app)
            {
                g_app->menuHidden = !g_app->menuHidden;
                LayoutChildren(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return true;
        }
        case ID_MENU_VIEW_SYMBOLS:
        {
            g_app->hwndTargetEdit = GetFocus();
            ShowSymbolDialog(hwnd);

            if (g_app->hwndSymbol && IsWindow(g_app->hwndSymbol)) {
                SetWindowPos(g_app->hwndSymbol, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                BringWindowToTop(g_app->hwndSymbol); // 추가
                SetForegroundWindow(g_app->hwndSymbol);
            }
            return true;
        }
        case ID_MENU_HELP_SHORTCUT:
            ShowShortcutHelp(hwnd);
            return true;
        case ID_MENU_EDIT_ABBREVIATION:
        {
            PromptAbbreviationDialog(hwnd);
            return true;
        }
        case ID_MENU_EDIT_VARIABLE:
        {
            PromptVariableDialog(hwnd);
            RefocusActiveInput();
            return true;
        }
        case ID_MENU_EDIT_TIMER:
        {
            PromptTimerDialog(hwnd);
            RefocusActiveInput();
            return true;
        }
        case ID_MENU_EDIT_NUMPAD:
        {
            PromptNumpadDialog(hwnd);
            RefocusActiveInput();
            return true;
        }
        case ID_EDIT_STATUSBAR: // 추가된 부분
        {
            PromptStatusBarDialog(hwnd);
            return true;
        }

        case ID_MENU_HELP_ABOUT:
        {
            PromptAboutDialog(hwnd);
            RefocusActiveInput();
            return true;
        }
        case ID_LOG_SHOW_MENU:
        {
            if (g_app)
            {
                HWND target = g_app->hwndMain ? g_app->hwndMain : hwnd;
                g_app->menuHidden = false;
                LayoutChildren(target);
                InvalidateRect(target, nullptr, FALSE);
            }
            return true;
        }

        case ID_LOG_COPY:
            if (g_app && g_app->hwndLog) { SendMessageW(g_app->hwndLog, WM_COMMAND, MAKEWPARAM(ID_LOG_COPY, 0), 0); }
            RefocusActiveInput();
            if (g_app) {
                int idx = g_app->activeEditIndex;
                if (idx >= 0 && idx < INPUT_ROWS && g_app->hwndEdit[idx])
                    ShowCaret(g_app->hwndEdit[idx]);
            }
            return true;
        case ID_MENU_FILE_NEW_WINDOW:
            LaunchNewKtinWindow(hwnd);
            return true;

        case ID_MENU_FILE_QUICK_CONNECT:
            ShowQuickConnectDialog(hwnd);
            return true;
        case ID_MENU_FILE_READ_SCRIPT:
        {
            std::wstring path;
            if (ChooseScriptFile(hwnd, path)) {
                std::wstring cmd = L"#read {" + path + L"}";
                SendRawCommandToMud(cmd);
            }
            RefocusActiveInput();
            return true;
        }
        case ID_MENU_FILE_ZAP:
            // buildfix38: 주소록 세션뿐 아니라 빠른연결의 new 세션도 종료합니다.
            ZapKnownTinTinSession();
            g_app->hasPendingQuickConnect = false;
            KillWinTimer(hwnd, ID_TIMER_SWITCH_QUICK_CONNECT);
            g_app->isConnected = false;

            RefocusActiveInput();
            return true;
        case ID_MENU_FILE_ADDRESSBOOK:
        {
            PromptAddressBook(hwnd);
            RefocusActiveInput();
            return true;
        }
        case ID_MENU_OPTIONS_KEEPALIVE:
        {
            bool enabled = g_app->keepAliveEnabled;
            int intervalSec = g_app->keepAliveIntervalSec;
            std::wstring command = g_app->keepAliveCommand;
            if (PromptKeepAliveSettings(hwnd, enabled, intervalSec, command)) {
                g_app->keepAliveEnabled = enabled; g_app->keepAliveIntervalSec = intervalSec; g_app->keepAliveCommand = command;
                SaveKeepAliveSettings(); ApplyKeepAliveTimer(hwnd);
            }
            RefocusActiveInput();
            return true;
        }
        case ID_MENU_OPTIONS_SHORTCUTBAR:
        {
            g_app->shortcutBarVisible = !g_app->shortcutBarVisible;
            ApplyShortcutButtons(hwnd); LayoutChildren(hwnd);
            if (g_app->hwndInput) { InvalidateRect(g_app->hwndInput, nullptr, FALSE); }
            RefocusActiveInput();
            return true;
        }
        case ID_MENU_OPTIONS_SAVE_INPUT_ON_EXIT:
        {
            g_app->saveInputOnExit = !g_app->saveInputOnExit;
            SaveInputHistorySettings();
            RefocusActiveInput();
            return true;
        }

        case ID_MENU_OPTIONS_KEEPALIVE_TOGGLE:
        {
            if (!g_app) break;
            g_app->keepAliveEnabled = !g_app->keepAliveEnabled; // 상태 반전
            SaveKeepAliveSettings(); // 즉시 파일에 저장
            ApplyKeepAliveTimer(hwnd); // 타이머 끄거나 켜기 반영
            UpdateMenuToggleStates(); // 메뉴 글씨 갱신

            // 포커스를 입력창으로 돌려줌
            RefocusActiveInput();
            return true;
        }

        case ID_MENU_OPTIONS_SCREEN_SIZE:
        {
            int cols = g_app->screenCols; int rows = g_app->screenRows;
            if (PromptScreenSizeSettings(hwnd, cols, rows)) {
                g_app->screenCols = cols; g_app->screenRows = rows; SaveScreenSizeSettings();
            }
            RefocusActiveInput();
            return true;
        }
        case ID_MENU_OPTIONS_FIT_WINDOW:
        {
            FitWindowToScreenGrid(hwnd, g_app->screenCols, g_app->screenRows, false);
            SaveWindowSettings(hwnd);
            RefocusActiveInput();
            return true;
        }
        case ID_MENU_OPTIONS_SHORTCUT_EDIT:
        {
            PromptShortcutEditor(hwnd);
            RefocusActiveInput();
            return true;
        }
        case ID_MENU_THEME_DIALOG:
        {
            int theme = g_app->ansiTheme;
            if (ShowThemeDialog(hwnd, &theme)) {
                g_app->ansiTheme = theme; ApplyThemeVisualsToApp(theme);
            }
            return true;
        }
        case ID_MENU_FIND_DIALOG:
            ShowFindDialog(hwnd);
            return true;
        case ID_MENU_EDIT_HIGHLIGHT:
            MessageBoxW(hwnd, L"장시간 실행 안정성을 위해 트리거/실시간 하이라이트 기능은 제거했습니다.", L"안내", MB_OK | MB_ICONINFORMATION);
            return true;

        case ID_MENU_EDIT_FUNCTION_SHORTCUT:
        {
            ShowShortcutDialog(hwnd);
            RefocusActiveInput();
            return true;
        }

        // (★ 아래처럼 수정: "끝내기" 메뉴를 누르면 트레이 옵션을 무시하고 완전히 강제 종료시킴)
        case ID_MENU_EXIT:
            // 변수는 건드리지 않고, 창을 즉시 파괴합니다. 
            // 이렇게 하면 WM_CLOSE를 거치지 않고 바로 WM_DESTROY로 가서 안전하게 저장됩니다.
            DestroyWindow(hwnd);
            return true;

        default:
            if (LOWORD(wParam) >= ID_SHORTCUT_BUTTON_BASE && LOWORD(wParam) < ID_SHORTCUT_BUTTON_BASE + SHORTCUT_BUTTON_COUNT) {
                int idx = LOWORD(wParam) - ID_SHORTCUT_BUTTON_BASE;
                if (idx >= 0 && idx < SHORTCUT_BUTTON_COUNT) { ExecuteShortcutButton(idx); return true; }
            }
            break;
        }

    return false;
}

// ==============================================
// merged from main_window.cpp
// ==============================================
#include "main.h"

#include "constants.h"

#include "input.h"
#include "resource.h"
#include "shortcut_bar.h"
#include "status_bar.h"
#include "utils.h"

#include <string>

const wchar_t kMainWindowClass[] = L"TTGuiMainWindow";
const wchar_t* kTerminalWindowClass = L"TTGuiTerminalClass";
const wchar_t kInputWindowClass[] = L"TTGuiInputWindow";
const wchar_t* kInputContainerClass = L"TintinInputContainer";
const wchar_t* kShortcutBarClass = L"TintinShortcutBar";
const wchar_t* kStatusBarClass = L"TintinStatusBar";

namespace
{
    bool RegisterWindowClass(const WNDCLASSW& wc)
    {
        if (RegisterClassW(&wc))
            return true;

        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    WNDCLASSW MakeWindowClass(HINSTANCE hInstance,
                              LPCWSTR className,
                              WNDPROC proc,
                              LPCWSTR cursorName)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = proc;
        wc.hInstance = hInstance;
        wc.lpszClassName = className;
        wc.hCursor = LoadCursorW(nullptr, cursorName);
        wc.hbrBackground = nullptr;
        return wc;
    }
}

bool RegisterMainWindowClasses(HINSTANCE hInstance)
{
    WNDCLASSW wc = MakeWindowClass(hInstance, kMainWindowClass, MainWndProc, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON1));
    if (!RegisterWindowClass(wc)) return false;

    if (!RegisterWindowClass(MakeWindowClass(hInstance, kTerminalWindowClass, TerminalWndProc, IDC_ARROW))) return false;
    if (!RegisterWindowClass(MakeWindowClass(hInstance, kInputWindowClass, DefWindowProcW, IDC_IBEAM))) return false;
    if (!RegisterWindowClass(MakeWindowClass(hInstance, kInputContainerClass, InputContainerProc, IDC_IBEAM))) return false;
    if (!RegisterWindowClass(MakeWindowClass(hInstance, kShortcutBarClass, ShortcutBarProc, IDC_ARROW))) return false;
    if (!RegisterWindowClass(MakeWindowClass(hInstance, kStatusBarClass, StatusBarProc, IDC_ARROW))) return false;

    return true;
}

HWND CreateMainAppWindow(HINSTANCE hInstance)
{
    const std::wstring mainTitle = L"TinTin++ GUI v" + GetAppVersionString();

    return CreateWindowExW(
        0, kMainWindowClass, mainTitle.c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
        538, 61, 690, 899,
        nullptr, nullptr, hInstance, nullptr);
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
        if (HandleGlobalMenuShortcut(hwnd, msg, wParam))
            return 0;
        break;

    case WM_INITMENUPOPUP:
        if (HandleMainInitMenuPopup(reinterpret_cast<HMENU>(wParam)))
            return 0;
        break;

    case WM_CREATE:
        return HandleMainCreate(hwnd);

    case WM_COMMAND:
        if (HandleMainCommand(hwnd, msg, wParam, lParam))
            return 0;
        break;

    case WM_TIMER:
        if (HandleMainTimer(hwnd, wParam))
            return 0;
        break;

    case WM_LBUTTONDOWN:
        if (HandleMainMenuMouseDown(hwnd, lParam))
            return 0;
        break;

    case WM_MOUSEMOVE:
        if (HandleMainMenuMouseMove(hwnd, lParam))
            return 0;
        break;

    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        if (HandleMainHiddenMenuContext(hwnd, msg, lParam))
            return 0;
        break;

    case WM_PAINT:
        if (HandleMainPaint(hwnd))
            return 0;
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
        if (HandleMainSize(hwnd))
            return 0;
        break;

    case WM_MOVE:
        if (HandleMainMove(hwnd))
            return 0;
        break;

    case WM_EXITSIZEMOVE:
        if (HandleMainExitSizeMove(hwnd))
            return 0;
        break;

    case WM_APP_LOG_CHUNK:
        if (HandleMainLogChunk(hwnd, lParam))
            return 0;
        break;

    case WM_APP_HISTORY_EXPORT_DONE:
        if (HandleMainHistoryExportDone(hwnd, lParam))
            return 0;
        break;

    case WM_APP_THEME_RECOLOR_DONE:
        if (g_app && g_app->hwndLog && IsWindow(g_app->hwndLog))
            InvalidateTerminalDirtyRows(g_app->hwndLog);
        return 0;

    case WM_APP_VAR_UPDATE:
        HandleMainVarUpdate(hwnd, wParam, lParam);
        return 0;

    case WM_APP_PROCESS_EXIT:
        if (HandleMainProcessExit(hwnd))
            return 0;
        break;

    case WM_APP_START_BACKEND:
        if (HandleMainStartBackend(hwnd))
            return 0;
        break;

    case WM_APP_TRAYICON:
        if (HandleMainTrayIcon(hwnd, lParam))
            return 0;
        break;

    case WM_SETFOCUS:
        if (HandleMainFocus(hwnd))
            return 0;
        break;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU && static_cast<char>(lParam) == ' ')
            return 0;
        break;

    case WM_CLOSE:
        if (HandleMainClose(hwnd))
            return 0;
        break;

    case WM_DESTROY:
        if (HandleMainDestroy(hwnd))
            return 0;
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
