#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "highlight.h"
#include "variables.h"      // VariableItem 사용을 위해
#include "resource.h"
#include "settings.h"
#include "win_util.h"
#include <commctrl.h>
#include <regex>

// ==============================================
// 전역 변수
// ==============================================
HighlightState g_hiState;

struct BrushCache
{
    HBRUSH brush = nullptr;
    COLORREF color = CLR_INVALID;
};

static BrushCache g_hiFgBrush;
static BrushCache g_hiBgBrush;

static HBRUSH GetCachedBrush(BrushCache& cache, COLORREF color)
{
    if (!cache.brush || cache.color != color)
    {
        ResetGdiObjectRef(cache.brush);
        cache.brush = CreateSolidBrush(color);
        cache.color = color;
    }
    return cache.brush ? cache.brush : GetSysColorBrush(COLOR_BTNFACE);
}

static void ClearBrushCache(BrushCache& cache)
{
    if (cache.brush)
    {
        ResetGdiObjectRef(cache.brush);
    }
    cache.color = CLR_INVALID;
}


// ==============================================
// 내부 헬퍼 함수 (static)
// ==============================================
std::wstring ExpandHighlightCaptures(const std::wstring& src, const std::vector<std::wstring>& caps);
bool MatchHighlightPattern(const std::wstring& pattern, const std::wstring& text, std::vector<std::wstring>& caps)
{
    // 안전판: 화면 렌더링/수신 처리 중 실시간 정규식 매칭은 하지 않습니다.
    (void)pattern; (void)text; caps.clear();
    return false;
}


std::wstring ExpandHighlightCaptures(const std::wstring& src, const std::vector<std::wstring>& caps)
{
    if (caps.empty() || src.empty()) return src;

    std::wstring res = src;
    for (int i = 1; i <= 9; ++i)
    {
        if (i >= (int)caps.size()) break;
        wchar_t target[4];
        wsprintfW(target, L"%%%d", i);
        size_t pos = 0;
        while ((pos = res.find(target, pos)) != std::wstring::npos)
        {
            res.replace(pos, 2, caps[i]);
            pos += caps[i].length();
        }
    }
    return res;
}

// ==============================================
// UI 관련 함수들
// ==============================================
void RefreshHiListBox(HWND hList);
void UpdateHiDetailUI(HWND hwnd, int idx);
void SyncHiDataFromUI(HWND hwnd, int idx);

LRESULT CALLBACK HighlightDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ==============================================
// 설정 저장/로드
// ==============================================
void LoadHighlightSettings() {
    std::wstring path = GetSettingsPath();
    g_hiState.rules.clear();
    int count = GetPrivateProfileIntW(L"highlight", L"count", 0, path.c_str());
    for (int i = 0; i < count; i++) {
        HighlightRule r; wchar_t sec[64], val[1024]; wsprintfW(sec, L"rule_%d", i);

        // ★ 별명(name) 불러오기 추가
        GetPrivateProfileStringW(L"highlight", (std::wstring(sec) + L"_name").c_str(), L"", val, 1024, path.c_str());
        r.name = val;

        r.enabled = GetPrivateProfileIntW(L"highlight", (std::wstring(sec) + L"_en").c_str(), 1, path.c_str()) != 0;
        GetPrivateProfileStringW(L"highlight", (std::wstring(sec) + L"_pat").c_str(), L"", val, 1024, path.c_str()); r.pattern = val;
        r.useInverse = GetPrivateProfileIntW(L"highlight", (std::wstring(sec) + L"_inv").c_str(), 1, path.c_str()) != 0;
        GetPrivateProfileStringW(L"highlight", (std::wstring(sec) + L"_fg").c_str(), L"16777215", val, 1024, path.c_str()); r.fg = wcstoul(val, nullptr, 10);
        GetPrivateProfileStringW(L"highlight", (std::wstring(sec) + L"_bg").c_str(), L"0", val, 1024, path.c_str()); r.bg = wcstoul(val, nullptr, 10);
        int fallbackCmd = GetPrivateProfileIntW(L"highlight", (std::wstring(sec) + L"_usecmd").c_str(), 0, path.c_str());
        r.actionType = GetPrivateProfileIntW(L"highlight", (std::wstring(sec) + L"_act").c_str(), fallbackCmd ? 1 : 0, path.c_str());
        if (r.actionType > 1) r.actionType = 1;
        GetPrivateProfileStringW(L"highlight", (std::wstring(sec) + L"_cmd").c_str(), L"", val, 1024, path.c_str()); r.command = val;
        r.useBeep = GetPrivateProfileIntW(L"highlight", (std::wstring(sec) + L"_beep").c_str(), 0, path.c_str()) != 0;
        r.useSound = GetPrivateProfileIntW(L"highlight", (std::wstring(sec) + L"_usesnd").c_str(), 0, path.c_str()) != 0;
        GetPrivateProfileStringW(L"highlight", (std::wstring(sec) + L"_sndpath").c_str(), L"", val, 1024, path.c_str()); r.soundPath = val;

        g_hiState.rules.push_back(r);
    }
    g_hiState.active = !g_hiState.rules.empty();
}

void SaveHighlightSettings() {
    std::wstring path = GetSettingsPath();
    WritePrivateProfileStringW(L"highlight", nullptr, nullptr, path.c_str());
    wchar_t buf[32];
    wsprintfW(buf, L"%d", (int)g_hiState.rules.size());
    WritePrivateProfileStringW(L"highlight", L"count", buf, path.c_str());

    for (int i = 0; i < (int)g_hiState.rules.size(); ++i) {
        const auto& r = g_hiState.rules[i];
        wchar_t sec[64]; wsprintfW(sec, L"rule_%d", i);

        // ★ 별명(name) 저장 추가
        WritePrivateProfileStringW(L"highlight", (std::wstring(sec) + L"_name").c_str(), r.name.c_str(), path.c_str());

        WritePrivateProfileStringW(L"highlight", (std::wstring(sec) + L"_en").c_str(), r.enabled ? L"1" : L"0", path.c_str());
        WritePrivateProfileStringW(L"highlight", (std::wstring(sec) + L"_pat").c_str(), r.pattern.c_str(), path.c_str());
        WritePrivateProfileStringW(L"highlight", (std::wstring(sec) + L"_inv").c_str(), r.useInverse ? L"1" : L"0", path.c_str());
        wsprintfW(buf, L"%u", r.fg); WritePrivateProfileStringW(L"highlight", (std::wstring(sec) + L"_fg").c_str(), buf, path.c_str());
        wsprintfW(buf, L"%u", r.bg); WritePrivateProfileStringW(L"highlight", (std::wstring(sec) + L"_bg").c_str(), buf, path.c_str());
        wsprintfW(buf, L"%d", r.actionType);
        WritePrivateProfileStringW(L"highlight", (std::wstring(sec) + L"_act").c_str(), buf, path.c_str());
        WritePrivateProfileStringW(L"highlight", (std::wstring(sec) + L"_cmd").c_str(), r.command.c_str(), path.c_str());
        WritePrivateProfileStringW(L"highlight", (std::wstring(sec) + L"_beep").c_str(), r.useBeep ? L"1" : L"0", path.c_str());
        WritePrivateProfileStringW(L"highlight", (std::wstring(sec) + L"_usesnd").c_str(), r.useSound ? L"1" : L"0", path.c_str());
        WritePrivateProfileStringW(L"highlight", (std::wstring(sec) + L"_sndpath").c_str(), r.soundPath.c_str(), path.c_str());
    }
}

// ==============================================
// 대화상자 표시
// ==============================================
void ShowHighlightDialog(HWND owner) {
    static const wchar_t* kClass = L"TTGuiHighlightClass";
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = {}; wc.lpfnWndProc = HighlightDialogProc; wc.hInstance = GetModuleHandle(0);
        wc.lpszClassName = kClass; wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassW(&wc); reg = true;
    }

    // 1. 메인 프로그램의 창 핸들을 가장 확실하게 가져옵니다.
    HWND hParent = owner;
    if (!hParent && g_app) hParent = g_app->hwndMain;

    // 2. 일단 창을 '숨긴 상태'로 생성합니다. (WS_VISIBLE 속성 제거)
    // 크기는 WM_CREATE 내부에서 800x440으로 자동 조절됩니다.
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"트리거 설정",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, 800, 440, hParent, nullptr, GetModuleHandle(0), nullptr);

    if (!hDlg) return;

    // 3. 창이 생성된 직후, 부모 창(메인 프로그램)의 정확한 위치를 읽어옵니다.
    if (hParent && IsWindow(hParent)) {
        RECT rcOwner, rcDlg;
        GetWindowRect(hParent, &rcOwner); // 메인 프로그램 창의 좌표
        GetWindowRect(hDlg, &rcDlg);      // 생성된 팝업 창의 실제 크기

        int dlgW = rcDlg.right - rcDlg.left;
        int dlgH = rcDlg.bottom - rcDlg.top;

        // 메인 프로그램 영역 안에서의 '정중앙' 좌표 계산
        int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - dlgW) / 2;
        int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - dlgH) / 2;

        // 4. 계산된 중앙 위치로 이동시키면서 화면에 보여줍니다 (SWP_SHOWWINDOW)
        SetWindowPos(hDlg, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    }
    else {
        ShowWindow(hDlg, SW_SHOW);
    }

    if (hParent) EnableWindow(hParent, FALSE);

    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, 0, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (hParent) {
        EnableWindow(hParent, TRUE);
        SetActiveWindow(hParent);
    }
}

LRESULT CALLBACK HighlightDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static int s_sel = -1;
    switch (msg) {
    case WM_CREATE: {
        // ★ 창 높이를 440으로 줄여 불필요한 하단 여백 제거
        SetWindowPos(hwnd, nullptr, 0, 0, 800, 440, SWP_NOMOVE | SWP_NOZORDER);
        HFONT hF = GetPopupUIFont(hwnd); HINSTANCE hInst = GetModuleHandle(0);

        // 좌측 리스트
        CreateWindowExW(0, L"STATIC", L"규칙 목록", WS_CHILD | WS_VISIBLE, 15, 12, 100, 20, hwnd, 0, hInst, 0);
        HWND hList = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"LISTBOX",
            L"",
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
            15, 28, 220, 338,
            hwnd,
            (HMENU)ID_HI_LIST,
            hInst,
            0);

        // ★ 단축키 추가
        CreateWindowExW(0, L"BUTTON", L"추가(&N)", WS_CHILD | WS_VISIBLE, 15, 355, 50, 28, hwnd, (HMENU)ID_HI_BTN_ADD, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"삭제(&D)", WS_CHILD | WS_VISIBLE, 70, 355, 50, 28, hwnd, (HMENU)ID_HI_BTN_DEL, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"▲(&U)", WS_CHILD | WS_VISIBLE, 125, 355, 30, 28, hwnd, (HMENU)ID_HI_BTN_UP, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"▼(&D)", WS_CHILD | WS_VISIBLE, 160, 355, 30, 28, hwnd, (HMENU)ID_HI_BTN_DOWN, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"복제(&C)", WS_CHILD | WS_VISIBLE, 195, 355, 40, 28, hwnd, (HMENU)ID_HI_BTN_CLONE, hInst, 0);

        // 우측 상세 설정
        CreateWindowExW(0, L"BUTTON", L"규칙 상세 편집", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 250, 15, 520, 335, hwnd, 0, hInst, 0);

        CreateWindowExW(0, L"STATIC", L"규칙 이름(별명):", WS_CHILD | WS_VISIBLE, 270, 43, 100, 20, hwnd, 0, hInst, 0);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 375, 40, 260, 24, hwnd, (HMENU)ID_HI_DET_NAME, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"활성화", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 650, 42, 100, 20, hwnd, (HMENU)ID_HI_DET_ENABLE, hInst, 0);

        CreateWindowExW(0, L"STATIC", L"인식 패턴:", WS_CHILD | WS_VISIBLE, 270, 75, 100, 20, hwnd, 0, hInst, 0);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 270, 95, 480, 24, hwnd, (HMENU)ID_HI_DET_PATTERN, hInst, 0);

        CreateWindowExW(0, L"BUTTON", L"색상 반전", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 270, 130, 80, 20, hwnd, (HMENU)ID_HI_DET_INVERSE, hInst, 0);
        CreateWindowExW(0, L"STATIC", L"글자색:", WS_CHILD | WS_VISIBLE, 370, 130, 50, 20, hwnd, 0, hInst, 0);
        CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_NOTIFY, 425, 130, 30, 20, hwnd, (HMENU)ID_HI_DET_FG, hInst, 0);
        CreateWindowExW(0, L"STATIC", L"배경색:", WS_CHILD | WS_VISIBLE, 470, 130, 50, 20, hwnd, 0, hInst, 0);
        CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_NOTIFY, 525, 130, 30, 20, hwnd, (HMENU)ID_HI_DET_BG, hInst, 0);

        CreateWindowExW(0, L"STATIC", L"실행 동작:", WS_CHILD | WS_VISIBLE, 270, 165, 70, 20, hwnd, 0, hInst, 0);
        HWND hComboAct = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP, 345, 162, 180, 100, hwnd, (HMENU)ID_HI_DET_USECMD, hInst, 0);
        SendMessageW(hComboAct, CB_ADDSTRING, 0, (LPARAM)L"아무것도 안 함");
        SendMessageW(hComboAct, CB_ADDSTRING, 0, (LPARAM)L"명령 실행 (글월 등)");
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 535, 162, 215, 24, hwnd, (HMENU)ID_HI_DET_CMD, hInst, 0);

        CreateWindowExW(0, L"BUTTON", L"기본 비프음 사용", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 270, 200, 200, 20, hwnd, (HMENU)ID_HI_DET_BEEP, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"사운드 파일 재생 (.wav, .mp3)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 270, 235, 220, 20, hwnd, (HMENU)ID_HI_DET_USESOUND, hInst, 0);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 270, 260, 310, 24, hwnd, (HMENU)ID_HI_DET_PATH, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"찾기...", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 590, 260, 70, 24, hwnd, (HMENU)ID_HI_DET_BROWSE, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"미리듣기", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 670, 260, 80, 24, hwnd, (HMENU)ID_HI_DET_PLAY_SOUND, hInst, 0);

        CreateWindowExW(0, L"STATIC", L"※ 안내: 실행 동작을 [아무것도 안 함]으로 선택하고 활성화할 경우,", WS_CHILD | WS_VISIBLE, 270, 295, 500, 18, hwnd, 0, hInst, 0);
        CreateWindowExW(0, L"STATIC", L" 패턴에 일치하는 텍스트는 설정된 색상으로 하이라이트(반전) 됩니다.", WS_CHILD | WS_VISIBLE, 270, 317, 500, 18, hwnd, 0, hInst, 0);

        // ★ 하단 버튼에 & 단축키 추가
        CreateWindowExW(0, L"BUTTON", L"적용(&A)", WS_CHILD | WS_VISIBLE, 580, 355, 90, 32, hwnd, (HMENU)(INT_PTR)ID_HI_BTN_APPLY, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"닫기(&C)", WS_CHILD | WS_VISIBLE, 680, 355, 90, 32, hwnd, (HMENU)IDCANCEL, hInst, 0);

        EnumChildWindows(hwnd, [](HWND c, LPARAM f) { SendMessageW(c, WM_SETFONT, f, TRUE); return TRUE; }, (LPARAM)hF);
        SendMessageW(hList, LB_SETITEMHEIGHT, 0, 20);

        s_sel = -1;
        RefreshHiListBox(hList);
        UpdateHiDetailUI(hwnd, -1);
        return 0;
    }

                  // ★★★ ALT 단축키 처리 추가 ★★★
    case WM_SYSCHAR:
    {
        wchar_t ch = towlower((wchar_t)wParam);
        if (ch == 'n') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_HI_BTN_ADD, BN_CLICKED), 0); return 0; }     // 추가
        if (ch == 'd') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_HI_BTN_DEL, BN_CLICKED), 0); return 0; }     // 삭제
        if (ch == 'u') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_HI_BTN_UP, BN_CLICKED), 0); return 0; }       // ▲
        if (ch == 'a') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_HI_BTN_APPLY, BN_CLICKED), 0); return 0; }   // 적용
        if (ch == 'c') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0); return 0; }           // 닫기
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam; int id = GetDlgCtrlID((HWND)lParam);
        if (s_sel >= 0 && s_sel < (int)g_hiState.rules.size()) {
            if (id == ID_HI_DET_FG) { SetBkColor(hdc, g_hiState.rules[s_sel].fg); return (INT_PTR)GetCachedBrush(g_hiFgBrush, g_hiState.rules[s_sel].fg); }
            if (id == ID_HI_DET_BG) { SetBkColor(hdc, g_hiState.rules[s_sel].bg); return (INT_PTR)GetCachedBrush(g_hiBgBrush, g_hiState.rules[s_sel].bg); }
        }
        SetBkMode(hdc, TRANSPARENT);
        return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_HI_LIST && HIWORD(wParam) == LBN_SELCHANGE) {
            HWND hList = GetDlgItem(hwnd, ID_HI_LIST);
            int newSel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
            if (s_sel >= 0 && s_sel < (int)g_hiState.rules.size())
                SyncHiDataFromUI(hwnd, s_sel);
            s_sel = newSel;
            if (s_sel >= 0 && s_sel < (int)g_hiState.rules.size())
                UpdateHiDetailUI(hwnd, s_sel);
            else
                UpdateHiDetailUI(hwnd, -1);
        }
        else if (id == ID_HI_BTN_ADD) {
            SyncHiDataFromUI(hwnd, s_sel); g_hiState.rules.push_back(HighlightRule()); RefreshHiListBox(GetDlgItem(hwnd, ID_HI_LIST));
            s_sel = (int)g_hiState.rules.size() - 1; SendMessageW(GetDlgItem(hwnd, ID_HI_LIST), LB_SETCURSEL, s_sel, 0); UpdateHiDetailUI(hwnd, s_sel);
        }
        else if (id == ID_HI_BTN_DEL && s_sel >= 0) {
            g_hiState.rules.erase(g_hiState.rules.begin() + s_sel); s_sel = -1; RefreshHiListBox(GetDlgItem(hwnd, ID_HI_LIST)); UpdateHiDetailUI(hwnd, -1);
        }
        else if (id == ID_HI_BTN_CLONE && s_sel >= 0) {
            SyncHiDataFromUI(hwnd, s_sel);
            g_hiState.rules.push_back(g_hiState.rules[s_sel]);
            RefreshHiListBox(GetDlgItem(hwnd, ID_HI_LIST));
            s_sel = (int)g_hiState.rules.size() - 1; SendMessageW(GetDlgItem(hwnd, ID_HI_LIST), LB_SETCURSEL, s_sel, 0); UpdateHiDetailUI(hwnd, s_sel);
        }
        else if (id == ID_HI_DET_FG && s_sel >= 0) {
            if (ChooseColorOnly(hwnd, g_hiState.rules[s_sel].fg)) InvalidateRect(hwnd, nullptr, TRUE);
        }
        else if (id == ID_HI_DET_BG && s_sel >= 0) {
            if (ChooseColorOnly(hwnd, g_hiState.rules[s_sel].bg)) InvalidateRect(hwnd, nullptr, TRUE);
        }
        else if (id == ID_HI_DET_BROWSE && s_sel >= 0) {
            wchar_t f[MAX_PATH] = { 0 };
            OPENFILENAMEW of = {};
            of.lStructSize = sizeof(of);
            of.hwndOwner = hwnd;
            of.lpstrFilter = L"Audio Files (*.wav;*.mp3)\0*.wav;*.mp3\0All Files (*.*)\0*.*\0";
            of.nFilterIndex = 1;
            of.lpstrFile = f;
            of.nMaxFile = MAX_PATH;
            if (GetOpenFileNameW(&of)) {
                SetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_PATH), f);
            }
        }
        else if (id == ID_HI_DET_PLAY_SOUND && s_sel >= 0) {
            wchar_t pathBuf[1024] = { 0 };
            GetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_PATH), pathBuf, 1024);
            std::wstring path = Trim(pathBuf);
            if (!path.empty()) PlayAudioFile(path);
            else MessageBeep(MB_ICONWARNING);
        }
        else if (id == ID_HI_BTN_APPLY || id == IDOK) {
            SyncHiDataFromUI(hwnd, s_sel); g_hiState.active = !g_hiState.rules.empty(); SaveHighlightSettings();
            RefreshHiListBox(GetDlgItem(hwnd, ID_HI_LIST));
            if (g_app->hwndLog) InvalidateRect(g_app->hwndLog, nullptr, FALSE);
            if (id == IDOK) DestroyWindow(hwnd);
        }
        else if (id == ID_HI_BTN_UP) {
            if (s_sel > 0) {
                SyncHiDataFromUI(hwnd, s_sel);
                std::swap(g_hiState.rules[s_sel], g_hiState.rules[s_sel - 1]);
                s_sel--;
                RefreshHiListBox(GetDlgItem(hwnd, ID_HI_LIST));
                SendMessageW(GetDlgItem(hwnd, ID_HI_LIST), LB_SETCURSEL, s_sel, 0);
                UpdateHiDetailUI(hwnd, s_sel);
            }
        }
        else if (id == ID_HI_BTN_DOWN) {
            if (s_sel >= 0 && s_sel < (int)g_hiState.rules.size() - 1) {
                SyncHiDataFromUI(hwnd, s_sel);
                std::swap(g_hiState.rules[s_sel], g_hiState.rules[s_sel + 1]);
                s_sel++;
                RefreshHiListBox(GetDlgItem(hwnd, ID_HI_LIST));
                SendMessageW(GetDlgItem(hwnd, ID_HI_LIST), LB_SETCURSEL, s_sel, 0);
                UpdateHiDetailUI(hwnd, s_sel);
            }
        }
        else if (id == IDCANCEL) DestroyWindow(hwnd);
        return 0;
    }

                   // 나머지 기존 코드 (WM_MEASUREITEM, WM_DRAWITEM, WM_CLOSE 등)는 그대로 유지
    case WM_MEASUREITEM:
    {
        MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*)lParam;
        if (mis && mis->CtlID == ID_HI_LIST)
        {
            mis->itemHeight = 32;
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis && dis->CtlID == ID_HI_LIST)
        {
            if (dis->itemID == (UINT)-1)
                return TRUE;
            wchar_t text[256] = {};
            SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, (LPARAM)text);
            bool isOn = wcsncmp(text, L"[켬]", 3) == 0;
            COLORREF bg = (dis->itemState & ODS_SELECTED)
                ? GetSysColor(COLOR_HIGHLIGHT)
                : GetSysColor(COLOR_WINDOW);
            COLORREF mainText = (dis->itemState & ODS_SELECTED)
                ? GetSysColor(COLOR_HIGHLIGHTTEXT)
                : GetSysColor(COLOR_WINDOWTEXT);
            COLORREF prefixColor = isOn ? RGB(0, 160, 0) : RGB(200, 40, 40);
            UniqueGdiObject hbr(CreateSolidBrush(bg));
            if (hbr.IsValid())
                FillRect(dis->hDC, &dis->rcItem, (HBRUSH)hbr.Get());
            SetBkMode(dis->hDC, TRANSPARENT);
            HFONT hFont = GetPopupUIFont(hwnd);
            ScopedSelectObject fontSel(dis->hDC, hFont);
            RECT rcPrefix = dis->rcItem;
            rcPrefix.left += 8;
            rcPrefix.right = rcPrefix.left + 36;
            RECT rcText = dis->rcItem;
            rcText.left += 48;
            SetTextColor(dis->hDC, prefixColor);
            DrawTextW(dis->hDC, text, 3, &rcPrefix, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            SetTextColor(dis->hDC, mainText);
            DrawTextW(dis->hDC, text + 4, -1, &rcText, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            if (dis->itemState & ODS_FOCUS)
                DrawFocusRect(dis->hDC, &dis->rcItem);
            return TRUE;
        }
        break;
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        ClearBrushCache(g_hiFgBrush);
        ClearBrushCache(g_hiBgBrush);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void RefreshHiListBox(HWND hList) {
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);
    for (const auto& r : g_hiState.rules) {
        std::wstring title = r.enabled ? L"[켬] " : L"[끔] ";

        // ★ 핵심: 별명이 있으면 별명을 보여주고, 없으면 패턴을 보여줌
        if (!r.name.empty()) {
            title += r.name;
        }
        else {
            title += r.pattern.empty() ? L"(새 규칙)" : r.pattern;
        }

        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)title.c_str());
    }
}

void UpdateHiDetailUI(HWND hwnd, int idx) {
    bool hasSel = (idx >= 0 && idx < (int)g_hiState.rules.size());

    // ★ 수정: ID_HI_DET_NAME 도 컨트롤 배열에 추가하여, 선택 해제 시 같이 비활성화되게 함
    int controls[] = { ID_HI_DET_NAME, ID_HI_DET_ENABLE, ID_HI_DET_PATTERN, ID_HI_DET_INVERSE,
                           ID_HI_DET_USECMD, ID_HI_DET_CMD, ID_HI_DET_BEEP,
                           ID_HI_DET_USESOUND, ID_HI_DET_PATH, ID_HI_DET_BROWSE, ID_HI_DET_PLAY_SOUND };
    for (int id : controls) EnableWindow(GetDlgItem(hwnd, id), hasSel);

    if (!hasSel) {
        SetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_NAME), L"");
        SetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_PATTERN), L"");
        SetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_CMD), L"");
        SetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_PATH), L"");
        return;
    }

    const auto& r = g_hiState.rules[idx];
    SetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_NAME), r.name.c_str());
    SendMessageW(GetDlgItem(hwnd, ID_HI_DET_ENABLE), BM_SETCHECK, r.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_PATTERN), r.pattern.c_str());
    SendMessageW(GetDlgItem(hwnd, ID_HI_DET_INVERSE), BM_SETCHECK, r.useInverse ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(hwnd, ID_HI_DET_USECMD), CB_SETCURSEL, r.actionType, 0);
    SetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_CMD), r.command.c_str());
    SendMessageW(GetDlgItem(hwnd, ID_HI_DET_BEEP), BM_SETCHECK, r.useBeep ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(hwnd, ID_HI_DET_USESOUND), BM_SETCHECK, r.useSound ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_PATH), r.soundPath.c_str());

    InvalidateRect(hwnd, nullptr, TRUE); // 색상 박스 갱신
}

void SyncHiDataFromUI(HWND hwnd, int idx) {
    if (idx < 0 || idx >= (int)g_hiState.rules.size()) return;
    auto& r = g_hiState.rules[idx];

    r.enabled = (SendMessageW(GetDlgItem(hwnd, ID_HI_DET_ENABLE), BM_GETCHECK, 0, 0) == BST_CHECKED);
    r.useInverse = (SendMessageW(GetDlgItem(hwnd, ID_HI_DET_INVERSE), BM_GETCHECK, 0, 0) == BST_CHECKED);
    r.actionType = (int)SendMessageW(GetDlgItem(hwnd, ID_HI_DET_USECMD), CB_GETCURSEL, 0, 0);
    r.useBeep = (SendMessageW(GetDlgItem(hwnd, ID_HI_DET_BEEP), BM_GETCHECK, 0, 0) == BST_CHECKED);
    r.useSound = (SendMessageW(GetDlgItem(hwnd, ID_HI_DET_USESOUND), BM_GETCHECK, 0, 0) == BST_CHECKED);

    wchar_t b[1024];
    GetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_NAME), b, 1024); r.name = b; // 별명 저장
    GetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_PATTERN), b, 1024); r.pattern = b;
    GetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_CMD), b, 1024); r.command = b;
    GetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_PATH), b, 1024); r.soundPath = b;
}

