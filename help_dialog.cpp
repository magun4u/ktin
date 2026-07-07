#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "help_dialog.h"
#include "theme.h"
#include "resource.h"
#include "settings.h"
#include <richedit.h>
#include <commctrl.h>

static std::wstring BuildHelpPageText(int page);

// ==============================================
// RichEdit 서식 도우미 함수
// ==============================================
static void HelpSetCharFormatRange(HWND hEdit, LONG cpMin, LONG cpMax, LONG yHeight, COLORREF color, bool bold)
{
    CHARRANGE cr{};
    cr.cpMin = cpMin;
    cr.cpMax = cpMax;
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);

    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_SIZE | CFM_COLOR | CFM_BOLD;
    cf.yHeight = yHeight;
    cf.crTextColor = color;
    cf.dwEffects = bold ? CFE_BOLD : 0;

    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

static LONG HelpFindLineEnd(const std::wstring& text, LONG start)
{
    size_t pos = text.find(L"\r\n", (size_t)start);
    if (pos == std::wstring::npos)
        return (LONG)text.size();
    return (LONG)pos;
}

static void SetHelpRichEditText(HWND hEdit, const std::wstring& text)
{
    HFONT hUiFont = GetPopupUIFont(hEdit);
    SendMessageW(hEdit, WM_SETFONT, (WPARAM)hUiFont, TRUE);

    SetWindowTextW(hEdit, text.c_str());

    // 전체 기본 서식 초기화
    CHARRANGE all{};
    all.cpMin = 0;
    all.cpMax = -1;
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&all);

    CHARFORMAT2W base{};
    base.cbSize = sizeof(base);
    base.dwMask = CFM_SIZE | CFM_COLOR | CFM_BOLD;
    base.yHeight = 220; // 본문
    base.crTextColor = RGB(235, 235, 235);
    base.dwEffects = 0;
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&base);

    // 내부 여백만 적용
    RECT rc{};
    GetClientRect(hEdit, &rc);
    rc.left += 14;
    rc.top += 10;
    rc.right -= 14;
    rc.bottom -= 10;
    SendMessageW(hEdit, EM_SETRECTNP, 0, (LPARAM)&rc);

    // 첫 줄 끝 찾기
    LONG line1Start = 0;
    LONG line1End = HelpFindLineEnd(text, line1Start);

    // 둘째 줄 찾기
    LONG line2Start = line1End;
    if (line2Start < (LONG)text.size() && text.compare((size_t)line2Start, 2, L"\r\n") == 0)
        line2Start += 2;
    LONG line2End = HelpFindLineEnd(text, line2Start);

    // 첫 줄: 큰 제목
    HelpSetCharFormatRange(
        hEdit,
        line1Start,
        line1End,
        320,
        RGB(255, 255, 255),
        true);

    // 둘째 줄이 비어있지 않을 때만 부제 처리
    if (line2Start < line2End)
    {
        HelpSetCharFormatRange(
            hEdit,
            line2Start,
            line2End,
            190,
            RGB(180, 185, 190),
            false);
    }

    // 나머지 본문
    LONG bodyStart = line2End;
    if (bodyStart < (LONG)text.size() && text.compare((size_t)bodyStart, 2, L"\r\n") == 0)
        bodyStart += 2;

    if (bodyStart < (LONG)text.size())
    {
        HelpSetCharFormatRange(
            hEdit,
            bodyStart,
            -1,
            220,
            RGB(235, 235, 235),
            false);
    }

    SendMessageW(hEdit, EM_SETSEL, 0, 0);
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
    InvalidateRect(hEdit, nullptr, TRUE);
}


// ==============================================
// 도움말 창 프로시저
// ==============================================
static LRESULT CALLBACK ShortcutHelpProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HWND hTitle = nullptr;
    static HWND hSub = nullptr;
    static HWND hList = nullptr;
    static HWND hView = nullptr;
    static HWND hClose = nullptr;
    static HFONT hFontTitle = nullptr;
    static HFONT hFontSub = nullptr;
    static HFONT hFontUi = nullptr;
    static HBRUSH hbrBack = nullptr;
    static HBRUSH hbrPanel = nullptr;

    switch (msg)
    {
    case WM_CREATE:
    {
        RECT rc = { 0, 0, 920, 640 };
        AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
        SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);

        ApplyPopupTitleBarTheme(hwnd);
        hbrBack = CreateSolidBrush(RGB(32, 34, 37));
        hbrPanel = CreateSolidBrush(RGB(43, 45, 49));

        LOGFONTW lf = {};
        lf.lfHeight = -22;
        lf.lfWeight = FW_BOLD;
        lstrcpyW(lf.lfFaceName, L"맑은 고딕");
        hFontTitle = CreateFontIndirectW(&lf);

        ZeroMemory(&lf, sizeof(lf));
        lf.lfHeight = -15;
        lf.lfWeight = FW_NORMAL;
        lstrcpyW(lf.lfFaceName, L"맑은 고딕");
        hFontSub = CreateFontIndirectW(&lf);

        ZeroMemory(&lf, sizeof(lf));
        lf.lfHeight = -16;
        lf.lfWeight = FW_NORMAL;
        lstrcpyW(lf.lfFaceName, L"맑은 고딕");
        hFontUi = CreateFontIndirectW(&lf);

        hTitle = CreateWindowExW(
            0, L"STATIC", L"Ktin : TinTin++ GUI 도움말",
            WS_CHILD | WS_VISIBLE,
            24, 18, 360, 32,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

        hSub = CreateWindowExW(
            0, L"STATIC", L"현재 소스 기준 기능과 단축키를 정리한 도움말입니다.",
            WS_CHILD | WS_VISIBLE,
            24, 52, 520, 22,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

        hList = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
            24, 92, 220, 470,
            hwnd, (HMENU)10001, GetModuleHandleW(nullptr), nullptr);

        hView = CreateWindowExW(
            WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            260, 92, 630, 470,
            hwnd, (HMENU)10002, GetModuleHandleW(nullptr), nullptr);

        // ★ 닫기 버튼에 &C 추가 (ALT+C 단축키)
        hClose = CreateWindowExW(
            0, L"BUTTON", L"닫기(&C)",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            790, 578, 100, 32,
            hwnd, (HMENU)IDCANCEL, GetModuleHandleW(nullptr), nullptr);

        SendMessageW(hTitle, WM_SETFONT, (WPARAM)hFontTitle, TRUE);
        SendMessageW(hSub, WM_SETFONT, (WPARAM)hFontSub, TRUE);
        SendMessageW(hList, WM_SETFONT, (WPARAM)hFontUi, TRUE);
        SendMessageW(hView, WM_SETFONT, (WPARAM)hFontUi, TRUE);
        SendMessageW(hClose, WM_SETFONT, (WPARAM)hFontUi, TRUE);

        SendMessageW(hView, EM_SETBKGNDCOLOR, 0, RGB(24, 26, 27));

        RECT rcView{};
        GetClientRect(hView, &rcView);
        rcView.left += 12;
        rcView.top += 12;
        rcView.right -= 12;
        rcView.bottom -= 12;
        SendMessageW(hView, EM_SETRECTNP, 0, (LPARAM)&rcView);

        const wchar_t* cats[] = {
            L"기본 / 단축키",
            L"연결 / 주소록",
            L"입력 / 로그창",
            L"단축버튼 / 줄임말",
            L"변수 / 타이머",
            L"안정성 안내",
            L"환경설정 / 화면",
            L"메모장 / 특수기호"
        };
        for (int i = 0; i < (int)(sizeof(cats) / sizeof(cats[0])); ++i)
            SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)cats[i]);

        SendMessageW(hList, LB_SETCURSEL, 0, 0);
        SetHelpRichEditText(hView, BuildHelpPageText(0));

        return 0;
    }

    // ★★★ ALT + C 단축키 처리 추가 ★★★
    case WM_SYSCHAR:
    {
        wchar_t ch = towlower((wchar_t)wParam);
        if (ch == 'c')
        {
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
            return 0;
        }
        break;
    }

    case WM_SIZE:
    {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        const int margin = 24;
        const int topY1 = 18;
        const int topY2 = 52;
        const int contentTop = 92;
        const int leftW = 220;
        const int gap = 16;
        const int btnW = 100;
        const int btnH = 32;
        const int bottomMargin = 18;

        int btnX = w - margin - btnW;
        int btnY = h - bottomMargin - btnH;
        int contentBottom = btnY - 14;
        int contentH = contentBottom - contentTop;
        if (contentH < 100) contentH = 100;

        int rightX = margin + leftW + gap;
        int rightW = w - rightX - margin;

        MoveWindow(hTitle, margin, topY1, w - margin * 2, 32, TRUE);
        MoveWindow(hSub, margin, topY2, w - margin * 2, 22, TRUE);
        MoveWindow(hList, margin, contentTop, leftW, contentH + 3, TRUE);
        MoveWindow(hView, rightX, contentTop, rightW, contentH, TRUE);
        MoveWindow(hClose, btnX, btnY, btnW, btnH, TRUE);

        if (hView)
        {
            RECT rcView{};
            GetClientRect(hView, &rcView);
            rcView.left += 14;
            rcView.top += 10;
            rcView.right -= 14;
            rcView.bottom -= 10;
            SendMessageW(hView, EM_SETRECTNP, 0, (LPARAM)&rcView);
        }
        return 0;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == 10001 && HIWORD(wParam) == LBN_SELCHANGE)
        {
            int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
            if (sel >= 0)
                SetHelpRichEditText(hView, BuildHelpPageText(sel));
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL || LOWORD(wParam) == IDOK)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        HWND hCtl = (HWND)lParam;
        SetBkMode(hdc, TRANSPARENT);
        if (hCtl == hTitle)
        {
            SetTextColor(hdc, RGB(255, 255, 255));
            return (INT_PTR)hbrBack;
        }
        if (hCtl == hSub)
        {
            SetTextColor(hdc, RGB(180, 185, 190));
            return (INT_PTR)hbrBack;
        }
        SetTextColor(hdc, RGB(220, 220, 220));
        return (INT_PTR)hbrBack;
    }

    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, hbrBack);

        RECT rcPanelLeft = { 20, 88, 248, 566 };
        RECT rcPanelRight = { 256, 88, 894, 566 };
        FillRect(hdc, &rcPanelLeft, hbrPanel);
        FillRect(hdc, &rcPanelRight, hbrPanel);
        return 1;
    }

    case WM_DESTROY:
    {
        if (hFontTitle) { DeleteObject(hFontTitle); hFontTitle = nullptr; }
        if (hFontSub) { DeleteObject(hFontSub); hFontSub = nullptr; }
        if (hFontUi) { DeleteObject(hFontUi); hFontUi = nullptr; }
        if (hbrBack) { DeleteObject(hbrBack); hbrBack = nullptr; }
        if (hbrPanel) { DeleteObject(hbrPanel); hbrPanel = nullptr; }
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ==============================================
// 도움말 창 표시 함수
// ==============================================
void ShowShortcutHelp(HWND owner)
{
    static const wchar_t* kClass = L"TTGuiShortcutHelpClass";
    static bool registered = false;

    if (!registered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = ShortcutHelpProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    int w = 920;
    int h = 640;

    // 부모 창(메인 프로그램)의 위치와 크기 가져오기
    RECT rcOwner = { 0 };
    if (owner && IsWindow(owner)) {
        GetWindowRect(owner, &rcOwner);
    }
    else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcOwner, 0);
    }

    // 정중앙 좌표 계산
    int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - w) / 2;
    int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - h) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kClass,
        L"단축키 및 도움말",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (!hwnd)
        return;

    EnableWindow(owner, FALSE);

    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0))
    {
        if (!IsDialogMessageW(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
}

// ==============================================
// 도움말 페이지 텍스트 생성
// ==============================================
static std::wstring BuildHelpPageText(int page)
{
    switch (page)
    {
    default:
    case 0:
        return
            L"[기본 / 단축키]\r\n"
            L"\r\n"
            L"Ktin 2.6 안전판은 TinTin++를 GUI 창에서 다루기 위한 래퍼입니다.\r\n"
            L"장시간 실행 안정성을 위해 트리거, 실시간 하이라이트, 채팅 캡처,\r\n"
            L"자동 로그인 감시는 제거하고 TinTin++ 스크립트에 맡겼습니다.\r\n"
            L"\r\n"
            L"[화면 구성]\r\n"
            L"  1. 로그창\r\n"
            L"  2. 단축버튼 바\r\n"
            L"  3. 3줄 입력창\r\n"
            L"  4. 상태바\r\n"
            L"\r\n"
            L"[주요 단축키]\r\n"
            L"  Alt+Q   : 빠른 연결\r\n"
            L"  Alt+A   : 주소록\r\n"
            L"  Alt+V   : 메모장\r\n"
            L"  Alt+X   : 프로그램 종료\r\n"
            L"  Ctrl+F  : 찾기 창 열기\r\n"
            L"  F4      : 특수기호 창 열기/닫기\r\n"
            L"\r\n"
            L"[로그 / 입력 관련 단축키]\r\n"
            L"  Alt+Space        : 입력창 3줄 전체 비우기\r\n"
            L"  Ctrl+Space       : 현재 로그 화면만 비우기\r\n"
            L"  Ctrl+Shift+Space : 지난 화면까지 모두 비우기\r\n"
            L"  Ctrl+F9          : 연결 끊기(#zap)\r\n";

    case 1:
        return
            L"[연결 / 주소록]\r\n"
            L"\r\n"
            L"[빠른 연결]\r\n"
            L"빠른 연결 창은 메뉴 또는 Alt+Q로 열 수 있습니다.\r\n"
            L"주소, 포트, 문자셋을 선택해 TinTin++ 세션 명령을 보냅니다.\r\n"
            L"\r\n"
            L"[스크립트 읽기]\r\n"
            L"파일 메뉴의 스크립트 읽기는 선택한 파일을 TinTin++에\r\n"
            L"#read {경로} 형식으로 전송합니다.\r\n"
            L"\r\n"
            L"[주소록]\r\n"
            L"주소록은 Alt+A 또는 메뉴로 열 수 있습니다.\r\n"
            L"목록은 이름 / 서버주소:포트 중심으로 표시됩니다.\r\n"
            L"\r\n"
            L"[주소록에 저장되는 정보]\r\n"
            L"  - 서버 이름\r\n"
            L"  - 서버 주소\r\n"
            L"  - 서버 포트\r\n"
            L"  - 서버 문자셋(UTF-8 / EUC-KR CP949)\r\n"
            L"  - TinTin 스크립트 경로\r\n"
            L"  - 최근 접속 시간\r\n"
            L"\r\n"
            L"주소록 자동 재연결은 선택한 주소 항목에서만 동작합니다.\r\n"
            L"이 부분은 main.tin 같은 TinTin++ 스크립트에서 처리하세요.\r\n";

    case 2:
        return
            L"[입력 / 로그창]\r\n"
            L"\r\n"
            L"[입력창]\r\n"
            L"입력창은 3줄 구조입니다. 최근 입력을 3줄 보기 형태로 보여주며,\r\n"
            L"활성 줄에서 Enter를 누르면 TinTin++로 전송합니다.\r\n"
            L"\r\n"
            L"[로그창]\r\n"
            L"로그창은 자체 터미널 셀 버퍼를 사용합니다.\r\n"
            L"현재 화면과 지난 화면을 분리해서 관리하고, 보이는 부분만 그립니다.\r\n"
            L"\r\n"
            L"지원 기능\r\n"
            L"  - ANSI 색상 표시\r\n"
            L"  - 현재 화면 / 지난 화면 복사\r\n"
            L"  - 현재 화면 / 지난 화면 파일 저장\r\n"
            L"  - 마우스 드래그 선택 후 복사\r\n"
            L"  - 마우스 휠, PageUp/PageDown 탐색\r\n"
            L"  - Ctrl+F 로그 검색\r\n";

    case 3:
        return
            L"[단축버튼 / 줄임말]\r\n"
            L"\r\n"
            L"[단축버튼]\r\n"
            L"단축버튼은 최대 10개이며 입력창 위에 가로로 배치됩니다.\r\n"
            L"각 버튼은 라벨, 켜기 명령, 끄기 명령, 토글 여부를 가집니다.\r\n"
            L"Alt+1 ~ Alt+0으로 실행할 수 있습니다.\r\n"
            L"\r\n"
            L"[줄임말]\r\n"
            L"입력한 한 줄이 등록된 줄임말과 완전히 일치할 때만\r\n"
            L"실제 명령으로 바꿔서 TinTin++로 전송합니다.\r\n"
            L"이 기능은 화면 텍스트를 감시하지 않으므로 채팅 캡처/트리거와 다릅니다.\r\n";

    case 4:
        return
            L"[변수 / 타이머]\r\n"
            L"\r\n"
            L"[변수]\r\n"
            L"GUI에서 TinTin++ 변수 명령을 보낼 때 사용하는 보조 기능입니다.\r\n"
            L"문자열, 숫자, 참/거짓, 리스트 자료형을 지원합니다.\r\n"
            L"\r\n"
            L"[타이머]\r\n"
            L"타이머는 시간 기반으로 자동 명령을 실행하는 기능입니다.\r\n"
            L"너무 짧은 간격으로 많은 명령을 보내면 서버나 TinTin++에 부담이 될 수 있으므로\r\n"
            L"필요한 항목만 켜서 사용하세요.\r\n"
            L"\r\n"
            L"트리거 캡처값이나 실시간 하이라이트 연동 설명은 안전판에서 제거되었습니다.\r\n";

    case 5:
        return
            L"[안정성 안내]\r\n"
            L"\r\n"
            L"장시간 실행 중 화면이 하얗게 멈추거나 먹통이 되는 문제와 관련될 수 있는\r\n"
            L"실시간 화면 감시 기능을 제거했습니다.\r\n"
            L"\r\n"
            L"제거된 기능\r\n"
            L"  - 트리거 설정\r\n"
            L"  - 실시간 하이라이트\r\n"
            L"  - 채팅 캡처 패턴\r\n"
            L"  - 채팅 캡처창\r\n"
            L"  - 자동 로그인 화면 감시\r\n"
            L"  - 자동 재접속 화면 감시\r\n"
            L"\r\n"
            L"유지된 기능\r\n"
            L"  - ConPTY 통신\r\n"
            L"  - 터미널 셀 버퍼 렌더링\r\n"
            L"  - 입력창 3줄\r\n"
            L"  - 주소록/빠른 연결/스크립트 읽기\r\n"
            L"  - 복사/저장/찾기/메모장/특수기호\r\n";

    case 6:
        return
            L"[환경설정 / 화면]\r\n"
            L"\r\n"
            L"환경설정 창은 다음 카테고리를 가집니다.\r\n"
            L"  - 일반 설정\r\n"
            L"  - 폰트 및 색상\r\n"
            L"  - 접속 유지\r\n"
            L"  - 기타 설정\r\n"
            L"  - 단축버튼\r\n"
            L"\r\n"
            L"[일반 설정]\r\n"
            L"화면 가로 칸 수, 세로 줄 수, ClearType, 화면 정렬을 설정합니다.\r\n"
            L"\r\n"
            L"[폰트 및 색상]\r\n"
            L"메인창과 입력창의 폰트, 글자색, 배경색을 바꿀 수 있습니다.\r\n"
            L"\r\n"
            L"[기타 설정]\r\n"
            L"입력 저장, 시작 시 빠른 연결/주소록 표시, Backspace 제한,\r\n"
            L"트레이 숨기기 등을 설정합니다.\r\n"
            L"\r\n"
            L"[테마]\r\n"
            L"보기 메뉴에서 ANSI 테마를 선택할 수 있습니다.\r\n";

    case 7:
        return
            L"[메모장 / 특수기호]\r\n"
            L"\r\n"
            L"[메모장]\r\n"
            L"Alt+V로 여는 별도 메모장입니다.\r\n"
            L"파일 열기/저장, 최근 파일, 자동저장, 상태바, 줄 번호,\r\n"
            L"찾기/바꾸기/이동, 인코딩 선택, 구문 강조를 지원합니다.\r\n"
            L"\r\n"
            L"[특수기호]\r\n"
            L"특수기호 창은 F4로 열고 닫습니다.\r\n"
            L"선 그리기/특수기호 입력을 보조합니다.\r\n";
    }

    return L"";
}
