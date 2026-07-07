#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "settings.h"
#include "theme.h"
#include "memo.h"
#include "resource.h"
#include "chat_capture.h"
#include <commctrl.h>

// 필요한 전방 선언 (다른 파일에 정의된 함수들)
extern void ApplyStyles();
extern void ApplyShortcutButtons(HWND hwnd);
extern void UpdateMenuToggleStates();
extern int GetFontPointSizeFromLogFont(const LOGFONTW& lf);
extern bool ChooseFontOnly(HWND owner, LOGFONTW& lf);
extern bool ChooseColorOnly(HWND owner, COLORREF& color);
extern bool ChooseBackgroundColor(HWND owner, COLORREF& color);

// ==============================================
// 1. 기본 경로 및 유틸 함수
// ==============================================
void ConfigureLogStyle(HWND hwnd)
{
    if (!g_app) return;
    bool changed = false;
    if (ChooseFontOnly(hwnd, g_app->logStyle.font)) changed = true;
    if (ChooseColorOnly(hwnd, g_app->logStyle.textColor)) changed = true;
    if (changed) ApplyStyles();
}

void ConfigureInputStyle(HWND hwnd)
{
    if (!g_app) return;
    bool changed = false;
    if (ChooseFontOnly(hwnd, g_app->inputStyle.font)) changed = true;
    if (ChooseColorOnly(hwnd, g_app->inputStyle.textColor)) changed = true;
    if (changed) ApplyStyles();
}

// ==============================================
// 2. 팝업 UI 상태 업데이트 헬퍼 (순서 중요: Proc 이전에 위치)
// ==============================================
void UpdateSettingPreviews(HWND hwnd) {
    if (!g_app) return;

    wchar_t buf[256];
    const wchar_t* logFace = g_app->useCustomMudFont ? L"Mud둥근모" : g_app->logStyle.font.lfFaceName;
    const wchar_t* inpFace = g_app->useCustomMudFont ? L"Mud둥근모" : g_app->inputStyle.font.lfFaceName;

    wsprintfW(buf, L"폰트: %s (%dpt)", g_app->logStyle.font.lfFaceName, GetFontPointSizeFromLogFont(g_app->logStyle.font));
    SetWindowTextW(GetDlgItem(hwnd, ID_SET_PREVIEW_LOG_INFO), buf);

    wsprintfW(buf, L"폰트: %s (%dpt)", g_app->inputStyle.font.lfFaceName, GetFontPointSizeFromLogFont(g_app->inputStyle.font));
    SetWindowTextW(GetDlgItem(hwnd, ID_SET_PREVIEW_INP_INFO), buf);


    InvalidateRect(GetDlgItem(hwnd, ID_SET_PREVIEW_LOG_TEXT), nullptr, TRUE);
    InvalidateRect(GetDlgItem(hwnd, ID_SET_PREVIEW_LOG_BACK), nullptr, TRUE);
    InvalidateRect(GetDlgItem(hwnd, ID_SET_PREVIEW_INP_TEXT), nullptr, TRUE);
    InvalidateRect(GetDlgItem(hwnd, ID_SET_PREVIEW_INP_BACK), nullptr, TRUE);
}

void SwitchSettingsPane(SettingsDlgState* state, int index) {
    if (!state) return;
    for (int i = 0; i < 5; ++i) {
        int show = (i == index) ? SW_SHOW : SW_HIDE;
        if (state->hwndGroups[i]) ShowWindow(state->hwndGroups[i], show);
        for (HWND h : state->panelCtrls[i]) ShowWindow(h, show);
    }
    state->currentIdx = index;
}

// ==============================================
// 3. 환경설정 대화상자 프로시저
// ==============================================
void ApplyKeepAliveTimer(HWND hwnd)
{
    if (!g_app || !hwnd)
        return;

    KillTimer(hwnd, ID_TIMER_KEEPALIVE);

    if (!g_app->keepAliveEnabled)
        return;

    UINT intervalMs = (UINT)(g_app->keepAliveIntervalSec * 1000);
    if (intervalMs < 5000)
        intervalMs = 5000;

    SetTimer(hwnd, ID_TIMER_KEEPALIVE, intervalMs, nullptr);
}


static LRESULT CALLBACK SettingsDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    SettingsDlgState* state = (SettingsDlgState*)GetPropW(hwnd, L"SettingsState");

    switch (msg)
    {
    case WM_CREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        state = (SettingsDlgState*)cs->lpCreateParams;
        SetPropW(hwnd, L"SettingsState", state);

        HFONT hFont = GetPopupUIFont(hwnd);
        HINSTANCE hInst = GetModuleHandleW(nullptr);

        auto AddToPanel = [&](int p, HWND h) { if (h) state->panelCtrls[p].push_back(h); };
        const wchar_t* categories[] = { L"일반 설정", L"폰트 및 색상", L"접속 유지", L"기타 설정", L"단축버튼" };

        state->hwndList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
            12, 12, 140, 475, hwnd, (HMENU)(INT_PTR)ID_SETTING_LIST, hInst, nullptr);

        for (int i = 0; i < 5; ++i)
            SendMessageW(state->hwndList, LB_ADDSTRING, 0, (LPARAM)categories[i]);

        SendMessageW(state->hwndList, LB_SETITEMHEIGHT, 0, 26);
        SendMessageW(state->hwndList, LB_SETCURSEL, 0, 0);

        for (int i = 0; i < 5; ++i) {
            state->hwndGroups[i] = CreateWindowExW(0, L"BUTTON", categories[i], WS_CHILD | BS_GROUPBOX, 165, 5, 410, 482, hwnd, nullptr, hInst, nullptr);
        }

        // --- 패널 0: 일반 설정 ---
        AddToPanel(0, CreateWindowExW(0, L"STATIC", L"화면 가로칸 수:", WS_CHILD, 185, 40, 100, 20, hwnd, nullptr, hInst, nullptr));
        HWND hEditCols = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_NUMBER, 300, 37, 60, 24, hwnd, (HMENU)(INT_PTR)ID_SET_EDIT_COLS, hInst, nullptr);
        AddToPanel(0, hEditCols);
        AddToPanel(0, CreateWindowExW(0, L"STATIC", L"화면 세로줄 수:", WS_CHILD, 185, 75, 100, 20, hwnd, nullptr, hInst, nullptr));
        HWND hEditRows = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_NUMBER, 300, 72, 60, 24, hwnd, (HMENU)(INT_PTR)ID_SET_EDIT_ROWS, hInst, nullptr);
        AddToPanel(0, hEditRows);

        AddToPanel(0, CreateWindowExW(0, L"BUTTON", L"화면 여백 설정(px)", WS_CHILD | BS_GROUPBOX, 185, 105, 370, 58, hwnd, nullptr, hInst, nullptr));
        AddToPanel(0, CreateWindowExW(0, L"STATIC", L"왼쪽:", WS_CHILD, 198, 128, 42, 18, hwnd, nullptr, hInst, nullptr));
        HWND hMarginL = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_NUMBER, 238, 124, 42, 22, hwnd, (HMENU)(INT_PTR)ID_SET_MARGIN_LEFT, hInst, nullptr);
        AddToPanel(0, hMarginL);
        AddToPanel(0, CreateWindowExW(0, L"STATIC", L"오른쪽:", WS_CHILD, 286, 128, 52, 18, hwnd, nullptr, hInst, nullptr));
        HWND hMarginR = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_NUMBER, 340, 124, 42, 22, hwnd, (HMENU)(INT_PTR)ID_SET_MARGIN_RIGHT, hInst, nullptr);
        AddToPanel(0, hMarginR);
        AddToPanel(0, CreateWindowExW(0, L"STATIC", L"위:", WS_CHILD, 390, 128, 26, 18, hwnd, nullptr, hInst, nullptr));
        HWND hMarginT = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_NUMBER, 417, 124, 42, 22, hwnd, (HMENU)(INT_PTR)ID_SET_MARGIN_TOP, hInst, nullptr);
        AddToPanel(0, hMarginT);
        AddToPanel(0, CreateWindowExW(0, L"STATIC", L"아래:", WS_CHILD, 467, 128, 42, 18, hwnd, nullptr, hInst, nullptr));
        HWND hMarginB = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_NUMBER, 510, 124, 42, 22, hwnd, (HMENU)(INT_PTR)ID_SET_MARGIN_BOTTOM, hInst, nullptr);
        AddToPanel(0, hMarginB);

        HWND hChkSmooth = CreateWindowExW(0, L"BUTTON", L"폰트 부드럽게 표시 (ClearType)", WS_CHILD | BS_AUTOCHECKBOX, 185, 170, 250, 24, hwnd, (HMENU)(INT_PTR)ID_SET_CHK_SMOOTH, hInst, nullptr);
        AddToPanel(0, hChkSmooth);

        AddToPanel(0, CreateWindowExW(0, L"STATIC", L"화면 정렬:", WS_CHILD, 185, 205, 80, 20, hwnd, nullptr, hInst, nullptr));
        HWND hAlignL = CreateWindowExW(0, L"BUTTON", L"왼쪽", WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP, 270, 203, 60, 24, hwnd, (HMENU)(UINT_PTR)ID_SET_ALIGN_LEFT, hInst, nullptr);
        HWND hAlignC = CreateWindowExW(0, L"BUTTON", L"중앙", WS_CHILD | BS_AUTORADIOBUTTON, 335, 203, 60, 24, hwnd, (HMENU)(UINT_PTR)ID_SET_ALIGN_CENTER, hInst, nullptr);
        HWND hAlignR = CreateWindowExW(0, L"BUTTON", L"오른쪽", WS_CHILD | BS_AUTORADIOBUTTON, 400, 203, 70, 24, hwnd, (HMENU)(UINT_PTR)ID_SET_ALIGN_RIGHT, hInst, nullptr);
        AddToPanel(0, hAlignL); AddToPanel(0, hAlignC); AddToPanel(0, hAlignR);

        int alY = 245;
        HWND hChkAL = CreateWindowExW(0, L"BUTTON", L"접속 후 60초 자동 로그인 패턴 검사", WS_CHILD | BS_AUTOCHECKBOX,
            185, alY, 310, 24, hwnd, (HMENU)(INT_PTR)ID_SET_CHK_AUTOLOGIN, hInst, nullptr);
        AddToPanel(0, hChkAL);
        AddToPanel(0, CreateWindowExW(0, L"STATIC", L"아이디 패턴:", WS_CHILD, 185, alY + 32, 90, 20, hwnd, nullptr, hInst, nullptr));
        HWND hEditAlIdPat = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 275, alY + 28, 135, 24, hwnd, (HMENU)(INT_PTR)ID_SET_EDIT_AL_ID_PAT, hInst, nullptr);
        AddToPanel(0, hEditAlIdPat);
        AddToPanel(0, CreateWindowExW(0, L"STATIC", L"아이디:", WS_CHILD, 415, alY + 32, 55, 20, hwnd, nullptr, hInst, nullptr));
        HWND hEditAlId = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 465, alY + 28, 90, 24, hwnd, (HMENU)(INT_PTR)ID_SET_EDIT_AL_ID, hInst, nullptr);
        AddToPanel(0, hEditAlId);
        AddToPanel(0, CreateWindowExW(0, L"STATIC", L"비번 패턴:", WS_CHILD, 185, alY + 62, 90, 20, hwnd, nullptr, hInst, nullptr));
        HWND hEditAlPwPat = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 275, alY + 58, 135, 24, hwnd, (HMENU)(INT_PTR)ID_SET_EDIT_AL_PW_PAT, hInst, nullptr);
        AddToPanel(0, hEditAlPwPat);
        AddToPanel(0, CreateWindowExW(0, L"STATIC", L"비번:", WS_CHILD, 415, alY + 62, 55, 20, hwnd, nullptr, hInst, nullptr));
        HWND hEditAlPw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | ES_PASSWORD, 465, alY + 58, 90, 24, hwnd, (HMENU)(INT_PTR)ID_SET_EDIT_AL_PW, hInst, nullptr);
        AddToPanel(0, hEditAlPw);
        AddToPanel(0, CreateWindowExW(0, L"STATIC", L"※ 접속 후 60초만 검사하고 아이디/비번 전송 후 즉시 OFF", WS_CHILD, 185, alY + 95, 395, 20, hwnd, nullptr, hInst, nullptr));

        if (g_app->termAlign == 0) SendMessageW(hAlignL, BM_SETCHECK, BST_CHECKED, 0);
        else if (g_app->termAlign == 2) SendMessageW(hAlignR, BM_SETCHECK, BST_CHECKED, 0);
        else SendMessageW(hAlignC, BM_SETCHECK, BST_CHECKED, 0);

        // --- 패널 1: 폰트/색상 ---
        int startX = 185; int nextX = 395; int groupW = 430;

        HWND hChkUseMudFont = CreateWindowExW(0, L"BUTTON", L"전용 폰트(Mud둥근모) 우선 사용",
            WS_CHILD | BS_AUTOCHECKBOX, startX, 20, 300, 24, hwnd, (HMENU)(INT_PTR)ID_SET_CHK_USE_MUD_FONT, hInst, nullptr);
        AddToPanel(1, hChkUseMudFont);
        SendMessageW(hChkUseMudFont, BM_SETCHECK, g_app->useCustomMudFont ? BST_CHECKED : BST_UNCHECKED, 0);

        AddToPanel(1, CreateWindowExW(0, L"BUTTON", L"메인창 설정", WS_CHILD | BS_GROUPBOX, startX - 5, 45, groupW, 140, hwnd, nullptr, hInst, nullptr));
        AddToPanel(1, CreateWindowExW(0, L"BUTTON", L"폰트 변경...", WS_CHILD, startX, 65, 150, 30, hwnd, (HMENU)(INT_PTR)ID_SET_BTN_LOG_FONT, hInst, nullptr));
        AddToPanel(1, CreateWindowExW(0, L"BUTTON", L"글자색 변경...", WS_CHILD, nextX, 65, 150, 30, hwnd, (HMENU)(INT_PTR)ID_SET_BTN_LOG_COLOR, hInst, nullptr));
        AddToPanel(1, CreateWindowExW(0, L"BUTTON", L"배경색 변경...", WS_CHILD, nextX, 105, 150, 30, hwnd, (HMENU)(INT_PTR)ID_SET_BTN_MAIN_BG, hInst, nullptr));
        AddToPanel(1, CreateWindowExW(0, L"STATIC", L"폰트: -", WS_CHILD, startX + 5, 105, 205, 20, hwnd, (HMENU)(INT_PTR)ID_SET_PREVIEW_LOG_INFO, hInst, nullptr));
        AddToPanel(1, CreateWindowExW(0, L"STATIC", L"글자색:", WS_CHILD, startX + 5, 130, 55, 20, hwnd, nullptr, hInst, nullptr));
        AddToPanel(1, CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"", WS_CHILD | SS_NOTIFY, startX + 60, 130, 18, 18, hwnd, (HMENU)(INT_PTR)ID_SET_PREVIEW_LOG_TEXT, hInst, nullptr));
        AddToPanel(1, CreateWindowExW(0, L"STATIC", L"배경색:", WS_CHILD, startX + 95, 130, 55, 20, hwnd, nullptr, hInst, nullptr));
        AddToPanel(1, CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"", WS_CHILD | SS_NOTIFY, startX + 150, 130, 18, 18, hwnd, (HMENU)(INT_PTR)ID_SET_PREVIEW_LOG_BACK, hInst, nullptr));

        int inpY = 200;
        AddToPanel(1, CreateWindowExW(0, L"BUTTON", L"입력창 설정", WS_CHILD | BS_GROUPBOX, startX - 5, inpY, groupW, 140, hwnd, nullptr, hInst, nullptr));
        AddToPanel(1, CreateWindowExW(0, L"BUTTON", L"폰트 변경...", WS_CHILD, startX, inpY + 20, 150, 30, hwnd, (HMENU)(INT_PTR)ID_SET_BTN_INP_FONT, hInst, nullptr));
        AddToPanel(1, CreateWindowExW(0, L"BUTTON", L"글자색 변경...", WS_CHILD, nextX, inpY + 20, 150, 30, hwnd, (HMENU)(INT_PTR)ID_SET_BTN_INP_COLOR, hInst, nullptr));
        AddToPanel(1, CreateWindowExW(0, L"BUTTON", L"배경색 변경...", WS_CHILD, nextX, inpY + 60, 150, 30, hwnd, (HMENU)(INT_PTR)ID_SET_BTN_INP_BG, hInst, nullptr));
        AddToPanel(1, CreateWindowExW(0, L"STATIC", L"폰트: -", WS_CHILD, startX + 5, inpY + 60, 205, 20, hwnd, (HMENU)(INT_PTR)ID_SET_PREVIEW_INP_INFO, hInst, nullptr));
        AddToPanel(1, CreateWindowExW(0, L"STATIC", L"글자색:", WS_CHILD, startX + 5, inpY + 85, 55, 20, hwnd, nullptr, hInst, nullptr));
        AddToPanel(1, CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"", WS_CHILD | SS_NOTIFY, startX + 60, inpY + 85, 18, 18, hwnd, (HMENU)(INT_PTR)ID_SET_PREVIEW_INP_TEXT, hInst, nullptr));
        AddToPanel(1, CreateWindowExW(0, L"STATIC", L"배경색:", WS_CHILD, startX + 95, inpY + 85, 55, 20, hwnd, nullptr, hInst, nullptr));
        AddToPanel(1, CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"", WS_CHILD | SS_NOTIFY, startX + 150, inpY + 85, 18, 18, hwnd, (HMENU)(INT_PTR)ID_SET_PREVIEW_INP_BACK, hInst, nullptr));

        // 채팅 캡처창 설정은 안정성 버전에서 제거했습니다.

        // --- 패널 2: 접속유지 ---
        HWND hChkKA = CreateWindowExW(0, L"BUTTON", L"접속 유지 사용", WS_CHILD | BS_AUTOCHECKBOX, 185, 40, 150, 24, hwnd, (HMENU)(INT_PTR)ID_SET_CHK_KA_ENABLE, hInst, nullptr);
        AddToPanel(2, hChkKA);
        AddToPanel(2, CreateWindowExW(0, L"STATIC", L"전송 간격(초):", WS_CHILD, 185, 80, 100, 20, hwnd, nullptr, hInst, nullptr));
        HWND hEditKAInt = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_NUMBER, 300, 77, 60, 24, hwnd, (HMENU)(INT_PTR)ID_SET_EDIT_KA_INT, hInst, nullptr);
        AddToPanel(2, hEditKAInt);
        AddToPanel(2, CreateWindowExW(0, L"STATIC", L"전송 명령:", WS_CHILD, 185, 115, 100, 20, hwnd, nullptr, hInst, nullptr));
        HWND hEditKACmd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD, 185, 140, 360, 24, hwnd, (HMENU)(INT_PTR)ID_SET_EDIT_KA_CMD, hInst, nullptr);
        AddToPanel(2, hEditKACmd);
        AddToPanel(2, CreateWindowExW(0, L"STATIC", L"※ 명령을 입력하지 않으면 엔터를 전송합니다.", WS_CHILD, 185, 170, 360, 20, hwnd, nullptr, hInst, nullptr));

        // --- 패널 3: 기타 ---
        HWND hChkSaveInp = CreateWindowExW(0, L"BUTTON", L"종료 시 입력창 내용 저장", WS_CHILD | BS_AUTOCHECKBOX, 185, 40, 250, 24, hwnd, (HMENU)(INT_PTR)ID_SET_CHK_SAVE_INP, hInst, nullptr);
        AddToPanel(3, hChkSaveInp);
        HWND hChkAutoQuick = CreateWindowExW(0, L"BUTTON", L"프로그램 실행 시 빠른 연결 띄우기", WS_CHILD | BS_AUTOCHECKBOX, 185, 110, 250, 24, hwnd, (HMENU)(INT_PTR)ID_SET_CHK_AUTO_QUICK, hInst, nullptr);
        AddToPanel(3, hChkAutoQuick);
        HWND hChkAutoAddr = CreateWindowExW(0, L"BUTTON", L"프로그램 실행 시 주소록 띄우기", WS_CHILD | BS_AUTOCHECKBOX, 185, 145, 250, 24, hwnd, (HMENU)(INT_PTR)ID_SET_CHK_AUTO_ADDR, hInst, nullptr);
        AddToPanel(3, hChkAutoAddr);
        HWND hChkBackspace = CreateWindowExW(0, L"BUTTON", L"Backspace 키로 삭제를 현재 행으로 제한", WS_CHILD | BS_AUTOCHECKBOX, 185, 180, 300, 24, hwnd, (HMENU)(INT_PTR)ID_SET_CHK_BACKSPACE_LIMIT, hInst, nullptr);
        AddToPanel(3, hChkBackspace);
        HWND hChkTray = CreateWindowExW(0, L"BUTTON", L"종료(X) 버튼 클릭 시 시스템 트레이로 숨기기", WS_CHILD | BS_AUTOCHECKBOX, 185, 215, 300, 24, hwnd, (HMENU)(INT_PTR)ID_SET_CHK_CLOSE_TRAY, hInst, nullptr);
        AddToPanel(3, hChkTray);
        // ★ 소리 옵션 체크박스 추가 (y좌표 250으로 배치)
        HWND hChkSound = CreateWindowExW(0, L"BUTTON", L"모든 소리 효과(Beep) 사용", WS_CHILD | BS_AUTOCHECKBOX, 185, 250, 300, 24, hwnd, (HMENU)(INT_PTR)ID_SET_CHK_SOUND_ENABLE, hInst, nullptr);
        AddToPanel(3, hChkSound);
        // ★ 모호한 동아시아 문자 폭 옵션 (소리 체크박스 바로 아래에 배치)
        HWND hChkAmbiguous = CreateWindowExW(0, L"BUTTON", L"모호한 동아시아 문자 폭을 넓게 처리", WS_CHILD | BS_AUTOCHECKBOX | WS_VISIBLE, 185, 280, 300, 24, hwnd, (HMENU)(INT_PTR)ID_SET_CHK_AMBIGUOUS_WIDE, hInst, nullptr);
        AddToPanel(3, hChkAmbiguous);
        HWND hChkMainTopmost = CreateWindowExW(0, L"BUTTON", L"메인창 항상 위", WS_CHILD | BS_AUTOCHECKBOX, 185, 310, 300, 24, hwnd, (HMENU)(INT_PTR)ID_SET_CHK_MAIN_TOPMOST, hInst, nullptr);
        AddToPanel(3, hChkMainTopmost);
        HWND hChkTailSnap = CreateWindowExW(0, L"BUTTON", L"갈무리창 자동 붙기", WS_CHILD | BS_AUTOCHECKBOX, 185, 340, 300, 24, hwnd, (HMENU)(INT_PTR)ID_SET_CHK_TAIL_SNAP, hInst, nullptr);
        AddToPanel(3, hChkTailSnap);

        // --- 패널 4: 단축버튼 ---
        AddToPanel(4, CreateWindowExW(0, L"STATIC", L"라벨", WS_CHILD, 210, 30, 70, 20, hwnd, nullptr, hInst, nullptr));
        AddToPanel(4, CreateWindowExW(0, L"STATIC", L"켜기(ON) 명령", WS_CHILD, 285, 30, 100, 20, hwnd, nullptr, hInst, nullptr));
        AddToPanel(4, CreateWindowExW(0, L"STATIC", L"끄기(OFF)", WS_CHILD, 425, 30, 100, 20, hwnd, nullptr, hInst, nullptr));
        AddToPanel(4, CreateWindowExW(0, L"STATIC", L"토글", WS_CHILD, 545, 30, 30, 20, hwnd, nullptr, hInst, nullptr));

        for (int i = 0; i < 10; ++i) {
            int y = 45 + (i * 32);
            wchar_t numStr[8]; wsprintfW(numStr, L"%d", (i + 1) % 10);
            AddToPanel(4, CreateWindowExW(0, L"STATIC", numStr, WS_CHILD, 185, y + 4, 20, 20, hwnd, nullptr, hInst, nullptr));
            AddToPanel(4, CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_app->shortcutLabels[i].c_str(), WS_CHILD | ES_AUTOHSCROLL, 210, y, 70, 26, hwnd, (HMENU)(INT_PTR)(ID_SET_EDIT_SCLABEL_BASE + i), hInst, nullptr));
            AddToPanel(4, CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_app->shortcutCommands[i].c_str(), WS_CHILD | ES_AUTOHSCROLL, 285, y, 135, 26, hwnd, (HMENU)(INT_PTR)(ID_SET_EDIT_SHORTCUT_BASE + i), hInst, nullptr));
            AddToPanel(4, CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_app->shortcutOffCommands[i].c_str(), WS_CHILD | ES_AUTOHSCROLL, 425, y, 115, 26, hwnd, (HMENU)(INT_PTR)(ID_SHORTCUT_EDITOR_OFF_BASE + i), hInst, nullptr));
            HWND hChk = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | BS_AUTOCHECKBOX, 550, y + 3, 20, 20, hwnd, (HMENU)(INT_PTR)(ID_SHORTCUT_EDITOR_TOGGLE_BASE + i), hInst, nullptr);
            SendMessageW(hChk, BM_SETCHECK, g_app->shortcutIsToggle[i] ? BST_CHECKED : BST_UNCHECKED, 0);
            AddToPanel(4, hChk);
        }

        AddToPanel(4, CreateWindowExW(0, L"STATIC", L"단축 버튼은 Alt+1 ~ Alt+0 키로 사용할 수 있습니다.", WS_CHILD, 210, 380, 360, 18, hwnd, nullptr, hInst, nullptr));
        AddToPanel(4, CreateWindowExW(0, L"STATIC", L"토글이 체크된 버튼은 ON / OFF 명령을 각각 전송합니다.", WS_CHILD, 210, 402, 360, 18, hwnd, nullptr, hInst, nullptr));
        AddToPanel(4, CreateWindowExW(0, L"STATIC", L"라벨은 자유롭게 입력할 수 있으며, 번호를 함께 적으면", WS_CHILD, 210, 424, 360, 18, hwnd, nullptr, hInst, nullptr));
        AddToPanel(4, CreateWindowExW(0, L"STATIC", L"구분하기 쉽습니다.", WS_CHILD, 210, 446, 360, 18, hwnd, nullptr, hInst, nullptr));

        // 초기값 설정
        wchar_t b[32];
        wsprintfW(b, L"%d", g_app->screenCols); SetWindowTextW(hEditCols, b);
        wsprintfW(b, L"%d", g_app->screenRows); SetWindowTextW(hEditRows, b);
        wsprintfW(b, L"%d", g_app->termMarginLeft); SetWindowTextW(hMarginL, b);
        wsprintfW(b, L"%d", g_app->termMarginRight); SetWindowTextW(hMarginR, b);
        wsprintfW(b, L"%d", g_app->termMarginTop); SetWindowTextW(hMarginT, b);
        wsprintfW(b, L"%d", g_app->termMarginBottom); SetWindowTextW(hMarginB, b);
        SendMessageW(hChkSmooth, BM_SETCHECK, g_app->smoothFontEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(hChkKA, BM_SETCHECK, g_app->keepAliveEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        wsprintfW(b, L"%d", g_app->keepAliveIntervalSec); SetWindowTextW(hEditKAInt, b);
        SetWindowTextW(hEditKACmd, g_app->keepAliveCommand.c_str());
        SendMessageW(hChkSaveInp, BM_SETCHECK, g_app->saveInputOnExit ? BST_CHECKED : BST_UNCHECKED, 0);

        SendMessageW(hChkAutoQuick, BM_SETCHECK, g_app->autoShowQuickConnect ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(hChkAutoAddr, BM_SETCHECK, g_app->autoShowAddressBook ? BST_CHECKED : BST_UNCHECKED, 0);

        SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_BACKSPACE_LIMIT), BM_SETCHECK, g_app->limitBackspaceToCurrentRow ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_CLOSE_TRAY), BM_SETCHECK, g_app->closeToTray ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_SOUND_ENABLE), BM_SETCHECK, g_app->soundEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(hChkAmbiguous, BM_SETCHECK, g_app->ambiguousEastAsianWide ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_MAIN_TOPMOST), BM_SETCHECK, g_app->mainAlwaysOnTop ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_TAIL_SNAP), BM_SETCHECK, g_app->tailSnapEnabled ? BST_CHECKED : BST_UNCHECKED, 0);

        SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_AUTOLOGIN), BM_SETCHECK, g_app->autoLoginEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        SetWindowTextW(GetDlgItem(hwnd, ID_SET_EDIT_AL_ID_PAT), g_app->autoLoginIdPattern.c_str());
        SetWindowTextW(GetDlgItem(hwnd, ID_SET_EDIT_AL_ID), g_app->autoLoginId.c_str());
        SetWindowTextW(GetDlgItem(hwnd, ID_SET_EDIT_AL_PW_PAT), g_app->autoLoginPwPattern.c_str());
        SetWindowTextW(GetDlgItem(hwnd, ID_SET_EDIT_AL_PW), g_app->autoLoginPw.c_str());

        UpdateSettingPreviews(hwnd);

        CreateWindowExW(0, L"BUTTON", L"확인(&O)", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 308, 500, 85, 30, hwnd, (HMENU)(UINT_PTR)IDOK, hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"취소(&C)", WS_CHILD | WS_VISIBLE, 400, 500, 85, 30, hwnd, (HMENU)(UINT_PTR)IDCANCEL, hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"적용(&A)", WS_CHILD | WS_VISIBLE, 492, 500, 85, 30, hwnd, (HMENU)(UINT_PTR)ID_SET_BTN_APPLY, hInst, nullptr);

        EnumChildWindows(hwnd, [](HWND c, LPARAM f) { SendMessage(c, WM_SETFONT, f, TRUE); return TRUE; }, (LPARAM)hFont);
        SwitchSettingsPane(state, 0);
        return 0;
    }

    // ★★★ ALT 단축키 처리 추가 ★★★
    case WM_SYSCHAR:
    {
        wchar_t ch = towlower((wchar_t)wParam);
        if (ch == 'o') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0); return 0; }           // 확인
        if (ch == 'c') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0); return 0; }       // 취소
        if (ch == 'a') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_SET_BTN_APPLY, BN_CLICKED), 0); return 0; } // 적용
        break;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        int id = GetDlgCtrlID(hCtrl);

        if (id == ID_SET_PREVIEW_LOG_TEXT) { SetBkColor(hdc, g_app->logStyle.textColor); return (INT_PTR)CreateSolidBrush(g_app->logStyle.textColor); }
        if (id == ID_SET_PREVIEW_LOG_BACK) { SetBkColor(hdc, g_app->logStyle.backColor); return (INT_PTR)CreateSolidBrush(g_app->logStyle.backColor); }
        if (id == ID_SET_PREVIEW_INP_TEXT) { SetBkColor(hdc, g_app->inputStyle.textColor); return (INT_PTR)CreateSolidBrush(g_app->inputStyle.textColor); }
        if (id == ID_SET_PREVIEW_INP_BACK) { SetBkColor(hdc, g_app->inputStyle.backColor); return (INT_PTR)CreateSolidBrush(g_app->inputStyle.backColor); }
        if (id == ID_SET_PREVIEW_CHAT_TEXT) { SetBkColor(hdc, g_app->chatStyle.textColor); return (INT_PTR)CreateSolidBrush(g_app->chatStyle.textColor); }
        if (id == ID_SET_PREVIEW_CHAT_BACK) { SetBkColor(hdc, g_app->chatStyle.backColor); return (INT_PTR)CreateSolidBrush(g_app->chatStyle.backColor); }

        SetBkMode(hdc, TRANSPARENT);
        return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_MEASUREITEM:
    {
        MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*)lParam;
        if (mis && mis->CtlID == ID_SETTING_LIST)
        {
            mis->itemHeight = 40;
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis && dis->CtlID == ID_SETTING_LIST)
        {
            wchar_t text[128] = {};
            SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, (LPARAM)text);

            COLORREF bg = (dis->itemState & ODS_SELECTED) ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_WINDOW);
            COLORREF fg = (dis->itemState & ODS_SELECTED) ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_WINDOWTEXT);

            HBRUSH hbr = CreateSolidBrush(bg);
            FillRect(dis->hDC, &dis->rcItem, hbr);
            DeleteObject(hbr);

            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, fg);

            HFONT hFont = GetPopupUIFont(hwnd);
            HFONT hOld = (HFONT)SelectObject(dis->hDC, hFont);

            RECT rc = dis->rcItem;
            rc.left += 8;
            DrawTextW(dis->hDC, text, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

            SelectObject(dis->hDC, hOld);

            if (dis->itemState & ODS_FOCUS)
                DrawFocusRect(dis->hDC, &dis->rcItem);

            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);

        if (id == ID_SET_CHK_AUTO_QUICK && HIWORD(wParam) == BN_CLICKED) {
            if (SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_AUTO_QUICK), BM_GETCHECK, 0, 0) == BST_CHECKED)
                SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_AUTO_ADDR), BM_SETCHECK, BST_UNCHECKED, 0);
        }
        else if (id == ID_SET_CHK_AUTO_ADDR && HIWORD(wParam) == BN_CLICKED) {
            if (SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_AUTO_ADDR), BM_GETCHECK, 0, 0) == BST_CHECKED)
                SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_AUTO_QUICK), BM_SETCHECK, BST_UNCHECKED, 0);
        }
        if (id == ID_SETTING_LIST && HIWORD(wParam) == LBN_SELCHANGE) {
            SwitchSettingsPane(state, (int)SendMessage(state->hwndList, LB_GETCURSEL, 0, 0));
        }
        else if (id == ID_SET_BTN_LOG_FONT) {
            if (ChooseFontOnly(hwnd, g_app->logStyle.font)) { ApplyStyles(); UpdateSettingPreviews(hwnd); }
        }
        else if (id == ID_SET_BTN_LOG_COLOR) {
            if (ChooseColorOnly(hwnd, g_app->logStyle.textColor)) { ApplyStyles(); UpdateSettingPreviews(hwnd); }
        }
        else if (id == ID_SET_BTN_MAIN_BG) {
            if (ChooseBackgroundColor(hwnd, g_app->logStyle.backColor)) { ApplyStyles(); UpdateSettingPreviews(hwnd); }
        }
        else if (id == ID_SET_BTN_INP_FONT) {
            if (ChooseFontOnly(hwnd, g_app->inputStyle.font)) { ApplyStyles(); UpdateSettingPreviews(hwnd); }
        }
        else if (id == ID_SET_BTN_INP_COLOR) {
            if (ChooseColorOnly(hwnd, g_app->inputStyle.textColor)) { ApplyStyles(); UpdateSettingPreviews(hwnd); }
        }
        else if (id == ID_SET_BTN_INP_BG) {
            if (ChooseBackgroundColor(hwnd, g_app->inputStyle.backColor)) {
                ApplyStyles();
                if (g_app->hwndInput) { InvalidateRect(g_app->hwndInput, nullptr, TRUE); UpdateWindow(g_app->hwndInput); }
            }
        }
        else if (id == ID_SET_CHK_AMBIGUOUS_WIDE && HIWORD(wParam) == BN_CLICKED) {
            g_app->ambiguousEastAsianWide = (SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_AMBIGUOUS_WIDE), BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (g_app->hwndLog) {
                InvalidateRect(g_app->hwndLog, nullptr, TRUE);
                UpdateWindow(g_app->hwndLog);   // 즉시 다시 그리기
            }
        }
        else if (id == IDOK || id == ID_SET_BTN_APPLY) {
            wchar_t buf[1024];

            GetWindowTextW(GetDlgItem(hwnd, ID_SET_EDIT_COLS), buf, 256);
            int newCols = _wtoi(buf);
            GetWindowTextW(GetDlgItem(hwnd, ID_SET_EDIT_ROWS), buf, 256);
            int newRows = _wtoi(buf);
            if (newCols >= 20 && newCols <= 300) g_app->screenCols = newCols;
            if (newRows >= 5 && newRows <= 200) g_app->screenRows = newRows;

            GetWindowTextW(GetDlgItem(hwnd, ID_SET_MARGIN_LEFT), buf, 256);
            g_app->termMarginLeft = min(200, max(0, _wtoi(buf)));
            GetWindowTextW(GetDlgItem(hwnd, ID_SET_MARGIN_RIGHT), buf, 256);
            g_app->termMarginRight = min(200, max(0, _wtoi(buf)));
            GetWindowTextW(GetDlgItem(hwnd, ID_SET_MARGIN_TOP), buf, 256);
            g_app->termMarginTop = min(200, max(0, _wtoi(buf)));
            GetWindowTextW(GetDlgItem(hwnd, ID_SET_MARGIN_BOTTOM), buf, 256);
            g_app->termMarginBottom = min(200, max(0, _wtoi(buf)));

            g_app->chatDockedLines = 0;
            g_app->chatTimestampEnabled = false;

            g_app->autoLoginEnabled = (SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_AUTOLOGIN), BM_GETCHECK, 0, 0) == BST_CHECKED);
            GetWindowTextW(GetDlgItem(hwnd, ID_SET_EDIT_AL_ID_PAT), buf, 1024); g_app->autoLoginIdPattern = Trim(buf);
            GetWindowTextW(GetDlgItem(hwnd, ID_SET_EDIT_AL_ID), buf, 1024); g_app->autoLoginId = Trim(buf);
            GetWindowTextW(GetDlgItem(hwnd, ID_SET_EDIT_AL_PW_PAT), buf, 1024); g_app->autoLoginPwPattern = Trim(buf);
            GetWindowTextW(GetDlgItem(hwnd, ID_SET_EDIT_AL_PW), buf, 1024); g_app->autoLoginPw = Trim(buf);
            g_app->autoLoginSuccessPattern1.clear();
            g_app->autoLoginSuccessPattern2.clear();
            g_app->autoLoginSuccessPattern3.clear();
            g_app->autoLoginFailPattern1.clear();
            g_app->autoLoginFailPattern2.clear();
            g_app->autoLoginFailPattern3.clear();
            SaveAutoLoginSettings();

            if (SendMessageW(GetDlgItem(hwnd, ID_SET_ALIGN_LEFT), BM_GETCHECK, 0, 0) == BST_CHECKED) g_app->termAlign = 0;
            else if (SendMessageW(GetDlgItem(hwnd, ID_SET_ALIGN_RIGHT), BM_GETCHECK, 0, 0) == BST_CHECKED) g_app->termAlign = 2;
            else g_app->termAlign = 1;

            // 1. [추가] 전용 폰트(Mud둥근모) 사용 여부 체크박스 값 읽기
            g_app->useCustomMudFont = (SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_USE_MUD_FONT), BM_GETCHECK, 0, 0) == BST_CHECKED);

            // 2. 폰트 부드럽게 표시 설정
            g_app->smoothFontEnabled = (SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_SMOOTH), BM_GETCHECK, 0, 0) == BST_CHECKED);

            // 3. [추가] 폰트 및 렌더링 설정 파일에 저장
            SaveFontRenderSettings();
            ApplyStyles();

            g_app->keepAliveEnabled = (SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_KA_ENABLE), BM_GETCHECK, 0, 0) == BST_CHECKED);
            GetWindowTextW(GetDlgItem(hwnd, ID_SET_EDIT_KA_INT), buf, 256);
            g_app->keepAliveIntervalSec = max(5, _wtoi(buf));
            GetWindowTextW(GetDlgItem(hwnd, ID_SET_EDIT_KA_CMD), buf, 1024);
            g_app->keepAliveCommand = Trim(buf);
            ApplyKeepAliveTimer(g_app->hwndMain);

            g_app->saveInputOnExit = (SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_SAVE_INP), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_app->limitBackspaceToCurrentRow = (SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_BACKSPACE_LIMIT), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_app->closeToTray = (SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_CLOSE_TRAY), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_app->soundEnabled = (SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_SOUND_ENABLE), BM_GETCHECK, 0, 0) == BST_CHECKED);
            // ★ 새로 추가: 모호한 동아시아 문자 폭 옵션
            g_app->ambiguousEastAsianWide = (SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_AMBIGUOUS_WIDE), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_app->mainAlwaysOnTop = (SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_MAIN_TOPMOST), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_app->tailSnapEnabled = (SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_TAIL_SNAP), BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (g_app->hwndMain)
            {
                SetWindowPos(g_app->hwndMain, g_app->mainAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                    0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            SaveScreenSizeSettings();
            SaveGeneralSettings();
            g_app->autoShowQuickConnect = (SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_AUTO_QUICK), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_app->autoShowAddressBook = (SendMessageW(GetDlgItem(hwnd, ID_SET_CHK_AUTO_ADDR), BM_GETCHECK, 0, 0) == BST_CHECKED);

            // buildfix33: 환경설정 적용은 갈무리 켜짐/꺼짐 상태를 바꾸지 않습니다.
            // 갈무리는 보기 → 갈무리 → 갈무리 켜짐/꺼짐 메뉴에서만 제어합니다.

            for (int i = 0; i < 10; ++i) {
                GetWindowTextW(GetDlgItem(hwnd, ID_SET_EDIT_SCLABEL_BASE + i), buf, 256);
                g_app->shortcutLabels[i] = Trim(buf).empty() ? std::to_wstring((i + 1) % 10) : Trim(buf);

                GetWindowTextW(GetDlgItem(hwnd, ID_SET_EDIT_SHORTCUT_BASE + i), buf, 1024);
                g_app->shortcutCommands[i] = Trim(buf);

                GetWindowTextW(GetDlgItem(hwnd, ID_SHORTCUT_EDITOR_OFF_BASE + i), buf, 1024);
                g_app->shortcutOffCommands[i] = Trim(buf);

                bool isToggleNow = (SendMessageW(GetDlgItem(hwnd, ID_SHORTCUT_EDITOR_TOGGLE_BASE + i), BM_GETCHECK, 0, 0) == BST_CHECKED);
                g_app->shortcutIsToggle[i] = isToggleNow;

                if (!isToggleNow) {
                    g_app->shortcutActive[i] = false;
                }
            }

            SaveShortcutSettings();
            ApplyShortcutButtons(g_app->hwndMain);

            if (g_app->termBuffer) g_app->termBuffer->Resize(g_app->screenCols, g_app->screenRows);
            if (g_app->hwndMain) {
                // utils.cpp에 있는 함수 호출
                // FitWindowToScreenGrid(g_app->hwndMain, g_app->screenCols, g_app->screenRows, false);
                PostMessageW(g_app->hwndMain, WM_SIZE, 0, 0); // 대신 WM_SIZE를 발생시켜 자연스럽게 재배치 유도
            }
            if (g_app->proc.hPC) ResizePseudoConsoleToLogWindow();
            if (g_app && g_app->hwndLog) InvalidateRect(g_app->hwndLog, nullptr, TRUE);

            if (id == IDOK) {
                DestroyWindow(hwnd);
            }
        }
        else if (id == IDCANCEL) {
            DestroyWindow(hwnd);
        }
        return 0;
    }

    case WM_DESTROY:
        RemovePropW(hwnd, L"SettingsState");
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowSettingsDialog(HWND owner)
{
    static const wchar_t* kClass = L"TTGuiSettingsClass";
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = SettingsDialogProc;
        wc.hInstance = GetModuleHandle(0);
        wc.lpszClassName = kClass;
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    SettingsDlgState state;
    RECT rc; GetWindowRect(owner, &rc);
    int w = 600, h = 580;
    int x = rc.left + (rc.right - rc.left - w) / 2;
    int y = rc.top + (rc.bottom - rc.top - h) / 2;

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"환경 설정",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h, owner, nullptr, GetModuleHandle(0), &state);

    if (!hwnd) return;
    EnableWindow(owner, FALSE);
    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
}

// ==============================================
// 4. 기타 설정 파일 로드 및 세이브
// ==============================================

void LoadKeepAliveSettings()
{
    if (!g_app) return;
    std::wstring path = GetSettingsPath();
    wchar_t buf[256] = {};

    g_app->keepAliveEnabled = GetPrivateProfileIntW(L"keepalive", L"enabled", 0, path.c_str()) ? true : false;
    g_app->keepAliveIntervalSec = GetPrivateProfileIntW(L"keepalive", L"interval", 60, path.c_str());

    if (g_app->keepAliveIntervalSec < 5)
        g_app->keepAliveIntervalSec = 5;

    GetPrivateProfileStringW(L"keepalive", L"command", L"", buf, 256, path.c_str());
    g_app->keepAliveCommand = buf;
}

void SaveKeepAliveSettings()
{
    if (!g_app) return;
    std::wstring path = GetSettingsPath();
    wchar_t buf[64] = {};

    WritePrivateProfileStringW(L"keepalive", L"enabled", g_app->keepAliveEnabled ? L"1" : L"0", path.c_str());
    wsprintfW(buf, L"%d", g_app->keepAliveIntervalSec);
    WritePrivateProfileStringW(L"keepalive", L"interval", buf, path.c_str());
    WritePrivateProfileStringW(L"keepalive", L"command", g_app->keepAliveCommand.c_str(), path.c_str());
}

static LRESULT CALLBACK KeepAlivePopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    KeepAliveDialogState* state = (KeepAliveDialogState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return TRUE;
    }
    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDOK:
            if (state && state->enabled && state->intervalSec && state->command)
            {
                wchar_t intervalBuf[64] = {};
                wchar_t commandBuf[1024] = {};

                GetWindowTextW(GetDlgItem(hwnd, ID_KEEPALIVE_INTERVAL), intervalBuf, 64);
                GetWindowTextW(GetDlgItem(hwnd, ID_KEEPALIVE_COMMAND), commandBuf, 1024);

                int newInterval = _wtoi(intervalBuf);
                if (newInterval < 5) newInterval = 5;
                std::wstring newCommand = Trim(commandBuf);

                *state->enabled = (SendMessageW(GetDlgItem(hwnd, ID_KEEPALIVE_ENABLE), BM_GETCHECK, 0, 0) == BST_CHECKED);
                *state->intervalSec = newInterval;
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
    }
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

bool PromptKeepAliveSettings(HWND hwnd, bool& enabled, int& intervalSec, std::wstring& command)
{
    const wchar_t kDlgClass[] = L"TTGuiKeepAlivePopupClass";
    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = KeepAlivePopupProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kDlgClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        s_registered = true;
    }

    KeepAliveDialogState state;
    state.enabled = &enabled;
    state.intervalSec = &intervalSec;
    state.command = &command;
    state.accepted = false;

    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kDlgClass, L"접속 유지 설정",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 360, 210, hwnd, nullptr, GetModuleHandleW(nullptr), &state);

    if (!hDlg) return false;

    CreateWindowExW(0, L"BUTTON", L"접속 유지 사용", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        16, 16, 140, 22, hDlg, (HMENU)(INT_PTR)ID_KEEPALIVE_ENABLE, GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, L"STATIC", L"시간(초):", WS_CHILD | WS_VISIBLE,
        16, 52, 80, 20, hDlg, (HMENU)(INT_PTR)ID_KEEPALIVE_LABEL_INTERVAL, GetModuleHandleW(nullptr), nullptr);
    HWND hEditInterval = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
        100, 48, 80, 24, hDlg, (HMENU)(INT_PTR)ID_KEEPALIVE_INTERVAL, GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, L"STATIC", L"전송 명령:", WS_CHILD | WS_VISIBLE,
        16, 88, 80, 20, hDlg, (HMENU)(INT_PTR)ID_KEEPALIVE_LABEL_COMMAND, GetModuleHandleW(nullptr), nullptr);
    HWND hEditCommand = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        100, 84, 220, 24, hDlg, (HMENU)(INT_PTR)ID_KEEPALIVE_COMMAND, GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, L"BUTTON", L"확인", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        156, 130, 80, 28, hDlg, (HMENU)(INT_PTR)IDOK, GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        244, 130, 80, 28, hDlg, (HMENU)(INT_PTR)IDCANCEL, GetModuleHandleW(nullptr), nullptr);

    SendMessageW(GetDlgItem(hDlg, ID_KEEPALIVE_ENABLE), BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);

    wchar_t buf[64]; wsprintfW(buf, L"%d", intervalSec);
    SetWindowTextW(hEditInterval, buf);
    SetWindowTextW(hEditCommand, command.c_str());

    HFONT hFont = GetPopupUIFont(hDlg);
    SendMessageW(hDlg, WM_SETFONT, (WPARAM)hFont, TRUE);
    EnumChildWindows(hDlg, [](HWND child, LPARAM lParam) -> BOOL { SendMessageW(child, WM_SETFONT, lParam, TRUE); return TRUE; }, (LPARAM)hFont);

    RECT rcOwner{}, rcDlg{};
    GetWindowRect(hwnd, &rcOwner);
    GetWindowRect(hDlg, &rcDlg);
    int dlgW = rcDlg.right - rcDlg.left; int dlgH = rcDlg.bottom - rcDlg.top;
    int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - dlgW) / 2;
    int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - dlgH) / 2;
    SetWindowPos(hDlg, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);

    EnableWindow(hwnd, FALSE); SetFocus(hEditInterval);
    MSG msg; while (IsWindow(hDlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(hwnd, TRUE); SetActiveWindow(hwnd); SetForegroundWindow(hwnd);
    return state.accepted;
}

// 5. 화면 크기 관련
void LoadScreenSizeSettings()
{
    if (!g_app) return;
    std::wstring path = GetSettingsPath();
    g_app->screenCols = GetPrivateProfileIntW(L"screen_size", L"cols", 80, path.c_str());
    g_app->screenRows = GetPrivateProfileIntW(L"screen_size", L"rows", 32, path.c_str());
    if (g_app->screenCols < 20) g_app->screenCols = 20;
    if (g_app->screenCols > 300) g_app->screenCols = 300;
    if (g_app->screenRows < 5) g_app->screenRows = 5;
    if (g_app->screenRows > 200) g_app->screenRows = 200;
    g_app->termMarginLeft = GetPrivateProfileIntW(L"screen_size", L"margin_left", 0, path.c_str());
    g_app->termMarginRight = GetPrivateProfileIntW(L"screen_size", L"margin_right", 0, path.c_str());
    g_app->termMarginTop = GetPrivateProfileIntW(L"screen_size", L"margin_top", 0, path.c_str());
    g_app->termMarginBottom = GetPrivateProfileIntW(L"screen_size", L"margin_bottom", 0, path.c_str());
    g_app->termMarginLeft = min(200, max(0, g_app->termMarginLeft));
    g_app->termMarginRight = min(200, max(0, g_app->termMarginRight));
    g_app->termMarginTop = min(200, max(0, g_app->termMarginTop));
    g_app->termMarginBottom = min(200, max(0, g_app->termMarginBottom));
}

void SaveScreenSizeSettings()
{
    if (!g_app) return;
    std::wstring path = GetSettingsPath();
    wchar_t buf[32];
    wsprintfW(buf, L"%d", g_app->screenCols);
    WritePrivateProfileStringW(L"screen_size", L"cols", buf, path.c_str());
    wsprintfW(buf, L"%d", g_app->screenRows);
    WritePrivateProfileStringW(L"screen_size", L"rows", buf, path.c_str());
    wsprintfW(buf, L"%d", g_app->termMarginLeft);
    WritePrivateProfileStringW(L"screen_size", L"margin_left", buf, path.c_str());
    wsprintfW(buf, L"%d", g_app->termMarginRight);
    WritePrivateProfileStringW(L"screen_size", L"margin_right", buf, path.c_str());
    wsprintfW(buf, L"%d", g_app->termMarginTop);
    WritePrivateProfileStringW(L"screen_size", L"margin_top", buf, path.c_str());
    wsprintfW(buf, L"%d", g_app->termMarginBottom);
    WritePrivateProfileStringW(L"screen_size", L"margin_bottom", buf, path.c_str());
}

static LRESULT CALLBACK ScreenSizePopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ScreenSizePopupState* state = (ScreenSizePopupState*)GetPropW(hwnd, L"ScreenSizePopupState");
    switch (msg)
    {
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            if (state && state->cols && state->rows)
            {
                wchar_t colsBuf[64] = {}; wchar_t rowsBuf[64] = {};
                GetWindowTextW(GetDlgItem(hwnd, ID_SCREEN_SIZE_COLS), colsBuf, 64);
                GetWindowTextW(GetDlgItem(hwnd, ID_SCREEN_SIZE_ROWS), rowsBuf, 64);
                int cols = _wtoi(colsBuf); int rows = _wtoi(rowsBuf);
                if (cols < 20) cols = 20; if (cols > 300) cols = 300;
                if (rows < 5) rows = 5; if (rows > 200) rows = 200;
                *state->cols = cols; *state->rows = rows;
                state->accepted = true;
            }
            DestroyWindow(hwnd); return 0;
        case IDCANCEL: DestroyWindow(hwnd); return 0;
        }
        break;
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam; SetBkMode(hdc, TRANSPARENT); return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: RemovePropW(hwnd, L"ScreenSizePopupState"); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool PromptScreenSizeSettings(HWND hwnd, int& cols, int& rows)
{
    const wchar_t kDlgClass[] = L"TTGuiScreenSizePopupClass";
    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSW wc = {}; wc.lpfnWndProc = ScreenSizePopupProc; wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kDlgClass; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassW(&wc); s_registered = true;
    }

    ScreenSizePopupState state; state.cols = &cols; state.rows = &rows; state.accepted = false;
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kDlgClass, L"화면 크기 설정", WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 360, 180, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hDlg) return false;

    SetPropW(hDlg, L"ScreenSizePopupState", &state);
    CreateWindowExW(0, L"STATIC", L"화면 가로줄 수:", WS_CHILD | WS_VISIBLE, 20, 24, 110, 20, hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND hEditCols = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER, 140, 20, 80, 24, hDlg, (HMENU)(INT_PTR)ID_SCREEN_SIZE_COLS, GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, L"STATIC", L"화면 세로줄 수:", WS_CHILD | WS_VISIBLE, 20, 60, 110, 20, hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND hEditRows = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER, 140, 56, 80, 24, hDlg, (HMENU)(INT_PTR)ID_SCREEN_SIZE_ROWS, GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, L"BUTTON", L"확인", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 170, 105, 70, 28, hDlg, (HMENU)(INT_PTR)IDOK, GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 250, 105, 70, 28, hDlg, (HMENU)(INT_PTR)IDCANCEL, GetModuleHandleW(nullptr), nullptr);

    wchar_t buf[32]; wsprintfW(buf, L"%d", cols); SetWindowTextW(hEditCols, buf);
    wsprintfW(buf, L"%d", rows); SetWindowTextW(hEditRows, buf);

    HFONT hFont = GetPopupUIFont(hDlg);
    SendMessageW(hDlg, WM_SETFONT, (WPARAM)hFont, TRUE);
    EnumChildWindows(hDlg, [](HWND child, LPARAM lParam) -> BOOL { SendMessageW(child, WM_SETFONT, lParam, TRUE); return TRUE; }, (LPARAM)hFont);

    RECT rcOwner{}, rcDlg{}; GetWindowRect(hwnd, &rcOwner); GetWindowRect(hDlg, &rcDlg);
    int dlgW = rcDlg.right - rcDlg.left; int dlgH = rcDlg.bottom - rcDlg.top;
    int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - dlgW) / 2;
    int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - dlgH) / 2;
    SetWindowPos(hDlg, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);

    EnableWindow(hwnd, FALSE); SetFocus(hEditCols);
    MSG msg; while (IsWindow(hDlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(hwnd, TRUE); SetActiveWindow(hwnd); SetForegroundWindow(hwnd);
    return state.accepted;
}

// ==============================================
// 나머지 저장/로드 유틸 함수들
// ==============================================
void LoadWindowSettings(HWND hwnd)
{
    if (!g_app || !hwnd) return;
    std::wstring path = GetSettingsPath();
    wchar_t buf[256] = {};

    g_app->logStyle.textColor = StringToColor((GetPrivateProfileStringW(L"colors", L"log_text", L"", buf, 256, path.c_str()), buf), g_app->logStyle.textColor);
    g_app->logStyle.backColor = StringToColor((GetPrivateProfileStringW(L"colors", L"log_back", L"", buf, 256, path.c_str()), buf), g_app->logStyle.backColor);
    g_app->inputStyle.textColor = StringToColor((GetPrivateProfileStringW(L"colors", L"input_text", L"", buf, 256, path.c_str()), buf), g_app->inputStyle.textColor);
    g_app->inputStyle.backColor = StringToColor((GetPrivateProfileStringW(L"colors", L"input_back", L"", buf, 256, path.c_str()), buf), g_app->inputStyle.backColor);
    g_app->mainBackColor = StringToColor((GetPrivateProfileStringW(L"colors", L"main_back", L"", buf, 256, path.c_str()), buf), g_app->mainBackColor);

    GetPrivateProfileStringW(L"font", L"log_face", g_app->logStyle.font.lfFaceName, buf, LF_FACESIZE, path.c_str()); if (buf[0]) lstrcpynW(g_app->logStyle.font.lfFaceName, buf, LF_FACESIZE);
    g_app->logStyle.font.lfHeight = GetPrivateProfileIntW(L"font", L"log_height", g_app->logStyle.font.lfHeight, path.c_str());
    g_app->logStyle.font.lfWeight = GetPrivateProfileIntW(L"font", L"log_weight", g_app->logStyle.font.lfWeight, path.c_str());

    GetPrivateProfileStringW(L"font", L"input_face", g_app->inputStyle.font.lfFaceName, buf, LF_FACESIZE, path.c_str()); if (buf[0]) lstrcpynW(g_app->inputStyle.font.lfFaceName, buf, LF_FACESIZE);
    g_app->inputStyle.font.lfHeight = GetPrivateProfileIntW(L"font", L"input_height", g_app->inputStyle.font.lfHeight, path.c_str());
    g_app->inputStyle.font.lfWeight = GetPrivateProfileIntW(L"font", L"input_weight", g_app->inputStyle.font.lfWeight, path.c_str());

    g_app->chatStyle.textColor = StringToColor((GetPrivateProfileStringW(L"colors", L"chat_text", L"", buf, 256, path.c_str()), buf), g_app->logStyle.textColor);
    g_app->chatStyle.backColor = StringToColor((GetPrivateProfileStringW(L"colors", L"chat_back", L"", buf, 256, path.c_str()), buf), g_app->logStyle.backColor);

    GetPrivateProfileStringW(L"font", L"chat_face", g_app->logStyle.font.lfFaceName, buf, LF_FACESIZE, path.c_str()); if (buf[0]) lstrcpynW(g_app->chatStyle.font.lfFaceName, buf, LF_FACESIZE);
    g_app->chatStyle.font.lfHeight = GetPrivateProfileIntW(L"font", L"chat_height", g_app->logStyle.font.lfHeight, path.c_str());
    g_app->chatStyle.font.lfWeight = GetPrivateProfileIntW(L"font", L"chat_weight", g_app->logStyle.font.lfWeight, path.c_str());

    int x = GetPrivateProfileIntW(L"window", L"x", -1, path.c_str());
    int y = GetPrivateProfileIntW(L"window", L"y", -1, path.c_str());
    int w = GetPrivateProfileIntW(L"window", L"w", 800, path.c_str());
    int h = GetPrivateProfileIntW(L"window", L"h", 600, path.c_str());
    int maximized = GetPrivateProfileIntW(L"window", L"maximized", 0, path.c_str());

    int chatDocked = GetPrivateProfileIntW(L"window", L"chat_docked", 1, path.c_str());
    int chatVisible = GetPrivateProfileIntW(L"window", L"chat_visible", 0, path.c_str());
    if (chatDocked == 0) { chatDocked = 1; chatVisible = 0; }

    g_app->chatDocked = (chatDocked != 0);
    g_app->chatVisible = (chatVisible != 0);

    GetPrivateProfileStringW(L"window", L"chat_float_rect", L"", buf, 256, path.c_str());
    if (buf[0]) swscanf_s(buf, L"%ld,%ld,%ld,%ld", &g_app->chatFloatRect.left, &g_app->chatFloatRect.top, &g_app->chatFloatRect.right, &g_app->chatFloatRect.bottom);

    g_app->termAlign = GetPrivateProfileIntW(L"window", L"align", 0, path.c_str());
    if (g_app->termAlign < 0 || g_app->termAlign > 2) g_app->termAlign = 0;

    // 안전판 기본값: 실행 직후에는 상단 메뉴를 반드시 보이게 합니다.
    // 우클릭 메뉴의 "상단 메뉴 보이기"도 함께 동작합니다.
    g_app->menuHidden = false;

    g_app->autoShowQuickConnect = GetPrivateProfileIntW(L"startup", L"quick_connect", 0, path.c_str()) != 0;
    g_app->autoShowAddressBook = GetPrivateProfileIntW(L"startup", L"address_book", 0, path.c_str()) != 0;
    g_app->closeToTray = GetPrivateProfileIntW(L"window", L"close_to_tray", 0, path.c_str()) != 0;

    if (w < 300) w = 300; if (h < 200) h = 200;
    if (x >= 0 && y >= 0 && x > -32000 && y > -32000) SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    else {
        RECT rcWork{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
        int screenW = rcWork.right - rcWork.left; int screenH = rcWork.bottom - rcWork.top;
        SetWindowPos(hwnd, nullptr, rcWork.left + (screenW - w) / 2, rcWork.top + (screenH - h) / 2, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    GetPrivateProfileStringW(L"recent", L"last_connect", L"", buf, 256, path.c_str());
    g_app->lastConnectCommand = buf;

    g_app->statusPartCount = GetPrivateProfileIntW(L"statusbar", L"count", 1, path.c_str());

    // ★ 크래시 방지 안전벨트 (무조건 1~5 사이로 고정)
    if (g_app->statusPartCount < 1) g_app->statusPartCount = 1;
    if (g_app->statusPartCount > 5) g_app->statusPartCount = 5;

    for (int i = 0; i < 5; ++i) {
        wchar_t keyF[32]; wsprintfW(keyF, L"format_%d", i);
        wchar_t fbuf[256] = {}; GetPrivateProfileStringW(L"statusbar", keyF, L"", fbuf, 256, path.c_str());
        g_app->statusFormats[i] = fbuf;

        wchar_t keyA[32]; wsprintfW(keyA, L"align_%d", i);
        g_app->statusAligns[i] = GetPrivateProfileIntW(L"statusbar", keyA, 0, path.c_str());
    }

    // 상태바 설정이 비어 있으면 실제로 보이지 않는 것처럼 보이므로 기본 문구를 넣습니다.
    if (g_app->statusFormats[0].empty())
        g_app->statusFormats[0] = L"KTin 준비";

    if (maximized) ShowWindow(hwnd, SW_MAXIMIZE);
}

void SaveWindowSettings(HWND hwnd)
{
    if (!g_app || !hwnd) return;
    std::wstring path = GetSettingsPath();

    WINDOWPLACEMENT wp = {}; wp.length = sizeof(wp);
    GetWindowPlacement(hwnd, &wp);
    if (wp.showCmd == SW_SHOWMINIMIZED) return;
    if (IsIconic(hwnd)) return;

    RECT rc = {};
    if (IsZoomed(hwnd)) rc = wp.rcNormalPosition;
    else GetWindowRect(hwnd, &rc);

    wchar_t buf[64] = {};
    wsprintfW(buf, L"%ld", rc.left); WritePrivateProfileStringW(L"window", L"x", buf, path.c_str());
    wsprintfW(buf, L"%ld", rc.top); WritePrivateProfileStringW(L"window", L"y", buf, path.c_str());
    wsprintfW(buf, L"%ld", rc.right - rc.left); WritePrivateProfileStringW(L"window", L"w", buf, path.c_str());
    wsprintfW(buf, L"%ld", rc.bottom - rc.top); WritePrivateProfileStringW(L"window", L"h", buf, path.c_str());

    wsprintfW(buf, L"%d", (wp.showCmd == SW_MAXIMIZE) ? 1 : 0);
    WritePrivateProfileStringW(L"window", L"maximized", buf, path.c_str());

    wsprintfW(buf, L"%d", g_app->chatDocked ? 1 : 0); WritePrivateProfileStringW(L"window", L"chat_docked", buf, path.c_str());
    wsprintfW(buf, L"%d", g_app->chatVisible ? 1 : 0); WritePrivateProfileStringW(L"window", L"chat_visible", buf, path.c_str());

    if (g_app->hwndChatFloat && !g_app->chatDocked) GetWindowRect(g_app->hwndChatFloat, &g_app->chatFloatRect);
    wsprintfW(buf, L"%ld,%ld,%ld,%ld", g_app->chatFloatRect.left, g_app->chatFloatRect.top, g_app->chatFloatRect.right, g_app->chatFloatRect.bottom);
    WritePrivateProfileStringW(L"window", L"chat_float_rect", buf, path.c_str());

    wsprintfW(buf, L"%d", g_app->termAlign); WritePrivateProfileStringW(L"window", L"align", buf, path.c_str());

    WritePrivateProfileStringW(L"startup", L"quick_connect", g_app->autoShowQuickConnect ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"startup", L"address_book", g_app->autoShowAddressBook ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"window", L"close_to_tray", g_app->closeToTray ? L"1" : L"0", path.c_str());

    WritePrivateProfileStringW(L"colors", L"log_text,unused", nullptr, path.c_str());
    WritePrivateProfileStringW(L"colors", L"log_text", ColorToString(g_app->logStyle.textColor).c_str(), path.c_str());
    WritePrivateProfileStringW(L"colors", L"log_back", ColorToString(g_app->logStyle.backColor).c_str(), path.c_str());
    WritePrivateProfileStringW(L"colors", L"input_text", ColorToString(g_app->inputStyle.textColor).c_str(), path.c_str());
    WritePrivateProfileStringW(L"colors", L"input_back", ColorToString(g_app->inputStyle.backColor).c_str(), path.c_str());
    WritePrivateProfileStringW(L"colors", L"main_back", ColorToString(g_app->mainBackColor).c_str(), path.c_str());

    WritePrivateProfileStringW(L"colors", L"chat_text", ColorToString(g_app->chatStyle.textColor).c_str(), path.c_str());
    WritePrivateProfileStringW(L"colors", L"chat_back", ColorToString(g_app->chatStyle.backColor).c_str(), path.c_str());

    WritePrivateProfileStringW(L"font", L"chat_face", g_app->chatStyle.font.lfFaceName, path.c_str());
    wsprintfW(buf, L"%ld", g_app->chatStyle.font.lfHeight); WritePrivateProfileStringW(L"font", L"chat_height", buf, path.c_str());
    wsprintfW(buf, L"%ld", g_app->chatStyle.font.lfWeight); WritePrivateProfileStringW(L"font", L"chat_weight", buf, path.c_str());

    WritePrivateProfileStringW(L"font", L"log_face", g_app->logStyle.font.lfFaceName, path.c_str());
    wsprintfW(buf, L"%ld", g_app->logStyle.font.lfHeight); WritePrivateProfileStringW(L"font", L"log_height", buf, path.c_str());
    wsprintfW(buf, L"%ld", g_app->logStyle.font.lfWeight); WritePrivateProfileStringW(L"font", L"log_weight", buf, path.c_str());

    WritePrivateProfileStringW(L"font", L"input_face", g_app->inputStyle.font.lfFaceName, path.c_str());
    wsprintfW(buf, L"%ld", g_app->inputStyle.font.lfHeight); WritePrivateProfileStringW(L"font", L"input_height", buf, path.c_str());
    wsprintfW(buf, L"%ld", g_app->inputStyle.font.lfWeight); WritePrivateProfileStringW(L"font", L"input_weight", buf, path.c_str());

    wsprintfW(buf, L"%d", g_app->statusPartCount);
    WritePrivateProfileStringW(L"statusbar", L"count", buf, path.c_str());
    for (int i = 0; i < 5; ++i) {
        // ★ 누락되었던 상태바 텍스트 양식(format) 저장 코드 복구!
        wchar_t keyF[32]; wsprintfW(keyF, L"format_%d", i);
        WritePrivateProfileStringW(L"statusbar", keyF, g_app->statusFormats[i].c_str(), path.c_str());

        // 기존에 있던 정렬 방식(align) 저장 코드
        wchar_t keyA[32]; wsprintfW(keyA, L"align_%d", i);
        wsprintfW(buf, L"%d", g_app->statusAligns[i]);
        WritePrivateProfileStringW(L"statusbar", keyA, buf, path.c_str());
    }
}

void QueueSaveWindowSettings(HWND hwnd)
{
    if (!hwnd) return;
    KillTimer(hwnd, ID_TIMER_DEFER_SAVE);
    SetTimer(hwnd, ID_TIMER_DEFER_SAVE, 250, nullptr);
}

void LoadQuickConnectHistory() {
    if (!g_app) return;
    g_app->quickConnectHistory.clear();
    std::wstring path = GetSettingsPath();
    int count = GetPrivateProfileIntW(L"quick_connect", L"count", 0, path.c_str());
    for (int i = 0; i < count; ++i) {
        wchar_t keyA[32], keyC[32], buf[1024] = { 0 };
        wsprintfW(keyA, L"addr_%d", i); wsprintfW(keyC, L"charset_%d", i);
        GetPrivateProfileStringW(L"quick_connect", keyA, L"", buf, 1024, path.c_str());
        int charset = GetPrivateProfileIntW(L"quick_connect", keyC, 0, path.c_str());
        if (buf[0]) g_app->quickConnectHistory.push_back({ buf, charset });
    }
}

void SaveQuickConnectHistory() {
    if (!g_app) return;
    std::wstring path = GetSettingsPath();
    int count = (int)g_app->quickConnectHistory.size();
    wchar_t buf[32]; wsprintfW(buf, L"%d", count);
    WritePrivateProfileStringW(L"quick_connect", L"count", buf, path.c_str());
    for (int i = 0; i < count; ++i) {
        wchar_t keyA[32], keyC[32];
        wsprintfW(keyA, L"addr_%d", i); wsprintfW(keyC, L"charset_%d", i);
        WritePrivateProfileStringW(L"quick_connect", keyA, g_app->quickConnectHistory[i].first.c_str(), path.c_str());
        wsprintfW(buf, L"%d", g_app->quickConnectHistory[i].second);
        WritePrivateProfileStringW(L"quick_connect", keyC, buf, path.c_str());
    }
}

void LoadInputHistorySettings()
{
    if (!g_app) return;
    std::wstring path = GetSettingsPath();
    wchar_t buf[4096] = {};

    g_app->saveInputOnExit = GetPrivateProfileIntW(L"input_history", L"save_on_exit", 0, path.c_str()) ? true : false;
    g_app->limitBackspaceToCurrentRow = GetPrivateProfileIntW(L"input_history", L"limit_backspace", 1, path.c_str()) != 0;

    g_app->history.clear();
    int count = GetPrivateProfileIntW(L"input_history", L"count", 0, path.c_str());
    if (count < 0) count = 0;
    if (count > 1000) count = 1000;

    for (int i = 0; i < count; ++i) {
        wchar_t key[32]; wsprintfW(key, L"item_%d", i);
        GetPrivateProfileStringW(L"input_history", key, L"", buf, 4096, path.c_str());
        if (buf[0] != 0) g_app->history.push_back(buf);
    }
}

void SaveInputHistorySettings()
{
    if (!g_app) return;
    std::wstring path = GetSettingsPath();

    WritePrivateProfileStringW(L"input_history", L"save_on_exit", g_app->saveInputOnExit ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"input_history", L"limit_backspace", g_app->limitBackspaceToCurrentRow ? L"1" : L"0", path.c_str());

    if (!g_app->saveInputOnExit) {
        WritePrivateProfileStringW(L"input_history", L"count", L"0", path.c_str());
        for (int i = 0; i < 1000; ++i) {
            wchar_t key[32]; wsprintfW(key, L"item_%d", i);
            WritePrivateProfileStringW(L"input_history", key, nullptr, path.c_str());
        }
        return;
    }

    int count = (int)g_app->history.size();
    if (count > 1000) count = 1000;

    wchar_t buf[32]; wsprintfW(buf, L"%d", count);
    WritePrivateProfileStringW(L"input_history", L"count", buf, path.c_str());

    for (int i = 0; i < count; ++i) {
        wchar_t key[32]; wsprintfW(key, L"item_%d", i);
        WritePrivateProfileStringW(L"input_history", key, g_app->history[i].c_str(), path.c_str());
    }

    for (int i = count; i < 1000; ++i) {
        wchar_t key[32]; wsprintfW(key, L"item_%d", i);
        WritePrivateProfileStringW(L"input_history", key, nullptr, path.c_str());
    }
}

void ClearInputHistorySettings()
{
    if (!g_app) return;
    g_app->history.clear();
    for (int i = 0; i < INPUT_ROWS; ++i) g_app->displayedHistoryIndex[i] = -1;

    std::wstring path = GetSettingsPath();
    WritePrivateProfileStringW(L"input_history", L"count", L"0", path.c_str());
    for (int i = 0; i < 1000; ++i) {
        wchar_t key[32]; wsprintfW(key, L"item_%d", i);
        WritePrivateProfileStringW(L"input_history", key, nullptr, path.c_str());
    }
}

void LoadFontRenderSettings()
{
    if (!g_app) return;
    std::wstring path = GetSettingsPath();

    // 기본값은 1(사용함)로 설정하거나 0으로 설정하세요.
    g_app->useCustomMudFont = GetPrivateProfileIntW(L"Font", L"use_custom_mud_font", 1, path.c_str()) != 0;

    g_app->smoothFontEnabled = GetPrivateProfileIntW(L"font_render", L"smooth", 1, path.c_str()) != 0;
}

void SaveFontRenderSettings()
{
    if (!g_app) return;
    std::wstring path = GetSettingsPath();
    wchar_t buf[1024];

    // 1. 부드러운 폰트 저장
    wsprintfW(buf, L"%d", g_app->smoothFontEnabled ? 1 : 0);
    WritePrivateProfileStringW(L"font_render", L"smooth", buf, path.c_str());

    // 2. [추가] 전용 폰트 사용 여부 저장
    wsprintfW(buf, L"%d", g_app->useCustomMudFont ? 1 : 0);
    WritePrivateProfileStringW(L"font_render", L"use_custom_mud", buf, path.c_str());
}

BYTE GetCurrentFontQuality()
{
    if (!g_app) return NONANTIALIASED_QUALITY;
    return g_app->smoothFontEnabled ? CLEARTYPE_QUALITY : NONANTIALIASED_QUALITY;
}

void RebuildInputBrushes()
{
    if (!g_app) return;
    if (g_app->hbrInputContainer) { DeleteObject(g_app->hbrInputContainer); g_app->hbrInputContainer = nullptr; }
    if (g_app->hbrInputEdit) { DeleteObject(g_app->hbrInputEdit); g_app->hbrInputEdit = nullptr; }
    if (g_app->hbrInputEditActive) { DeleteObject(g_app->hbrInputEditActive); g_app->hbrInputEditActive = nullptr; }

    COLORREF base = g_app->inputStyle.backColor;
    g_app->hbrInputContainer = CreateSolidBrush(base);
    g_app->hbrInputEdit = CreateSolidBrush(base);
    g_app->hbrInputEditActive = CreateSolidBrush(base);
}

void SaveAutoLoginSettings() {
    if (!g_app) return;
    std::wstring path = GetSettingsPath();
    WritePrivateProfileStringW(L"autologin", L"enabled", g_app->autoLoginEnabled ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"autologin", L"id_pattern", g_app->autoLoginIdPattern.c_str(), path.c_str());
    WritePrivateProfileStringW(L"autologin", L"id", g_app->autoLoginId.c_str(), path.c_str());
    WritePrivateProfileStringW(L"autologin", L"pw_pattern", g_app->autoLoginPwPattern.c_str(), path.c_str());
    WritePrivateProfileStringW(L"autologin", L"pw", g_app->autoLoginPw.c_str(), path.c_str());
    WritePrivateProfileStringW(L"autologin", L"success1", g_app->autoLoginSuccessPattern1.c_str(), path.c_str());
    WritePrivateProfileStringW(L"autologin", L"success2", g_app->autoLoginSuccessPattern2.c_str(), path.c_str());
    WritePrivateProfileStringW(L"autologin", L"success3", g_app->autoLoginSuccessPattern3.c_str(), path.c_str());
    WritePrivateProfileStringW(L"autologin", L"fail1", g_app->autoLoginFailPattern1.c_str(), path.c_str());
    WritePrivateProfileStringW(L"autologin", L"fail2", g_app->autoLoginFailPattern2.c_str(), path.c_str());
    WritePrivateProfileStringW(L"autologin", L"fail3", g_app->autoLoginFailPattern3.c_str(), path.c_str());
}

void LoadAutoLoginSettings() {
    if (!g_app) return;
    std::wstring path = GetSettingsPath();
    wchar_t buf[1024] = {};

    g_app->autoLoginEnabled = GetPrivateProfileIntW(L"autologin", L"enabled", 0, path.c_str()) ? true : false;
    GetPrivateProfileStringW(L"autologin", L"id_pattern", L"아이디:", buf, 1024, path.c_str()); g_app->autoLoginIdPattern = buf;
    GetPrivateProfileStringW(L"autologin", L"id", L"", buf, 1024, path.c_str()); g_app->autoLoginId = buf;
    GetPrivateProfileStringW(L"autologin", L"pw_pattern", L"비밀번호:", buf, 1024, path.c_str()); g_app->autoLoginPwPattern = buf;
    GetPrivateProfileStringW(L"autologin", L"pw", L"", buf, 1024, path.c_str()); g_app->autoLoginPw = buf;
    GetPrivateProfileStringW(L"autologin", L"success1", L"", buf, 1024, path.c_str()); g_app->autoLoginSuccessPattern1 = buf;
    GetPrivateProfileStringW(L"autologin", L"success2", L"", buf, 1024, path.c_str()); g_app->autoLoginSuccessPattern2 = buf;
    GetPrivateProfileStringW(L"autologin", L"success3", L"", buf, 1024, path.c_str()); g_app->autoLoginSuccessPattern3 = buf;
    GetPrivateProfileStringW(L"autologin", L"fail1", L"", buf, 1024, path.c_str()); g_app->autoLoginFailPattern1 = buf;
    GetPrivateProfileStringW(L"autologin", L"fail2", L"", buf, 1024, path.c_str()); g_app->autoLoginFailPattern2 = buf;
    GetPrivateProfileStringW(L"autologin", L"fail3", L"", buf, 1024, path.c_str()); g_app->autoLoginFailPattern3 = buf;

    g_app->activeAutoLoginEnabled = false;
    g_app->autoLoginWindowActive = false;
    g_app->keepAliveBlockedUntilTick = 0;
}

static bool ModifyMenuTextRecursive(HMENU hMenu, UINT id, const wchar_t* text)
{
    if (!hMenu)
        return false;

    int count = GetMenuItemCount(hMenu);
    for (int i = 0; i < count; ++i)
    {
        MENUITEMINFOW mi{};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_ID | MIIM_SUBMENU;
        if (!GetMenuItemInfoW(hMenu, i, TRUE, &mi))
            continue;

        if (!mi.hSubMenu && mi.wID == id)
        {
            ModifyMenuW(hMenu, id, MF_BYCOMMAND | MF_STRING, id, text);
            return true;
        }

        if (mi.hSubMenu && ModifyMenuTextRecursive(mi.hSubMenu, id, text))
            return true;
    }
    return false;
}

void UpdateMenuToggleStates()
{
    if (!g_app || !g_app->hMainMenu) return;

    ModifyMenuTextRecursive(g_app->hMainMenu,
        ID_MENU_CAPTURE_TOGGLE,
        g_app->captureLogEnabled ? L"갈무리 켜짐" : L"갈무리 꺼짐");

    // 옵션(O) 하위 메뉴 업데이트 (3번째 인덱스)
    HMENU hOptions = GetSubMenu(g_app->hMainMenu, 3);
    if (hOptions) {
        ModifyMenuW(hOptions, ID_MENU_OPTIONS_SHORTCUTBAR, MF_BYCOMMAND | MF_STRING,
            ID_MENU_OPTIONS_SHORTCUTBAR,
            (LPCTSTR)(g_app->shortcutBarVisible ? L"단축버튼 숨기기(&H)" : L"단축버튼 표시(&H)"));

        // 접속 유지 켜기/끄기
        ModifyMenuW(hOptions, ID_MENU_OPTIONS_KEEPALIVE_TOGGLE, MF_BYCOMMAND | MF_STRING,
            ID_MENU_OPTIONS_KEEPALIVE_TOGGLE,
            (LPCTSTR)(g_app->keepAliveEnabled ? L"접속 유지 끄기(&K)" : L"접속 유지 켜기(&K)"));

    }

    if (g_app->hwndMain) {
        InvalidateRect(g_app->hwndMain, nullptr, FALSE);
        UpdateWindow(g_app->hwndMain);
    }
}

void LoadGeneralSettings()
{
    if (!g_app) return;
    std::wstring path = GetSettingsPath();
    // 기본값은 1(소리 켬)로 설정
    g_app->soundEnabled = GetPrivateProfileIntW(L"Options", L"sound_enabled", 0, path.c_str()) != 0;

    // buildfix5부터는 한국어 MUD 박스/도형/화살표가 깨지지 않도록
    // 기존 설정 파일에 0이 남아 있어도 최초 1회는 넓게 처리로 보정합니다.
    if (GetPrivateProfileIntW(L"Options", L"AmbiguousSafeDefaultApplied", 0, path.c_str()) == 0)
    {
        g_app->ambiguousEastAsianWide = true;
        WritePrivateProfileStringW(L"Options", L"AmbiguousEastAsianWide", L"1", path.c_str());
        WritePrivateProfileStringW(L"Options", L"AmbiguousSafeDefaultApplied", L"1", path.c_str());
    }
    else
    {
        g_app->ambiguousEastAsianWide = GetPrivateProfileIntW(L"Options", L"AmbiguousEastAsianWide", 1, path.c_str()) != 0;
    }
    g_app->mainAlwaysOnTop = GetPrivateProfileIntW(L"Options", L"MainAlwaysOnTop", 0, path.c_str()) != 0;
    g_app->tailSnapEnabled = GetPrivateProfileIntW(L"Options", L"TailSnapEnabled", 1, path.c_str()) != 0;
}

void SaveGeneralSettings()
{
    if (!g_app) return;
    std::wstring path = GetSettingsPath();
    WritePrivateProfileStringW(L"Options", L"sound_enabled", g_app->soundEnabled ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Options", L"AmbiguousEastAsianWide",
        g_app->ambiguousEastAsianWide ? L"1" : L"0", path.c_str());
}