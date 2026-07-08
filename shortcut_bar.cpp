#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "shortcut_bar.h"
#include "resource.h"
#include "settings.h"
#include "input.h"
#include "win_util.h"
#include <commctrl.h>

// 외부에서 정의된 함수들 (extern 선언)
extern void SendTextToMud(const wchar_t* text);
extern void SendRawCommandToMud(const std::wstring& cmd);
extern HFONT GetPopupUIFont(HWND hwnd);
extern std::wstring Trim(const std::wstring& str);
extern void SaveFunctionKeySettings();   // functionkey.cpp에 있음

// 상수 (main.h에 정의되어 있지 않다면 여기서 정의)
#ifndef SHORTCUT_BUTTON_COUNT
#define SHORTCUT_BUTTON_COUNT 10
#endif

#ifndef SHORTCUT_BAR_HEIGHT
#define SHORTCUT_BAR_HEIGHT 32
#endif

// 전역 단축키 배열
ShortcutKeyBinding g_shortcuts[48];

// ==============================================
// 내부 헬퍼 함수들 (static)
// ==============================================

ShortcutKeyBinding* FindShortcut(int vk, int mod)
{
    for (int i = 0; i < 48; ++i)
    {
        if (g_shortcuts[i].vk == vk && g_shortcuts[i].mod == mod)
            return &g_shortcuts[i];
    }
    return nullptr;
}

int GetShortcutModState()
{
    int mod = SCMOD_NONE;

    if (GetKeyState(VK_CONTROL) & 0x8000)
        mod |= SCMOD_CTRL;
    if (GetKeyState(VK_SHIFT) & 0x8000)
        mod |= SCMOD_SHIFT;
    if (GetKeyState(VK_MENU) & 0x8000)
        mod |= SCMOD_ALT;

    return mod;
}

std::wstring GetShortcutName(int vk, int mod)
{
    wchar_t key[32];
    wsprintfW(key, L"F%d", (vk - VK_F1) + 1);

    std::wstring s;
    if (mod & SCMOD_CTRL)  s += L"Ctrl+";
    if (mod & SCMOD_SHIFT) s += L"Shift+";
    if (mod & SCMOD_ALT)   s += L"Alt+";

    s += key;
    return s;
}

std::wstring GetShortcutListText(const ShortcutKeyBinding& sc)
{
    std::wstring s = GetShortcutName(sc.vk, sc.mod);

    if (sc.reserved)
        s += L" (예약됨)";
    else if (sc.enabled && sc.command[0])
        s += L" [사용중]";

    return s;
}

int ShortcutTabToMod(int tabIndex)
{
    switch (tabIndex)
    {
    default:
    case 0: return SCMOD_NONE;
    case 1: return SCMOD_ALT;
    case 2: return SCMOD_SHIFT;
    case 3: return SCMOD_CTRL;
    }
}

void ReloadShortcutListByTab(HWND hList, int tabIndex)
{
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);

    int wantMod = ShortcutTabToMod(tabIndex);

    for (int i = 0; i < 48; ++i)
    {
        if (g_shortcuts[i].mod != wantMod)
            continue;

        std::wstring s = GetShortcutListText(g_shortcuts[i]);
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)s.c_str());
    }

    SendMessageW(hList, LB_SETCURSEL, 0, 0);
}

ShortcutKeyBinding* GetShortcutFromTabAndListIndex(int tabIndex, int listIndex)
{
    if (listIndex < 0 || listIndex >= 12)
        return nullptr;

    int mod = ShortcutTabToMod(tabIndex);
    int vk = VK_F1 + listIndex;

    return FindShortcut(vk, mod);
}

// ==============================================
// 초기화 및 설정 로드/저장
// ==============================================
void InitShortcutBindings()
{
    int idx = 0;

    for (int group = 0; group < 4; ++group)
    {
        int mod = 0;
        if (group == 1) mod = SCMOD_ALT;
        else if (group == 2) mod = SCMOD_SHIFT;
        else if (group == 3) mod = SCMOD_CTRL;

        for (int f = 1; f <= 12; ++f)
        {
            g_shortcuts[idx].vk = VK_F1 + (f - 1);
            g_shortcuts[idx].mod = mod;
            g_shortcuts[idx].reserved = (mod == SCMOD_NONE && f == 4);
            g_shortcuts[idx].enabled = false;
            g_shortcuts[idx].command[0] = 0;
            ++idx;
        }
    }
}


void LoadShortcutSettings()
{
    if (!g_app) return;

    std::wstring path = GetSettingsPath();
    wchar_t buf[1024];

    for (int i = 0; i < SHORTCUT_BUTTON_COUNT; ++i)
    {
        wchar_t keyLabel[32], keyCmdOn[32], keyCmdOff[32], keyIsToggle[32];
        wsprintfW(keyLabel, L"label_%d", i);
        wsprintfW(keyCmdOn, L"cmd_%d", i);
        wsprintfW(keyCmdOff, L"cmdoff_%d", i);
        wsprintfW(keyIsToggle, L"istoggle_%d", i);

        GetPrivateProfileStringW(L"shortcut", keyLabel, L"", buf, 1024, path.c_str());
        g_app->shortcutLabels[i] = buf;

        GetPrivateProfileStringW(L"shortcut", keyCmdOn, L"", buf, 1024, path.c_str());
        g_app->shortcutCommands[i] = buf;

        GetPrivateProfileStringW(L"shortcut", keyCmdOff, L"", buf, 1024, path.c_str());
        g_app->shortcutOffCommands[i] = buf;

        g_app->shortcutIsToggle[i] =
            (GetPrivateProfileIntW(L"shortcut", keyIsToggle, 0, path.c_str()) == 1);

        g_app->shortcutActive[i] = false;
    }

    g_app->shortcutBarVisible =
        (GetPrivateProfileIntW(L"shortcut", L"visible", 1, path.c_str()) != 0);
}


void SaveShortcutSettings()
{
    if (!g_app) return;
    std::wstring path = GetSettingsPath();

    for (int i = 0; i < SHORTCUT_BUTTON_COUNT; ++i)
    {
        wchar_t keyLabel[32], keyCmdOn[32], keyCmdOff[32], keyIsToggle[32];
        wsprintfW(keyLabel, L"label_%d", i);
        wsprintfW(keyCmdOn, L"cmd_%d", i);
        wsprintfW(keyCmdOff, L"cmdoff_%d", i); // ★ 추가
        wsprintfW(keyIsToggle, L"istoggle_%d", i); // ★ 추가

        WritePrivateProfileStringW(L"shortcut", keyLabel, g_app->shortcutLabels[i].c_str(), path.c_str());
        WritePrivateProfileStringW(L"shortcut", keyCmdOn, g_app->shortcutCommands[i].c_str(), path.c_str());
        WritePrivateProfileStringW(L"shortcut", keyCmdOff, g_app->shortcutOffCommands[i].c_str(), path.c_str());
        WritePrivateProfileStringW(L"shortcut", keyIsToggle, g_app->shortcutIsToggle[i] ? L"1" : L"0", path.c_str());
    }
    WritePrivateProfileStringW(L"shortcut", L"visible", g_app->shortcutBarVisible ? L"1" : L"0", path.c_str());
}

// ==============================================
// 단축키 편집 팝업
// ==============================================
LRESULT CALLBACK ShortcutEditPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ShortcutEditState* state = (ShortcutEditState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            if (state && state->label && state->command)
            {
                wchar_t labelBuf[256] = {};
                wchar_t cmdBuf[1024] = {};

                GetWindowTextW(GetDlgItem(hwnd, ID_SHORTCUT_EDIT_LABEL), labelBuf, 256);
                GetWindowTextW(GetDlgItem(hwnd, ID_SHORTCUT_EDIT_COMMAND), cmdBuf, 1024);

                std::wstring newLabel = Trim(labelBuf);
                std::wstring newCommand = Trim(cmdBuf);

                if (newLabel.empty())
                {
                    wchar_t fallback[16];
                    wsprintfW(fallback, L"%d", state->index + 1);
                    newLabel = fallback;
                }

                *state->label = newLabel;
                *state->command = newCommand;
                state->accepted = true;
            }

            DestroyWindow(hwnd);
            return 0;

        case IDCANCEL:
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}


bool PromptShortcutEdit(HWND hwnd, int index, std::wstring& label, std::wstring& command)
{
    const wchar_t kDlgClass[] = L"TTGuiShortcutEditPopupClass";
    static bool s_registered = false;

    if (!s_registered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = ShortcutEditPopupProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kDlgClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        s_registered = true;
    }

    ShortcutEditState state;
    state.index = index;
    state.label = &label;
    state.command = &command;
    state.accepted = false;

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kDlgClass,
        L"단축버튼 편집",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 210,
        hwnd,
        nullptr,
        GetModuleHandleW(nullptr),
        &state);

    if (!hDlg)
        return false;

    CreateWindowExW(
        0, L"STATIC", L"버튼 이름:",
        WS_CHILD | WS_VISIBLE,
        16, 20, 80, 20,
        hDlg, (HMENU)(INT_PTR)ID_SHORTCUT_EDIT_LABEL_TEXT, GetModuleHandleW(nullptr), nullptr);

    HWND hEditLabel = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        100, 16, 280, 24,
        hDlg, (HMENU)(INT_PTR)ID_SHORTCUT_EDIT_LABEL, GetModuleHandleW(nullptr), nullptr);

    CreateWindowExW(
        0, L"STATIC", L"전송 명령:",
        WS_CHILD | WS_VISIBLE,
        16, 60, 80, 20,
        hDlg, (HMENU)(INT_PTR)ID_SHORTCUT_EDIT_COMMAND_TEXT, GetModuleHandleW(nullptr), nullptr);

    HWND hEditCommand = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        100, 56, 280, 24,
        hDlg, (HMENU)(INT_PTR)ID_SHORTCUT_EDIT_COMMAND, GetModuleHandleW(nullptr), nullptr);

    CreateWindowExW(
        0, L"BUTTON", L"확인",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        212, 110, 80, 28,
        hDlg, (HMENU)(INT_PTR)IDOK, GetModuleHandleW(nullptr), nullptr);

    CreateWindowExW(
        0, L"BUTTON", L"취소",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        300, 110, 80, 28,
        hDlg, (HMENU)(INT_PTR)IDCANCEL, GetModuleHandleW(nullptr), nullptr);

    SetWindowTextW(hEditLabel, label.c_str());
    SetWindowTextW(hEditCommand, command.c_str());

    HFONT hFont = GetPopupUIFont(hDlg);
    SendMessageW(hDlg, WM_SETFONT, (WPARAM)hFont, TRUE);

    EnumChildWindows(
        hDlg,
        [](HWND child, LPARAM lParam) -> BOOL
        {
            SendMessageW(child, WM_SETFONT, lParam, TRUE);
            return TRUE;
        },
        (LPARAM)hFont);

    RECT rcOwner{}, rcDlg{};
    GetWindowRect(hwnd, &rcOwner);
    GetWindowRect(hDlg, &rcDlg);

    int dlgW = rcDlg.right - rcDlg.left;
    int dlgH = rcDlg.bottom - rcDlg.top;
    int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - dlgW) / 2;
    int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - dlgH) / 2;

    SetWindowPos(hDlg, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);

    EnableWindow(hwnd, FALSE);
    SetFocus(hEditLabel);

    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, nullptr, 0, 0))
    {
        if (!IsDialogMessageW(hDlg, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(hwnd, TRUE);
    SetActiveWindow(hwnd);
    SetForegroundWindow(hwnd);

    return state.accepted;
}

// ==============================================
// 단축키 설정 대화상자
// ==============================================
LRESULT CALLBACK ShortcutDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HWND hTab = nullptr;
    static HWND hList = nullptr;
    static HWND hEdit = nullptr;
    static HWND hCheck = nullptr;
    static HWND hStaticTitle = nullptr;
    static HWND hBtnSave = nullptr;
    static int s_tabIndex = 0;

    switch (msg)
    {
    case WM_CREATE:
    {
        HFONT hFont = GetPopupUIFont(hwnd);

        // 탭 컨트롤
        hTab = CreateWindowExW(
            0, WC_TABCONTROLW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            12, 12, 406, 250,
            hwnd, (HMENU)IDC_SC_TAB, GetModuleHandleW(nullptr), nullptr);

        TCITEMW tie = {};
        tie.mask = TCIF_TEXT;
        tie.pszText = const_cast<LPWSTR>(L"F");      TabCtrl_InsertItem(hTab, 0, &tie);
        tie.pszText = const_cast<LPWSTR>(L"ALT");    TabCtrl_InsertItem(hTab, 1, &tie);
        tie.pszText = const_cast<LPWSTR>(L"SHIFT");  TabCtrl_InsertItem(hTab, 2, &tie);
        tie.pszText = const_cast<LPWSTR>(L"CTRL");   TabCtrl_InsertItem(hTab, 3, &tie);

        // 리스트박스
        hList = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP,
            24, 48, 150, 200,
            hwnd, (HMENU)IDC_SC_LIST, GetModuleHandleW(nullptr), nullptr);

        // 우측 컨트롤
        hStaticTitle = CreateWindowExW(
            0, L"STATIC", L"명령:",
            WS_CHILD | WS_VISIBLE,
            190, 62, 60, 20,
            hwnd, (HMENU)IDC_SC_LABEL, GetModuleHandleW(nullptr), nullptr);

        hEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            190, 86, 210, 24,
            hwnd, (HMENU)IDC_SC_EDIT, GetModuleHandleW(nullptr), nullptr);

        hCheck = CreateWindowExW(
            0, L"BUTTON", L"사용",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            190, 120, 90, 24,
            hwnd, (HMENU)IDC_SC_ENABLE, GetModuleHandleW(nullptr), nullptr);

        // ★ 저장 / 닫기 버튼에 단축키 추가
        hBtnSave = CreateWindowExW(
            0, L"BUTTON", L"저장(&S)",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            190, 160, 90, 30,
            hwnd, (HMENU)IDC_SC_SAVE, GetModuleHandleW(nullptr), nullptr);

        CreateWindowExW(
            0, L"BUTTON", L"닫기(&C)",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            310, 160, 90, 30,
            hwnd, (HMENU)IDC_SC_CLOSE, GetModuleHandleW(nullptr), nullptr);

        EnumChildWindows(
            hwnd,
            [](HWND child, LPARAM lParam) -> BOOL
            {
                SendMessageW(child, WM_SETFONT, lParam, TRUE);
                return TRUE;
            },
            (LPARAM)hFont);

        SendMessageW(hTab, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hList, LB_SETITEMHEIGHT, 0, 16);

        s_tabIndex = 0;
        TabCtrl_SetCurSel(hTab, s_tabIndex);
        ReloadShortcutListByTab(hList, s_tabIndex);
        SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SC_LIST, LBN_SELCHANGE), (LPARAM)hList);

        return 0;
    }

    // ★★★ ALT 단축키 처리 추가 ★★★
    case WM_SYSCHAR:
    {
        wchar_t ch = towlower((wchar_t)wParam);
        if (ch == 's')
        {
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SC_SAVE, BN_CLICKED), 0);
            return 0;
        }
        if (ch == 'c')
        {
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SC_CLOSE, BN_CLICKED), 0);
            return 0;
        }
        break;
    }

    case WM_NOTIFY:
    {
        NMHDR* hdr = (NMHDR*)lParam;
        if (hdr && hdr->idFrom == IDC_SC_TAB && hdr->code == TCN_SELCHANGE)
        {
            s_tabIndex = TabCtrl_GetCurSel(hTab);
            if (s_tabIndex < 0) s_tabIndex = 0;
            ReloadShortcutListByTab(hList, s_tabIndex);
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SC_LIST, LBN_SELCHANGE), (LPARAM)hList);
            return 0;
        }
        break;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDC_SC_LIST:
            if (HIWORD(wParam) == LBN_SELCHANGE)
            {
                int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
                ShortcutKeyBinding* sc = GetShortcutFromTabAndListIndex(s_tabIndex, sel);
                if (!sc)
                {
                    SetWindowTextW(hEdit, L"");
                    SendMessageW(hCheck, BM_SETCHECK, BST_UNCHECKED, 0);
                    EnableWindow(hEdit, FALSE);
                    EnableWindow(hCheck, FALSE);
                    EnableWindow(hBtnSave, FALSE);
                    return 0;
                }
                SetWindowTextW(hEdit, sc->command);
                SendMessageW(hCheck, BM_SETCHECK, sc->enabled ? BST_CHECKED : BST_UNCHECKED, 0);
                if (sc->reserved)
                {
                    SetWindowTextW(hStaticTitle, L"예약 키:");
                    SetWindowTextW(hEdit, L"F4 는 특수기호 창으로 예약됨");
                    EnableWindow(hEdit, FALSE);
                    EnableWindow(hCheck, FALSE);
                    EnableWindow(hBtnSave, FALSE);
                }
                else
                {
                    SetWindowTextW(hStaticTitle, L"명령:");
                    EnableWindow(hEdit, TRUE);
                    EnableWindow(hCheck, TRUE);
                    EnableWindow(hBtnSave, TRUE);
                }
                return 0;
            }
            break;

        case IDC_SC_SAVE:
        {
            int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
            ShortcutKeyBinding* sc = GetShortcutFromTabAndListIndex(s_tabIndex, sel);
            if (sc && !sc->reserved)
            {
                GetWindowTextW(hEdit, sc->command, 512);
                sc->enabled = (SendMessageW(hCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
                SaveFunctionKeySettings();
                ReloadShortcutListByTab(hList, s_tabIndex);
                SendMessageW(hList, LB_SETCURSEL, sel, 0);
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SC_LIST, LBN_SELCHANGE), (LPARAM)hList);
            }
            return 0;
        }

        case IDC_SC_CLOSE:
        case IDCANCEL:
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowShortcutDialog(HWND parent)
{
    static const wchar_t* cls = L"TT_SHORTCUT_DLG";
    static bool s_registered = false;

    if (!s_registered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = ShortcutDlgProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = cls;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        s_registered = true;
    }

    const int dlgW = 446;
    const int dlgH = 320;

    RECT rcOwner{};
    if (parent && IsWindow(parent))
        GetWindowRect(parent, &rcOwner);
    else
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcOwner, 0);

    int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - dlgW) / 2;
    int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - dlgH) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        cls,
        L"단축키 설정",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, dlgW, dlgH,
        parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (!hwnd)
        return;

    EnableWindow(parent, FALSE);

    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0))
    {
        if (!IsDialogMessageW(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(parent, TRUE);
    SetActiveWindow(parent);
}

// ==============================================
// 단축 버튼 바 (하단 바)
// ==============================================

void InitializeShortcutButtons()
{
    if (!g_app || !g_app->hwndShortcutBar)
        return;

    HFONT hFont = GetShortcutButtonUIFont(g_app->hwndMain ? g_app->hwndMain : g_app->hwndShortcutBar);

    for (int i = 0; i < SHORTCUT_BUTTON_COUNT; ++i)
    {
        if (g_app->hwndShortcutButtons[i] && IsWindow(g_app->hwndShortcutButtons[i]))
        {
            DestroyWindow(g_app->hwndShortcutButtons[i]);
            g_app->hwndShortcutButtons[i] = nullptr;
        }

        std::wstring label = g_app->shortcutLabels[i];
        if (label.empty())
        {
            wchar_t num[16] = {};
            wsprintfW(num, L"%d", i + 1);
            label = num;
        }

        DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
        if (g_app->shortcutIsToggle[i])
            style |= BS_AUTOCHECKBOX | BS_PUSHLIKE;
        else
            style |= BS_PUSHBUTTON;

        g_app->hwndShortcutButtons[i] = CreateWindowExW(
            0,
            L"BUTTON",
            label.c_str(),
            style,
            0, 0, 80, 24,
            g_app->hwndShortcutBar,   // 핵심
            (HMENU)(INT_PTR)(ID_SHORTCUT_BUTTON_BASE + i),
            GetModuleHandleW(nullptr),
            nullptr);

        if (g_app->hwndShortcutButtons[i])
        {
            SendMessageW(g_app->hwndShortcutButtons[i], WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessageW(
                g_app->hwndShortcutButtons[i],
                BM_SETCHECK,
                g_app->shortcutActive[i] ? BST_CHECKED : BST_UNCHECKED,
                0);
            ShowWindow(g_app->hwndShortcutButtons[i], g_app->shortcutBarVisible ? SW_SHOW : SW_HIDE);
        }
    }

    if (g_app->hwndMain)
    {
        LayoutChildren(g_app->hwndMain);
        InvalidateRect(g_app->hwndShortcutBar, nullptr, FALSE);
    }
}


void ApplyShortcutButtons(HWND hwnd)
{
    if (!g_app)
        return;

    for (int i = 0; i < SHORTCUT_BUTTON_COUNT; ++i)
    {
        HWND hBtn = g_app->hwndShortcutButtons[i];
        if (!hBtn || !IsWindow(hBtn))
            continue;

        // 1) 버튼 라벨 반영
        std::wstring label = g_app->shortcutLabels[i];
        if (label.empty())
            label = std::to_wstring((i + 1) % 10);
        SetWindowTextW(hBtn, label.c_str());

        // 2) 현재 상태를 먼저 완전히 비움
        SendMessageW(hBtn, BM_SETCHECK, BST_UNCHECKED, 0);

        // 3) 스타일 재설정
        LONG_PTR style = GetWindowLongPtrW(hBtn, GWL_STYLE);
        style &= ~(BS_AUTOCHECKBOX | BS_CHECKBOX | BS_PUSHLIKE | BS_PUSHBUTTON);

        if (g_app->shortcutIsToggle[i])
        {
            style |= (WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | BS_PUSHLIKE);
            SetWindowLongPtrW(hBtn, GWL_STYLE, style);

            // 토글 버튼은 현재 active 상태를 다시 반영
            SendMessageW(
                hBtn,
                BM_SETCHECK,
                g_app->shortcutActive[i] ? BST_CHECKED : BST_UNCHECKED,
                0);
        }
        else
        {
            // 일반 버튼으로 바뀌면 내부 active 상태도 강제로 해제
            g_app->shortcutActive[i] = false;

            style |= (WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON);
            SetWindowLongPtrW(hBtn, GWL_STYLE, style);

            // 혹시 남아있는 눌림 상태를 다시 한번 해제
            SendMessageW(hBtn, BM_SETCHECK, BST_UNCHECKED, 0);
        }

        // 4) 스타일 변경 확정 + 즉시 재도장
        SetWindowPos(
            hBtn,
            nullptr,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

        InvalidateRect(hBtn, nullptr, FALSE);

        ShowWindow(hBtn, g_app->shortcutBarVisible ? SW_SHOW : SW_HIDE);
    }

    if (g_app->hwndShortcutBar && IsWindow(g_app->hwndShortcutBar))
    {
        ShowWindow(g_app->hwndShortcutBar, g_app->shortcutBarVisible ? SW_SHOW : SW_HIDE);
        InvalidateRect(g_app->hwndShortcutBar, nullptr, FALSE);
    }

    if (hwnd && IsWindow(hwnd))
    {
        LayoutChildren(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

int GetShortcutBarHeight()
{
    if (!g_app)
        return 0;

    return g_app->shortcutBarVisible ? SHORTCUT_BAR_HEIGHT : 0;
}

// ==============================================
// 단축 버튼 바 창 프로시저
// ==============================================
LRESULT CALLBACK ShortcutBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);

        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));

        UniqueGdiObject hPenDark(CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW)));
        UniqueGdiObject hPenLight(CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DHILIGHT)));
        {
            ScopedSelectObject penSel(hdc, hPenDark.Get());
            MoveToEx(hdc, 0, 0, nullptr);
            LineTo(hdc, rc.right, 0);
        }
        {
            ScopedSelectObject penSel(hdc, hPenLight.Get());
            MoveToEx(hdc, 0, rc.bottom - 1, nullptr);
            LineTo(hdc, rc.right, rc.bottom - 1);
        }
        return 1;
    }

    case WM_PAINT:
    {
        ScopedPaintDC paint(hwnd);
        HDC hdc = paint.Get();
        if (!hdc) return 0;

        RECT rc;
        GetClientRect(hwnd, &rc);

        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));

        UniqueGdiObject hPenDark(CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW)));
        UniqueGdiObject hPenLight(CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DHILIGHT)));
        {
            ScopedSelectObject penSel(hdc, hPenDark.Get());
            MoveToEx(hdc, 0, 0, nullptr);
            LineTo(hdc, rc.right, 0);
        }
        {
            ScopedSelectObject penSel(hdc, hPenLight.Get());
            MoveToEx(hdc, 0, rc.bottom - 1, nullptr);
            LineTo(hdc, rc.right, rc.bottom - 1);
        }

        return 0;
    }

    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: // ★ 토글 버튼은 Static 메시지로 올 때가 많습니다.
    {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        // 버튼 텍스트 색상이 흰색 배경에 묻히지 않게 검정색으로 강제 설정
        SetTextColor(hdc, RGB(0, 0, 0));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_COMMAND:
    {
        HWND hParent = GetParent(hwnd);
        if (hParent)
            return (LRESULT)SendMessageW(hParent, WM_COMMAND, wParam, lParam);
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
