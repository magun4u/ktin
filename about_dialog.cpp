#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "settings.h"
#include "win_util.h"
#include <shellapi.h>      // ShellExecuteW
#include <commctrl.h>      // SysLink 컨트롤

// 내부 프로시저 (static 유지)
LRESULT CALLBACK AboutPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool PromptAboutDialog(HWND hwnd)
{
    const wchar_t kDlgClass[] = L"TTGuiAboutPopupClass";
    static bool s_registered = false;
    if (!s_registered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = AboutPopupProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kDlgClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassW(&wc);
        s_registered = true;
    }

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kDlgClass,
        L"정보",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 460, 340,
        hwnd,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (!hDlg)
        return false;

    HFONT hFontBase = GetPopupUIFont(hDlg);

    // 제목용 큰 볼드 폰트
    LOGFONTW lfTitle = {};
    GetObjectW(hFontBase, sizeof(lfTitle), &lfTitle);
    lfTitle.lfHeight = -24;
    lfTitle.lfWeight = FW_BOLD;
    HFONT hFontTitle = CreateFontIndirectW(&lfTitle);

    // 서브 타이틀용 폰트
    LOGFONTW lfSub = {};
    GetObjectW(hFontBase, sizeof(lfSub), &lfSub);
    lfSub.lfHeight = -15;
    lfSub.lfWeight = FW_NORMAL;
    HFONT hFontSub = CreateFontIndirectW(&lfSub);

    HWND hTitle = CreateWindowExW(
        0, L"STATIC", L"Ktin: TinTin++ GUI Client",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        40, 26, 360, 32,
        hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);

    // 1. 방금 만든 함수로 동적 버전 문자열 생성
    std::wstring versionText = L"버전 " + GetAppVersionString();

    // 2. 조립된 문자열(versionText.c_str())을 텍스트 자리에 쏙!
    HWND hSub = CreateWindowExW(
        0, L"STATIC", versionText.c_str(),
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        40, 60, 360, 22,
        hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);

    CreateWindowExW(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
        30, 94, 390, 10,
        hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);

    CreateWindowExW(
        0, L"STATIC", L"　　제작",
        WS_CHILD | WS_VISIBLE,
        40, 114, 70, 22,
        hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);

    CreateWindowExW(
        0, L"STATIC", L"울보천사",
        WS_CHILD | WS_VISIBLE,
        120, 114, 280, 22,
        hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);

    CreateWindowExW(
        0, L"STATIC", L"　　문의",
        WS_CHILD | WS_VISIBLE,
        40, 142, 70, 22,
        hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);

    CreateWindowExW(
        0, L"STATIC", L"cry1004@gmail.com",
        WS_CHILD | WS_VISIBLE,
        120, 142, 280, 22,
        hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);

    CreateWindowExW(
        0, L"STATIC", L"　블로그",
        WS_CHILD | WS_VISIBLE,
        40, 176, 70, 22,
        hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);

    HWND hBlogLink = CreateWindowExW(
        0, L"SysLink",
        L"<a href=\"https://blog.naver.com/mirckorea\">https://blog.naver.com/mirckorea</a>",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        120, 173, 300, 24,
        hDlg, (HMENU)1001, GetModuleHandleW(nullptr), nullptr);

    if (!hBlogLink)
    {
        CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"https://blog.naver.com/mirckorea",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY,
            120, 173, 300, 24,
            hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);
    }

    CreateWindowExW(
        0, L"STATIC", L"홈페이지",
        WS_CHILD | WS_VISIBLE,
        40, 206, 70, 22,
        hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);

    HWND hHomeLink = CreateWindowExW(
        0, L"SysLink",
        L"<a href=\"https://luminari.vineyard.haus/forums\">https://luminari.vineyard.haus/forums</a>",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        120, 203, 300, 24,
        hDlg, (HMENU)1002, GetModuleHandleW(nullptr), nullptr);

    if (!hHomeLink)
    {
        CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"https://luminari.vineyard.haus/forums",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY,
            120, 203, 300, 24,
            hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);
    }

    CreateWindowExW(
        0, L"BUTTON", L"확인(&O)",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON | BS_OWNERDRAW,
        182, 238, 96, 32,
        hDlg, (HMENU)(INT_PTR)IDOK, GetModuleHandleW(nullptr), nullptr);

    // 폰트 적용
    EnumChildWindows(
        hDlg,
        [](HWND child, LPARAM lParam) -> BOOL
        {
            SendMessageW(child, WM_SETFONT, (WPARAM)lParam, TRUE);
            return TRUE;
        },
        (LPARAM)hFontBase);

    SendMessageW(hTitle, WM_SETFONT, (WPARAM)hFontTitle, TRUE);
    SendMessageW(hSub, WM_SETFONT, (WPARAM)hFontSub, TRUE);

    // 부모 창 중앙에 표시
    RECT rcOwner{}, rcDlg{};
    GetWindowRect(hwnd, &rcOwner);
    GetWindowRect(hDlg, &rcDlg);
    int dlgW = rcDlg.right - rcDlg.left;
    int dlgH = rcDlg.bottom - rcDlg.top;
    int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - dlgW) / 2;
    int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - dlgH) / 2;
    SetWindowPos(hDlg, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);

    EnableWindow(hwnd, FALSE);
    SetFocus(GetDlgItem(hDlg, IDOK));

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

    ResetGdiObjectRef(hFontTitle);
    ResetGdiObjectRef(hFontSub);

    return true;
}

LRESULT CALLBACK AboutPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HBRUSH hbrBack = nullptr;
    static HBRUSH hbrPanel = nullptr;

    switch (msg)
    {
    case WM_CREATE:
    {
        ApplyPopupTitleBarTheme(hwnd);
        hbrBack = CreateSolidBrush(RGB(32, 34, 37));
        hbrPanel = CreateSolidBrush(RGB(43, 45, 49));
        return 0;
    }

    // ★★★ ALT + O 단축키 처리 추가 ★★★
    case WM_SYSCHAR:
    {
        wchar_t ch = towlower((wchar_t)wParam);
        if (ch == 'o')
        {
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
            return 0;
        }
        break;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(230, 230, 230));
        return (INT_PTR)hbrPanel;
    }
    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, OPAQUE);
        SetTextColor(hdc, RGB(230, 230, 230));
        SetBkColor(hdc, RGB(24, 26, 27));
        return (INT_PTR)hbrBack;
    }
    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, hbrBack);
        RECT panel = { 14, 14, rc.right - 14, rc.bottom - 14 };
        FillRect(hdc, &panel, hbrPanel);
        return 1;
    }
    case WM_NOTIFY:
    {
        NMHDR* hdr = (NMHDR*)lParam;
        if (hdr && (hdr->code == NM_CLICK || hdr->code == NM_RETURN))
        {
            NMLINK* link = (NMLINK*)lParam;
            ShellExecuteW(nullptr, L"open", link->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        break;
    }
    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (!dis || dis->CtlID != IDOK)
            return FALSE;

        HDC hdc = dis->hDC;
        RECT rc = dis->rcItem;
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        bool focused = (dis->itemState & ODS_FOCUS) != 0;

        COLORREF bg = pressed ? RGB(70, 74, 80) : RGB(58, 62, 68);
        COLORREF border = RGB(95, 100, 108);
        COLORREF text = RGB(235, 235, 235);

        UniqueGdiObject hbr(CreateSolidBrush(bg));
        if (hbr.IsValid())
            FillRect(hdc, &rc, (HBRUSH)hbr.Get());

        UniqueGdiObject hPen(CreatePen(PS_SOLID, 1, border));
        ScopedSelectObject penSel(hdc, hPen.Get());
        ScopedSelectObject brushSel(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, text);

        wchar_t textBuf[64] = {};
        GetWindowTextW(dis->hwndItem, textBuf, 64);
        DrawTextW(hdc, textBuf, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        if (focused)
        {
            RECT focus = rc;
            InflateRect(&focus, -4, -4);
            DrawFocusRect(hdc, &focus);
        }
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        ResetGdiObjectRef(hbrBack);
        ResetGdiObjectRef(hbrPanel);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
