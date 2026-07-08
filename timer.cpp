#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "timer.h"
#include "settings.h"
#include <commctrl.h>

// ==============================================
// 1. 설정 로드 및 저장 (INI)
// ==============================================
void LoadTimerSettings()
{
    if (!g_app) return;
    g_app->timers.clear();

    std::wstring path = GetSettingsPath();
    int count = GetPrivateProfileIntW(L"Timer", L"Count", 0, path.c_str());

    for (int i = 0; i < count; ++i)
    {
        wchar_t sec[32], buf[1024];
        wsprintfW(sec, L"Timer_%d", i);

        TimerItem t;
        GetPrivateProfileStringW(L"Timer", (std::wstring(sec) + L"_Name").c_str(), L"", buf, 1024, path.c_str()); t.name = buf;
        GetPrivateProfileStringW(L"Timer", (std::wstring(sec) + L"_Group").c_str(), L"", buf, 1024, path.c_str()); t.groupPath = buf;

        t.enabled = GetPrivateProfileIntW(L"Timer", (std::wstring(sec) + L"_Enabled").c_str(), 0, path.c_str()) != 0;
        t.repeat = GetPrivateProfileIntW(L"Timer", (std::wstring(sec) + L"_Repeat").c_str(), 0, path.c_str()) != 0;
        t.autoStart = GetPrivateProfileIntW(L"Timer", (std::wstring(sec) + L"_AutoStart").c_str(), 0, path.c_str()) != 0;

        t.hour = GetPrivateProfileIntW(L"Timer", (std::wstring(sec) + L"_H").c_str(), 0, path.c_str());
        t.minute = GetPrivateProfileIntW(L"Timer", (std::wstring(sec) + L"_M").c_str(), 0, path.c_str());
        t.second = GetPrivateProfileIntW(L"Timer", (std::wstring(sec) + L"_S").c_str(), 0, path.c_str());
        t.millisecond = GetPrivateProfileIntW(L"Timer", (std::wstring(sec) + L"_Ms").c_str(), 0, path.c_str());

        GetPrivateProfileStringW(L"Timer", (std::wstring(sec) + L"_Cmd").c_str(), L"", buf, 1024, path.c_str()); t.command = buf;

        // 런타임 변수 초기화
        t.state = TIMER_STOPPED;
        RecalculateTimerInterval(t);

        if (!t.name.empty())
        {
            g_app->timers.push_back(t);
        }
    }

    // 프로그램 시작 시 AutoStart 처리
    for (auto& t : g_app->timers)
    {
        if (t.enabled && t.autoStart)
        {
            StartTimerItem(t);
        }
    }
}

void SaveTimerSettings()
{
    if (!g_app) return;
    std::wstring path = GetSettingsPath();

    // 기존 섹션 초기화 (찌꺼기 방지)
    WritePrivateProfileStringW(L"Timer", nullptr, nullptr, path.c_str());

    int count = (int)g_app->timers.size();
    wchar_t buf[32];
    wsprintfW(buf, L"%d", count);
    WritePrivateProfileStringW(L"Timer", L"Count", buf, path.c_str());

    for (int i = 0; i < count; ++i)
    {
        const auto& t = g_app->timers[i];
        wchar_t sec[32];
        wsprintfW(sec, L"Timer_%d", i);

        WritePrivateProfileStringW(L"Timer", (std::wstring(sec) + L"_Name").c_str(), t.name.c_str(), path.c_str());
        WritePrivateProfileStringW(L"Timer", (std::wstring(sec) + L"_Group").c_str(), t.groupPath.c_str(), path.c_str());

        WritePrivateProfileStringW(L"Timer", (std::wstring(sec) + L"_Enabled").c_str(), t.enabled ? L"1" : L"0", path.c_str());
        WritePrivateProfileStringW(L"Timer", (std::wstring(sec) + L"_Repeat").c_str(), t.repeat ? L"1" : L"0", path.c_str());
        WritePrivateProfileStringW(L"Timer", (std::wstring(sec) + L"_AutoStart").c_str(), t.autoStart ? L"1" : L"0", path.c_str());

        wsprintfW(buf, L"%d", t.hour); WritePrivateProfileStringW(L"Timer", (std::wstring(sec) + L"_H").c_str(), buf, path.c_str());
        wsprintfW(buf, L"%d", t.minute); WritePrivateProfileStringW(L"Timer", (std::wstring(sec) + L"_M").c_str(), buf, path.c_str());
        wsprintfW(buf, L"%d", t.second); WritePrivateProfileStringW(L"Timer", (std::wstring(sec) + L"_S").c_str(), buf, path.c_str());
        wsprintfW(buf, L"%d", t.millisecond); WritePrivateProfileStringW(L"Timer", (std::wstring(sec) + L"_Ms").c_str(), buf, path.c_str());

        WritePrivateProfileStringW(L"Timer", (std::wstring(sec) + L"_Cmd").c_str(), t.command.c_str(), path.c_str());
    }
}

// ==============================================
// 2. 런타임 제어 및 엔진 로직
// ==============================================
void RecalculateTimerInterval(TimerItem& t)
{
    t.intervalMs = (t.hour * 3600000ULL) + (t.minute * 60000ULL) + (t.second * 1000ULL) + t.millisecond;
}

void StartTimerItem(TimerItem& t)
{
    if (!t.enabled) return;
    RecalculateTimerInterval(t);
    if (t.intervalMs == 0) return;

    t.state = TIMER_RUNNING;
    t.lastFireTick = GetTickCount64();
    t.nextFireTick = t.lastFireTick + t.intervalMs;
}

void StopTimerItem(TimerItem& t)
{
    t.state = TIMER_STOPPED;
    t.remainingMs = 0;
}

void PauseTimerItem(TimerItem& t)
{
    if (t.state == TIMER_RUNNING)
    {
        ULONGLONG now = GetTickCount64();
        t.state = TIMER_PAUSED;
        if (t.nextFireTick > now)
            t.remainingMs = t.nextFireTick - now;
        else
            t.remainingMs = 0;
    }
}

void ResumeTimerItem(TimerItem& t)
{
    if (t.state == TIMER_PAUSED && t.enabled)
    {
        t.state = TIMER_RUNNING;
        t.nextFireTick = GetTickCount64() + t.remainingMs;
    }
}

void ResetTimerItem(TimerItem& t)
{
    if (t.state == TIMER_RUNNING || t.state == TIMER_PAUSED)
    {
        StartTimerItem(t); // 진행 중인 타이머를 즉시 처음부터 다시 돌림
    }
}

// WM_TIMER 에서 50ms마다 호출될 핵심 엔진
void RunTimerEngine()
{
    if (!g_app) return;
    ULONGLONG now = GetTickCount64();

    for (auto& t : g_app->timers)
    {
        if (t.enabled && t.state == TIMER_RUNNING)
        {
            if (now >= t.nextFireTick)
            {
                // 실행할 명령어가 있다면 서버(또는 내부)로 전송
                if (!t.command.empty())
                {
                    SendRawCommandToMud(t.command);
                }

                // 상태 갱신
                if (t.repeat)
                {
                    t.lastFireTick = now;
                    // 오차 누적 방지를 위해 intervalMs만큼 더함
                    t.nextFireTick += t.intervalMs; 
                    // 혹시 너무 지연되어 과거 시간이면 현재 시간 기준으로 리셋
                    if (t.nextFireTick < now) t.nextFireTick = now + t.intervalMs; 
                }
                else
                {
                    t.state = TIMER_STOPPED;
                }
            }
        }
    }
}

// #TIMER 명령 가로채기 (트리거나 사용자가 입력창에 쳤을 때)
bool InterceptTimerCommand(const std::wstring& inputCmd)
{
    if (!g_app || inputCmd.empty()) return false;

    std::wstring cmd = Trim(inputCmd);
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::towupper);

    if (cmd.find(L"#TIMER ") == 0 || cmd.find(L"#TIMERGROUP ") == 0)
    {
        // 포맷: #TIMER {타이머이름} {액션}
        // 포맷: #TIMERGROUP {그룹명} {액션}
        size_t firstOpen = cmd.find(L'{');
        size_t firstClose = cmd.find(L'}', firstOpen);
        size_t secondOpen = cmd.find(L'{', firstClose);
        size_t secondClose = cmd.find(L'}', secondOpen);

        if (firstOpen != std::wstring::npos && firstClose != std::wstring::npos &&
            secondOpen != std::wstring::npos && secondClose != std::wstring::npos)
        {
            std::wstring targetName = cmd.substr(firstOpen + 1, firstClose - firstOpen - 1);
            std::wstring action = cmd.substr(secondOpen + 1, secondClose - secondOpen - 1);

            bool isGroup = (cmd.find(L"#TIMERGROUP ") == 0);

            for (auto& t : g_app->timers)
            {
                bool match = false;
                if (isGroup) match = (_wcsicmp(t.groupPath.c_str(), targetName.c_str()) == 0);
                else         match = (_wcsicmp(t.name.c_str(), targetName.c_str()) == 0);

                if (match)
                {
                    if (action == L"START") StartTimerItem(t);
                    else if (action == L"STOP") StopTimerItem(t);
                    else if (action == L"PAUSE") PauseTimerItem(t);
                    else if (action == L"RESUME") ResumeTimerItem(t);
                    else if (action == L"RESTART" || action == L"RESET") ResetTimerItem(t);
                    else if (action == L"ON") t.enabled = true;
                    else if (action == L"OFF") { t.enabled = false; StopTimerItem(t); }
                    else if (action == L"TOGGLE") {
                        t.enabled = !t.enabled;
                        if (t.enabled) StartTimerItem(t); else StopTimerItem(t);
                    }
                }
            }
            return true; // 엔진에서 처리했으므로 서버로 전송 안 함
        }
    }
    return false;
}

// ==============================================
// 3. UI 처리부 (항목 관리자 형태)
// ==============================================
static void RefreshTimerList(HWND hList)
{
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);
    for (const auto& t : g_app->timers)
    {
        std::wstring title = t.enabled ? L"[켬] " : L"[끔] ";
        title += t.groupPath.empty() ? L"" : L"[" + t.groupPath + L"] ";
        title += t.name.empty() ? L"(이름 없음)" : t.name;
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)title.c_str());
    }
}

static void UpdateTimerDetailUI(HWND hwnd, int idx)
{
    bool hasSel = (idx >= 0 && idx < (int)g_app->timers.size());

    int controls[] = { ID_TIMER_EDIT_NAME, ID_TIMER_EDIT_GROUP, ID_TIMER_CHK_ENABLE, ID_TIMER_CHK_REPEAT,
                       ID_TIMER_CHK_AUTOSTART, ID_TIMER_EDIT_H, ID_TIMER_EDIT_M, ID_TIMER_EDIT_S,
                       ID_TIMER_EDIT_MS, ID_TIMER_EDIT_CMD, ID_TIMER_BTN_START }; 
    for (int id : controls) EnableWindow(GetDlgItem(hwnd, id), hasSel);

    if (!hasSel)
    {
        for(int id : {ID_TIMER_EDIT_NAME, ID_TIMER_EDIT_GROUP, ID_TIMER_EDIT_H, ID_TIMER_EDIT_M, ID_TIMER_EDIT_S, ID_TIMER_EDIT_MS, ID_TIMER_EDIT_CMD})
            SetWindowTextW(GetDlgItem(hwnd, id), L"");
        return;
    }

    const auto& t = g_app->timers[idx];
    SetWindowTextW(GetDlgItem(hwnd, ID_TIMER_EDIT_NAME), t.name.c_str());
    SetWindowTextW(GetDlgItem(hwnd, ID_TIMER_EDIT_GROUP), t.groupPath.c_str());

    SendMessageW(GetDlgItem(hwnd, ID_TIMER_CHK_ENABLE), BM_SETCHECK, t.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(hwnd, ID_TIMER_CHK_REPEAT), BM_SETCHECK, t.repeat ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(hwnd, ID_TIMER_CHK_AUTOSTART), BM_SETCHECK, t.autoStart ? BST_CHECKED : BST_UNCHECKED, 0);

    wchar_t buf[16];
    wsprintfW(buf, L"%d", t.hour); SetWindowTextW(GetDlgItem(hwnd, ID_TIMER_EDIT_H), buf);
    wsprintfW(buf, L"%d", t.minute); SetWindowTextW(GetDlgItem(hwnd, ID_TIMER_EDIT_M), buf);
    wsprintfW(buf, L"%d", t.second); SetWindowTextW(GetDlgItem(hwnd, ID_TIMER_EDIT_S), buf);
    wsprintfW(buf, L"%d", t.millisecond); SetWindowTextW(GetDlgItem(hwnd, ID_TIMER_EDIT_MS), buf);

    SetWindowTextW(GetDlgItem(hwnd, ID_TIMER_EDIT_CMD), t.command.c_str());
}

static void SyncTimerDataFromUI(HWND hwnd, int idx)
{
    if (idx < 0 || idx >= (int)g_app->timers.size()) return;
    auto& t = g_app->timers[idx];

    wchar_t buf[1024];
    GetWindowTextW(GetDlgItem(hwnd, ID_TIMER_EDIT_NAME), buf, 1024); t.name = buf;
    GetWindowTextW(GetDlgItem(hwnd, ID_TIMER_EDIT_GROUP), buf, 1024); t.groupPath = buf;

    t.enabled = (SendMessageW(GetDlgItem(hwnd, ID_TIMER_CHK_ENABLE), BM_GETCHECK, 0, 0) == BST_CHECKED);
    t.repeat = (SendMessageW(GetDlgItem(hwnd, ID_TIMER_CHK_REPEAT), BM_GETCHECK, 0, 0) == BST_CHECKED);
    t.autoStart = (SendMessageW(GetDlgItem(hwnd, ID_TIMER_CHK_AUTOSTART), BM_GETCHECK, 0, 0) == BST_CHECKED);

    GetWindowTextW(GetDlgItem(hwnd, ID_TIMER_EDIT_H), buf, 16); t.hour = _wtoi(buf);
    GetWindowTextW(GetDlgItem(hwnd, ID_TIMER_EDIT_M), buf, 16); t.minute = _wtoi(buf);
    GetWindowTextW(GetDlgItem(hwnd, ID_TIMER_EDIT_S), buf, 16); t.second = _wtoi(buf);
    GetWindowTextW(GetDlgItem(hwnd, ID_TIMER_EDIT_MS), buf, 16); t.millisecond = _wtoi(buf);

    GetWindowTextW(GetDlgItem(hwnd, ID_TIMER_EDIT_CMD), buf, 1024); t.command = buf;

    RecalculateTimerInterval(t);
}

static LRESULT CALLBACK TimerDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int s_sel = -1;
    switch (msg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    case WM_CREATE:
    {
        HFONT hF = GetPopupUIFont(hwnd);
        HINSTANCE hInst = GetModuleHandle(0);

        // 좌측 목록
        CreateWindowExW(0, L"STATIC", L"타이머 목록", WS_CHILD | WS_VISIBLE, 15, 12, 100, 20, hwnd, 0, hInst, 0);
        HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
            15, 28, 240, 270, hwnd, (HMENU)ID_TIMER_LIST, hInst, 0);

        CreateWindowExW(0, L"BUTTON", L"추가", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 15, 310, 60, 28, hwnd, (HMENU)ID_TIMER_BTN_ADD, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"삭제", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 80, 310, 60, 28, hwnd, (HMENU)ID_TIMER_BTN_DEL, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"▲", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 145, 310, 35, 28, hwnd, (HMENU)ID_TIMER_BTN_UP, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"▼", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 185, 310, 35, 28, hwnd, (HMENU)ID_TIMER_BTN_DOWN, hInst, 0);

        // 우측 상세 설정
        CreateWindowExW(0, L"BUTTON", L"타이머 상세 속성", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 270, 15, 400, 283, hwnd, 0, hInst, 0);

        CreateWindowExW(0, L"STATIC", L"이름:", WS_CHILD | WS_VISIBLE, 290, 40, 50, 20, hwnd, 0, hInst, 0);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 345, 37, 150, 24, hwnd, (HMENU)ID_TIMER_EDIT_NAME, hInst, 0);

        CreateWindowExW(0, L"STATIC", L"그룹:", WS_CHILD | WS_VISIBLE, 505, 40, 40, 20, hwnd, 0, hInst, 0);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 550, 37, 100, 24, hwnd, (HMENU)ID_TIMER_EDIT_GROUP, hInst, 0);

        CreateWindowExW(0, L"BUTTON", L"사용 활성화", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 290, 75, 100, 20, hwnd, (HMENU)ID_TIMER_CHK_ENABLE, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"반복 실행", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 400, 75, 100, 20, hwnd, (HMENU)ID_TIMER_CHK_REPEAT, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"시작 시 작동", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 510, 75, 120, 20, hwnd, (HMENU)ID_TIMER_CHK_AUTOSTART, hInst, 0);

        CreateWindowExW(0, L"STATIC", L"주기 (시간/분/초/밀리초):", WS_CHILD | WS_VISIBLE, 290, 110, 200, 20, hwnd, 0, hInst, 0);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 290, 130, 40, 24, hwnd, (HMENU)ID_TIMER_EDIT_H, hInst, 0);
        CreateWindowExW(0, L"STATIC", L"h", WS_CHILD | WS_VISIBLE, 332, 133, 15, 20, hwnd, 0, hInst, 0);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 350, 130, 40, 24, hwnd, (HMENU)ID_TIMER_EDIT_M, hInst, 0);
        CreateWindowExW(0, L"STATIC", L"m", WS_CHILD | WS_VISIBLE, 392, 133, 15, 20, hwnd, 0, hInst, 0);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 410, 130, 40, 24, hwnd, (HMENU)ID_TIMER_EDIT_S, hInst, 0);
        CreateWindowExW(0, L"STATIC", L"s", WS_CHILD | WS_VISIBLE, 452, 133, 15, 20, hwnd, 0, hInst, 0);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 470, 130, 50, 24, hwnd, (HMENU)ID_TIMER_EDIT_MS, hInst, 0);
        CreateWindowExW(0, L"STATIC", L"ms", WS_CHILD | WS_VISIBLE, 522, 133, 25, 20, hwnd, 0, hInst, 0);

        CreateWindowExW(0, L"STATIC", L"실행할 명령:", WS_CHILD | WS_VISIBLE, 290, 170, 100, 20, hwnd, 0, hInst, 0);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 290, 190, 360, 24, hwnd, (HMENU)ID_TIMER_EDIT_CMD, hInst, 0);

        CreateWindowExW(0, L"STATIC", L"※ 안내: 1회성 타이머는 실행 후 자동으로 정지됩니다.", WS_CHILD | WS_VISIBLE, 290, 225, 370, 18, hwnd, 0, hInst, 0);
        CreateWindowExW(0, L"STATIC", L"   정지된 타이머를 다시 시작하려면 [적용] 버튼을 누르세요.", WS_CHILD | WS_VISIBLE, 290, 245, 370, 18, hwnd, 0, hInst, 0);
        CreateWindowExW(0, L"STATIC", L"   또는 #TIMER {이름} {START|STOP} 명령으로 제어 가능합니다.", WS_CHILD | WS_VISIBLE, 290, 265, 370, 18, hwnd, 0, hInst, 0);

        // 하단 제어 버튼 5개 나란히 배치
        CreateWindowExW(0, L"BUTTON", L"모두 활성화", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 270, 310, 85, 30, hwnd, (HMENU)ID_TIMER_BTN_ENABLE_ALL, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"모두 비활성", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 360, 310, 85, 30, hwnd, (HMENU)ID_TIMER_BTN_DISABLE_ALL, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"시작(&S)", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 450, 310, 70, 30, hwnd, (HMENU)ID_TIMER_BTN_START, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"적용(&A)", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 525, 310, 70, 30, hwnd, (HMENU)ID_TIMER_BTN_APPLY, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"닫기(&C)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 600, 310, 70, 30, hwnd, (HMENU)IDCANCEL, hInst, 0);

        EnumChildWindows(hwnd, [](HWND c, LPARAM f) { SendMessageW(c, WM_SETFONT, f, TRUE); return TRUE; }, (LPARAM)hF);
        SendMessageW(hList, LB_SETITEMHEIGHT, 0, 20);

        s_sel = -1;
        RefreshTimerList(hList);
        UpdateTimerDetailUI(hwnd, -1);
        return 0;
    }
    case WM_SYSCHAR:
    {
        wchar_t ch = towlower((wchar_t)wParam);
        if (ch == 'a') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_TIMER_BTN_APPLY, BN_CLICKED), 0); return 0; }
        if (ch == 'c') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0); return 0; }
        break;
    }
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == ID_TIMER_LIST && HIWORD(wParam) == LBN_SELCHANGE)
        {
            HWND hList = GetDlgItem(hwnd, ID_TIMER_LIST);
            int newSel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
            if (s_sel >= 0 && s_sel < (int)g_app->timers.size()) SyncTimerDataFromUI(hwnd, s_sel);
            s_sel = newSel;
            UpdateTimerDetailUI(hwnd, s_sel);
        }
        else if (id == ID_TIMER_BTN_ADD)
        {
            if (s_sel >= 0) SyncTimerDataFromUI(hwnd, s_sel);
            g_app->timers.push_back(TimerItem());
            RefreshTimerList(GetDlgItem(hwnd, ID_TIMER_LIST));
            s_sel = (int)g_app->timers.size() - 1;
            SendMessageW(GetDlgItem(hwnd, ID_TIMER_LIST), LB_SETCURSEL, s_sel, 0);
            UpdateTimerDetailUI(hwnd, s_sel);
        }
        else if (id == ID_TIMER_BTN_DEL && s_sel >= 0)
        {
            g_app->timers.erase(g_app->timers.begin() + s_sel);
            s_sel = -1;
            RefreshTimerList(GetDlgItem(hwnd, ID_TIMER_LIST));
            UpdateTimerDetailUI(hwnd, -1);
        }
        else if (id == ID_TIMER_BTN_UP && s_sel > 0)
        {
            SyncTimerDataFromUI(hwnd, s_sel);
            std::swap(g_app->timers[s_sel], g_app->timers[s_sel - 1]);
            s_sel--;
            RefreshTimerList(GetDlgItem(hwnd, ID_TIMER_LIST));
            SendMessageW(GetDlgItem(hwnd, ID_TIMER_LIST), LB_SETCURSEL, s_sel, 0);
            UpdateTimerDetailUI(hwnd, s_sel);
        }
        else if (id == ID_TIMER_BTN_DOWN && s_sel >= 0 && s_sel < (int)g_app->timers.size() - 1)
        {
            SyncTimerDataFromUI(hwnd, s_sel);
            std::swap(g_app->timers[s_sel], g_app->timers[s_sel + 1]);
            s_sel++;
            RefreshTimerList(GetDlgItem(hwnd, ID_TIMER_LIST));
            SendMessageW(GetDlgItem(hwnd, ID_TIMER_LIST), LB_SETCURSEL, s_sel, 0);
            UpdateTimerDetailUI(hwnd, s_sel);
        }
        else if (id == ID_TIMER_BTN_ENABLE_ALL)
        {
            if (s_sel >= 0) SyncTimerDataFromUI(hwnd, s_sel); // 현재 편집 중인 내용 임시 저장

            for (auto& t : g_app->timers) {
                t.enabled = true;      // 메모리상 전부 켬
                StartTimerItem(t);     // 엔진 강제 시작
            }
            SaveTimerSettings();       // INI 파일에 즉시 저장
            RefreshTimerList(GetDlgItem(hwnd, ID_TIMER_LIST)); // 좌측 리스트 새로고침

            if (s_sel >= 0) {
                SendMessageW(GetDlgItem(hwnd, ID_TIMER_LIST), LB_SETCURSEL, s_sel, 0);
                UpdateTimerDetailUI(hwnd, s_sel); // 우측 체크박스 UI 새로고침
            }
            return 0;
        }
        else if (id == ID_TIMER_BTN_DISABLE_ALL)
        {
            if (s_sel >= 0) SyncTimerDataFromUI(hwnd, s_sel);

            for (auto& t : g_app->timers) {
                t.enabled = false;     // 메모리상 전부 끔
                StopTimerItem(t);      // 엔진 강제 정지
            }
            SaveTimerSettings();
            RefreshTimerList(GetDlgItem(hwnd, ID_TIMER_LIST));

            if (s_sel >= 0) {
                SendMessageW(GetDlgItem(hwnd, ID_TIMER_LIST), LB_SETCURSEL, s_sel, 0);
                UpdateTimerDetailUI(hwnd, s_sel);
            }
            return 0;
        }
        else if (id == ID_TIMER_BTN_START && s_sel >= 0)
        {
            SyncTimerDataFromUI(hwnd, s_sel); // 1. 현재 화면에 적힌(시간, 명령 등) 값을 읽어옴

            g_app->timers[s_sel].enabled = true; // 2. 메모리 상에서 무조건 활성화
            StartTimerItem(g_app->timers[s_sel]); // 3. 타이머 엔진 강제 시작

            SendMessageW(GetDlgItem(hwnd, ID_TIMER_CHK_ENABLE), BM_SETCHECK, BST_CHECKED, 0); // 4. 화면의 체크박스도 V 체크!

            SaveTimerSettings(); // 5. 파일에 저장
            RefreshTimerList(GetDlgItem(hwnd, ID_TIMER_LIST)); // 6. 왼쪽 리스트뷰 [켬] 상태로 새로고침
            SendMessageW(GetDlgItem(hwnd, ID_TIMER_LIST), LB_SETCURSEL, s_sel, 0);
        }
        else if (id == ID_TIMER_BTN_APPLY || id == IDOK)
        {
            if (s_sel >= 0)
            {
                bool wasEnabled = g_app->timers[s_sel].enabled; // 변경 전 상태 기억
                SyncTimerDataFromUI(hwnd, s_sel); // 화면 데이터 읽어오기

                auto& t = g_app->timers[s_sel];

                // ★ 여기서 시작/정지를 제어합니다! (적용 버튼 고유의 기능)
                if (t.enabled)
                {
                    if (!wasEnabled || t.state == TIMER_STOPPED)
                        StartTimerItem(t);
                }
                else
                {
                    StopTimerItem(t);
                }
            }

            SaveTimerSettings();
            RefreshTimerList(GetDlgItem(hwnd, ID_TIMER_LIST));
            if (s_sel >= 0) SendMessageW(GetDlgItem(hwnd, ID_TIMER_LIST), LB_SETCURSEL, s_sel, 0);
            if (id == IDOK) DestroyWindow(hwnd);
        }
        else if (id == IDCANCEL)
        {
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_MEASUREITEM:
    {
        MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*)lParam;
        if (mis && mis->CtlID == ID_TIMER_LIST) { mis->itemHeight = 22; return TRUE; }
        break;
    }
    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis && dis->CtlID == ID_TIMER_LIST && dis->itemID != (UINT)-1)
        {
            wchar_t text[256] = {};
            SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, (LPARAM)text);

            bool isOn = wcsncmp(text, L"[켬]", 3) == 0;
            COLORREF bg = (dis->itemState & ODS_SELECTED) ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_WINDOW);
            COLORREF fg = (dis->itemState & ODS_SELECTED) ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_WINDOWTEXT);
            COLORREF prefixColor = isOn ? RGB(0, 160, 0) : RGB(200, 40, 40);

            HBRUSH hbr = CreateSolidBrush(bg);
            FillRect(dis->hDC, &dis->rcItem, hbr);
            DeleteObject(hbr);
            SetBkMode(dis->hDC, TRANSPARENT);

            HFONT hFont = GetPopupUIFont(hwnd);
            HFONT hOld = (HFONT)SelectObject(dis->hDC, hFont);

            RECT rcPrefix = dis->rcItem; rcPrefix.left += 4; rcPrefix.right = rcPrefix.left + 36;
            RECT rcText = dis->rcItem; rcText.left += 40;

            SetTextColor(dis->hDC, prefixColor);
            DrawTextW(dis->hDC, text, 3, &rcPrefix, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

            SetTextColor(dis->hDC, fg);
            DrawTextW(dis->hDC, text + 4, -1, &rcText, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

            SelectObject(dis->hDC, hOld);
            if (dis->itemState & ODS_FOCUS) DrawFocusRect(dis->hDC, &dis->rcItem);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void PromptTimerDialog(HWND owner)
{
    const wchar_t kClass[] = L"TTGuiTimerPopupClass";
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = {}; wc.lpfnWndProc = TimerDialogProc; wc.hInstance = GetModuleHandle(0);
        wc.lpszClassName = kClass; wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassW(&wc); reg = true;
    }

    int w = 700, h = 400;
    RECT rcO; GetWindowRect(owner ? owner : GetDesktopWindow(), &rcO);
    int x = rcO.left + (rcO.right - rcO.left - w) / 2;
    int y = rcO.top + (rcO.bottom - rcO.top - h) / 2;

    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"타이머 엔진 설정",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h, owner, nullptr, GetModuleHandle(0), nullptr);

    ApplyPopupTitleBarTheme(hDlg);
    if (owner) EnableWindow(owner, FALSE);

    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, 0, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }

    if (owner) { EnableWindow(owner, TRUE); SetActiveWindow(owner); }
}
