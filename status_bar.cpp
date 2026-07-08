#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "status_bar.h"
#include "theme.h"
#include "resource.h"
#include "settings.h"
#include "log_tail.h"
#include <commctrl.h>

// ==============================================
// 내부 헬퍼 함수들 (static)
// ==============================================

LRESULT CALLBACK StatusBarSettingProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ==============================================
// 상태바 프로시저 (메인 상태바 그리기)
// ==============================================

LRESULT CALLBACK StatusBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);

        // 1. 배경색 칠하기
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));

        if (g_app) {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(0, 0, 0)); // 글자색 검정
            HFONT hFont = GetShortcutButtonUIFont(hwnd);
            HFONT hOld = (HFONT)SelectObject(hdc, hFont);

            int count = g_app->statusPartCount;
            // ★ 크래시 방지 안전벨트
            if (count < 1) count = 1;
            if (count > 5) count = 5;

            int partW = (rc.right - rc.left) / count; // 한 칸의 너비 계산

            for (int i = 0; i < count; ++i) {
                std::wstring text = ExpandStatusVariables(g_app->statusFormats[i]);

                RECT partRc = { rc.left + (i * partW), rc.top, rc.left + ((i + 1) * partW), rc.bottom };
                InflateRect(&partRc, -8, 0);

                // 정렬 옵션 결정
                UINT alignFlag = DT_LEFT; // 기본값
                if (g_app->statusAligns[i] == 1) alignFlag = DT_CENTER;
                else if (g_app->statusAligns[i] == 2) alignFlag = DT_RIGHT;

                // alignFlag를 적용하여 출력
                DrawTextW(hdc, text.c_str(), -1, &partRc, alignFlag | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                // 칸 구분선 그리기 (마지막 칸이 아닐 때만)
                if (i < count - 1) {
                    HPEN hPen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
                    HGDIOBJ oldP = SelectObject(hdc, hPen);
                    MoveToEx(hdc, rc.left + ((i + 1) * partW), rc.top + 4, NULL);
                    LineTo(hdc, rc.left + ((i + 1) * partW), rc.bottom - 4);
                    SelectObject(hdc, oldP);
                    DeleteObject(hPen);
                }
            }
            SelectObject(hdc, hOld);
        }
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ==============================================
// 상태바 설정 대화상자
// ==============================================


LRESULT CALLBACK StatusBarSettingProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        ApplyPopupTitleBarTheme(hwnd);
        HFONT hFont = GetPopupUIFont(hwnd);
        // 안내 글자
        CreateWindowExW(0, L"STATIC", L"상태바 분할 개수 (1~5):", WS_CHILD | WS_VISIBLE, 20, 20, 180, 20, hwnd, 0, 0, 0);
        // 드롭다운 선택창
        HWND hCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP, 200, 17, 60, 150, hwnd, (HMENU)IDC_STATUS_COUNT, 0, 0);
        for (int i = 1; i <= 5; ++i) { wchar_t b[4]; wsprintfW(b, L"%d", i); SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)b); }
        SendMessageW(hCombo, CB_SETCURSEL, g_app->statusPartCount - 1, 0);
        // 5개의 입력 칸 생성
        for (int i = 0; i < 5; ++i) {
            wchar_t lbl[32]; wsprintfW(lbl, L"%d번 칸 양식:", i + 1);
            CreateWindowExW(0, L"STATIC", lbl, WS_CHILD | WS_VISIBLE, 20, 60 + (i * 35), 100, 20, hwnd, 0, 0, 0);
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_app->statusFormats[i].c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 120, 57 + (i * 35), 220, 24, hwnd, (HMENU)(INT_PTR)(IDC_STATUS_EDIT_BASE + i), 0, 0);
            // 오른쪽에 정렬 드롭다운 추가
            HWND hAlign = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP, 350, 57 + (i * 35), 90, 150, hwnd, (HMENU)(INT_PTR)(IDC_STATUS_ALIGN_BASE + i), 0, 0);
            SendMessageW(hAlign, CB_ADDSTRING, 0, (LPARAM)L"왼쪽");
            SendMessageW(hAlign, CB_ADDSTRING, 0, (LPARAM)L"가운데");
            SendMessageW(hAlign, CB_ADDSTRING, 0, (LPARAM)L"오른쪽");
            SendMessageW(hAlign, CB_SETCURSEL, g_app->statusAligns[i], 0);
        }
        // ★ 버튼에 단축키 추가
        CreateWindowExW(0, L"BUTTON", L"확인(&O)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 180, 250, 85, 32, hwnd, (HMENU)IDOK, 0, 0);
        CreateWindowExW(0, L"BUTTON", L"취소(&C)", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 275, 250, 85, 32, hwnd, (HMENU)IDCANCEL, 0, 0);
        CreateWindowExW(0, L"BUTTON", L"적용(&A)", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 370, 250, 85, 32, hwnd, (HMENU)IDC_STATUS_APPLY, 0, 0);

        EnumChildWindows(hwnd, [](HWND c, LPARAM lp) -> BOOL { SendMessageW(c, WM_SETFONT, lp, TRUE); return TRUE; }, (LPARAM)hFont);
        return 0;
    }

    case WM_SYSCHAR:
    {
        wchar_t ch = towlower((wchar_t)wParam);
        if (ch == 'o') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0); return 0; }
        if (ch == 'c') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0); return 0; }
        if (ch == 'a') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_STATUS_APPLY, BN_CLICKED), 0); return 0; }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        // [확인] 또는 [적용] 버튼을 눌렀을 때 (공통 로직)
        if (id == IDOK || id == IDC_STATUS_APPLY) {
            // 1. 화면의 설정값들을 변수에 저장
            g_app->statusPartCount = (int)SendMessageW(GetDlgItem(hwnd, IDC_STATUS_COUNT), CB_GETCURSEL, 0, 0) + 1;
            for (int i = 0; i < 5; ++i) {
                wchar_t buf[256];
                GetWindowTextW(GetDlgItem(hwnd, IDC_STATUS_EDIT_BASE + i), buf, 256);
                g_app->statusFormats[i] = buf;
                g_app->statusAligns[i] = (int)SendMessageW(GetDlgItem(hwnd, IDC_STATUS_ALIGN_BASE + i), CB_GETCURSEL, 0, 0);
            }
            // 2. 상태바 즉시 갱신 및 파일 저장
            if (g_app->hwndStatusBar) InvalidateRect(g_app->hwndStatusBar, NULL, TRUE);
            SaveWindowSettings(g_app->hwndMain);
            // 3. [확인]일 때만 창을 닫고, [적용]일 때는 창을 유지함
            if (id == IDOK) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        if (id == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)wParam, TRANSPARENT); return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
void PromptStatusBarDialog(HWND owner) {
    static const wchar_t* kClass = L"TTStatusBarSettingClass";
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = StatusBarSettingProc;
        wc.hInstance = GetModuleHandle(0);
        wc.lpszClassName = kClass;
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc); reg = true;
    }

    // 1. 창을 먼저 만듭니다. (크기는 가로 480, 세로 340)
    int dlgW = 480;
    int dlgH = 340;

    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"상태바 설정",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        0, 0, dlgW, dlgH, owner, 0, GetModuleHandle(0), 0);

    if (!hDlg) return;

    // 2. 중앙 배치를 위한 계산 작업
    RECT rcO, rcD;
    GetWindowRect(owner, &rcO); // 부모 창(프로그램 전체)의 위치와 크기
    GetWindowRect(hDlg, &rcD);  // 방금 만든 설정 창의 위치와 크기

    int parentW = rcO.right - rcO.left;
    int parentH = rcO.bottom - rcO.top;
    int selfW = rcD.right - rcD.left;
    int selfH = rcD.bottom - rcD.top;

    // 부모 창의 정중앙 좌표 계산
    int x = rcO.left + (parentW - selfW) / 2;
    int y = rcO.top + (parentH - selfH) / 2;

    // 3. 계산된 위치로 창을 옮깁니다.
    SetWindowPos(hDlg, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);

    // 4. 메시지 루프 (창이 닫힐 때까지 대기)
    EnableWindow(owner, FALSE);
    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, 0, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
}

// ==============================================
// 상태바 변수 확장 함수
// ==============================================
std::wstring ExpandStatusVariables(const std::wstring& format) {
    if (!g_app) return format;
    std::wstring result = format;

    for (const auto& var : g_app->variables) {
        std::wstring target = L"$" + var.name;
        size_t pos = 0;
        while ((pos = result.find(target, pos)) != std::wstring::npos) {

            // ★ 수정된 핵심 부분: var.value를 그냥 넣지 않고 함수를 거쳐서 넣습니다!
            std::wstring formattedValue = FormatNumberWithCommas(var.value);

            result.replace(pos, target.length(), formattedValue);
            pos += formattedValue.length();
        }
    }
    return result;
}

// ==============================================
// 메인 메뉴 생성 및 커스텀 메뉴
// ==============================================

void CreateMainMenu(HWND hwnd)
{
    HMENU hMenuBar = CreateMenu();
    HMENU hMenuFile = CreatePopupMenu();
    HMENU hMenuEdit = CreatePopupMenu();
    HMENU hMenuView = CreatePopupMenu();
    HMENU hMenuOptions = CreatePopupMenu();
    HMENU hMenuHelp = CreatePopupMenu();

    HMENU hMenuPast = CreatePopupMenu();
    HMENU hMenuCur = CreatePopupMenu();
    HMENU hMenuCapture = CreatePopupMenu();
    HMENU hMenuTail = CreatePopupMenu();

    auto AddODItem = [](HMENU hMenu, UINT_PTR id, const wchar_t* text)
        {
            AppendMenuW(hMenu, MF_STRING, id, text);
        };

    auto AddODPopup = [](HMENU hMenu, HMENU hPopup, const wchar_t* text)
        {
            AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hPopup, text);
        };

    AddODItem(hMenuFile, ID_MENU_FILE_NEW_WINDOW, L"새 창 띄우기");
    AddODItem(hMenuFile, ID_MENU_FILE_QUICK_CONNECT, L"빠른 연결...\tAlt+Q");
    AddODItem(hMenuFile, ID_MENU_FILE_ADDRESSBOOK, L"주소록...(&A)\tAlt+A");
    AddODItem(hMenuFile, ID_MENU_FILE_ZAP, L"연결 끊기(#ZAP)\tCtrl+F9");
    AddODItem(hMenuFile, ID_MENU_FILE_READ_SCRIPT, L"스크립트 읽기...(&S)");
    AddODItem(hMenuFile, ID_MENU_EDIT_MEMO, L"메모장...\tAlt+V");
    AppendMenuW(hMenuFile, MF_SEPARATOR, 0, nullptr);
    AddODItem(hMenuFile, ID_MENU_EXIT, L"끝내기\tAlt+X");

    AddODItem(hMenuPast, ID_MENU_EDIT_COPY_PAST, L"클립보드로 복사");
    AddODItem(hMenuPast, ID_MENU_EDIT_SAVE_PAST, L"파일로 저장...");

    AddODItem(hMenuCur, ID_MENU_EDIT_COPY_CUR, L"클립보드로 복사");
    AddODItem(hMenuCur, ID_MENU_EDIT_SAVE_CUR, L"파일로 저장...");

    AddODItem(hMenuTail, ID_MENU_CAPTURE_TAIL_ALL, L"전체");
    AddODItem(hMenuTail, ID_MENU_CAPTURE_TAIL_CHAT, L"잡담");
    AddODItem(hMenuTail, ID_MENU_CAPTURE_TAIL_AUCTION, L"경매");
    AddODItem(hMenuTail, ID_MENU_CAPTURE_TAIL_TALK, L"대화");
    AddODItem(hMenuTail, ID_MENU_CAPTURE_TAIL_ITEM, L"아이템 획득");
    AddODItem(hMenuTail, ID_MENU_CAPTURE_TAIL_EXP, L"경험치");
    AddODItem(hMenuTail, ID_MENU_CAPTURE_TAIL_USER1, GetTailModeMenuTitle(7).c_str());
    AddODItem(hMenuTail, ID_MENU_CAPTURE_TAIL_USER2, GetTailModeMenuTitle(8).c_str());
    AddODItem(hMenuTail, ID_MENU_CAPTURE_TAIL_USER3, GetTailModeMenuTitle(9).c_str());
    AddODItem(hMenuTail, ID_MENU_CAPTURE_TAIL_CUSTOM, L"임시 문자열...");

    // 갈무리 메뉴는 저장 켜기/끄기와 폴더 열기만 둡니다.
    // 보기 창과 필터 설정은 보기 메뉴의 상위 항목으로 분리합니다.
    AddODItem(hMenuCapture, ID_MENU_CAPTURE_TOGGLE, L"갈무리 꺼짐");
    if (HasCaptureTailWindows())
        AddODItem(hMenuCapture, ID_MENU_CAPTURE_CLOSE_ALL, L"갈무리창 모두 닫기");
    AddODItem(hMenuCapture, ID_MENU_CAPTURE_OPEN_FOLDER, L"갈무리 폴더 열기");

    AddODItem(hMenuEdit, ID_MENU_FIND_DIALOG, L"찾기...\tCtrl+F");
    AddODItem(hMenuEdit, ID_MENU_EDIT_VARIABLE, L"변수 설정(&V)...");
    AddODItem(hMenuEdit, ID_MENU_EDIT_ABBREVIATION, L"줄임말 설정(&B)...");
    AddODItem(hMenuEdit, ID_MENU_EDIT_FUNCTION_SHORTCUT, L"단축키 설정(&K)...");
    AddODItem(hMenuEdit, ID_MENU_EDIT_TIMER, L"타이머 설정(&T)...");
    AddODItem(hMenuEdit, ID_EDIT_STATUSBAR, L"상태바 설정(&S)...");
    AddODItem(hMenuEdit, ID_MENU_EDIT_NUMPAD, L"숫자 키패드 매크로...(&N)");
    AppendMenuW(hMenuEdit, MF_SEPARATOR, 0, nullptr);
    AddODPopup(hMenuEdit, hMenuPast, L"지난 화면을");
    AddODPopup(hMenuEdit, hMenuCur, L"현재 화면을");

    AddODItem(hMenuView, ID_MENU_VIEW_HIDE_MENU, L"메뉴 숨기기(&M)");
    AddODPopup(hMenuView, hMenuCapture, L"갈무리(&L)");
    AddODPopup(hMenuView, hMenuTail, L"갈무리 보기(&G)");
    AddODItem(hMenuView, ID_MENU_CAPTURE_FILTER_SETTINGS, L"갈무리 필터 설정...");
    AddODItem(hMenuView, ID_MENU_THEME_DIALOG, L"ANSI 테마 선택...(&T)");
    AddODItem(hMenuView, ID_MENU_OPTIONS_FIT_WINDOW, L"화면 여백 없애기(&S)");
    AppendMenuW(hMenuView, MF_SEPARATOR, 0, nullptr);
    AddODItem(hMenuView, ID_MENU_VIEW_SYMBOLS, L"특수 기호...(&S)\tF4");

    AddODItem(hMenuOptions, ID_MENU_SETTINGS, L"환경 설정...(&O)");
    AddODItem(hMenuOptions, ID_MENU_OPTIONS_SHORTCUTBAR, L"단축버튼 표시(&H)");
    AddODItem(hMenuOptions, ID_MENU_OPTIONS_KEEPALIVE_TOGGLE, L"접속 유지 켜기(&K)");

    AddODItem(hMenuHelp, ID_MENU_HELP_SHORTCUT, L"단축키 도움말(&S)");
    AddODItem(hMenuHelp, ID_MENU_HELP_ABOUT, L"정보(&A)");

    AddODPopup(hMenuBar, hMenuFile, L"파일(&F)");
    AddODPopup(hMenuBar, hMenuEdit, L"편집(&E)");
    AddODPopup(hMenuBar, hMenuView, L"보기(&V)");
    AddODPopup(hMenuBar, hMenuOptions, L"옵션(&O)");
    AddODPopup(hMenuBar, hMenuHelp, L"도움말(&H)");

    g_app->hMainMenu = hMenuBar;
}

void ShowCustomMenuPopup(HWND hwnd, int menuIndex)
{
    if (!g_app || !g_app->hMainMenu || menuIndex < 0)
        return;

    // ★ 신규 추가: 메뉴가 화면에 그려지기 직전에 무조건 글씨를 최신 상태로 갱신합니다!
    UpdateMenuToggleStates();

    HMENU hPopup = GetSubMenu(g_app->hMainMenu, menuIndex);
    if (!hPopup)
        return;

    int x = 6;
    for (int i = 0; i < menuIndex; ++i)
        x += GetCustomMenuItemWidth(i) + 4;

    int y = g_app->customMenuHeight;

    POINT pt = { x, y };
    ClientToScreen(hwnd, &pt);

    g_app->hotMenuIndex = menuIndex;
    InvalidateRect(hwnd, nullptr, FALSE);
    UpdateWindow(hwnd);

    SetForegroundWindow(hwnd);

    TrackPopupMenu(
        hPopup,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON,
        pt.x,
        pt.y,
        0,
        hwnd,
        nullptr);

    PostMessageW(hwnd, WM_NULL, 0, 0);

    g_app->hotMenuIndex = -1;
    InvalidateRect(hwnd, nullptr, FALSE);
    UpdateWindow(hwnd);
}
