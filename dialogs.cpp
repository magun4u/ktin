#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "chat_capture.h"
#include "dialogs.h"
#include "theme.h"
#include "memo.h"
#include "shortcut_bar.h"
#include "resource.h"
#include "auto_login.h"
#include "settings.h"
#include <commctrl.h>
#include <shellapi.h>

// 전역 변수
static FindState g_findState = {};
static HFONT g_hFontFindDialog = nullptr;

// ==============================================
// 특수 기호 데이터
// ==============================================
struct SymbolCategory {
    const wchar_t* name;
    const wchar_t* symbols;
};

// ㄱ~ㅎ 에 해당하는 표준 한자 특수기호 모음 (이야기 그림 제외)
const SymbolCategory kSpecialSymbols[] = {
    { L"도형 문자 (ㅁ)", L"＃＆＊＠§※☆★○●◎◇◆□■△▲▽▼→←↑↓↔↕◁◀▷▶♤♠♡♥♧♣⊙◈▣◐◑▒▤▥▨▧▦▩♨☏☎☜☞¶†‡↕↗↙↖↘♭♩♪♬㉿㈜№㏇™㏂㏘℡®ªº" },
    { L"전각 기호 (ㄱ)", L"！＂＃＄％＆＇（）＊＋，－．／０１２３４５６７８９：；＜＝＞？＠ＡＢＣＤＥＦＧＨＩＪＫＬＭＮＯＰＱＲＳＴＵＶＷＸＹＺ［＼］＾＿｀ａｂｃｄｅｆｇｈｉｊｋｌｍｎｏｐｑｒｓｔｕｖｗｘｙｚ｛｜｝￣" },
    { L"괄호 문자 (ㄴ)", L"＂（）［］｛｝‘’“”〔〕〈〉《》「」『』【】" },
    { L"수학 기호 (ㄷ)", L"＋－＜＝＞±×÷≠≤≥∞∴♂♀∠⊥⌒∂∇≡≒≪≫√∽∝∵∫∬∈∋⊆⊇⊂⊃∪∩∧∨￢⇒⇔∀∃∮∑∏" },
    { L"단위 기호 (ㄹ)", L"＄％￦Ｆ′″℃Å￠￡￥¤℉‰€㎕㎖㎗ℓ㎘㏄㎣㎤㎥㎦㎙㎚㎛㎜㎝㎞㎟㎠㎡㎢㏊㎍㎎㎏㏏㎈㎉㏈㎧㎨㎲㎳㎴㎵㎶㎷㎸㎹㎀㎁㎂㎃㎄㎺㎻㎼㎽㎾㎿㎐㎑㎒㎓㎔Ω㏀㏁㎊㎋㎌㏖㏅㎭㎮㎯㏛㎩㎪㎫㎬㏝㏐㏓㏃㏉㏜㏆" },
    { L"선 문자 (ㅂ)", L"─│┌┐┘└├┬┤┴┼━┃┏┓┛┗┣┳┫┻╋┠┯┨┷┿┝┰┥┸╂┒┒┚┚┖┖┖┢┢┪┪┲┲┺┺" },
    { L"원/괄호(한글) (ㅅ)", L"㉠㉡㉢㉣㉤㉥㉦㉧㉨㉩㉪㉫㉬㉭㉮㉯㉰㉱㉲㉳㉴㉵㉶㉷㉸㉹㉺㉻ⓐⓑⓒⓓⓔⓕⓖⓗⓘⓙⓚⓛⓜⓝⓞⓟⓠⓡⓢⓣⓤⓥⓦⓧⓨⓩ①②③④⑤⑥⑦⑧⑨⑩⑪⑫⑬⑭⑮" },
    { L"원/괄호(영숫자) (ㅇ)", L"⒜⒝⒞⒟⒠⒡⒢⒣⒤⒥⒦⒧⒨⒩⒪⒟⑴⑵⑶⑷⑸⑹⑺⑻⑼⑽⑾⑿⒀⒁⒂" },
    { L"로마 숫자 (ㅈ)", L"ⅰⅱⅲⅳⅴⅵⅶⅷⅸⅹⅠⅡⅢⅣⅤⅥⅦⅧⅨⅩ" },
    { L"분수/첨자 (ㅊ)", L"½⅓⅔¼¾⅛⅜⅝⅞¹²³⁴ⁿ₁₂₃₄" },
    { L"한글 자모 (ㅋ)", L"ㄱㄲㄳㄴㄵㄶㄷㄸㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅃㅄㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎㅏㅐㅑㅒㅓㅔㅕㅖㅗㅘㅛㅙㅜㅝㅠㅝㅞㅟㅠㅡㅢㅣ" },
    { L"옛 한글 (ㅌ)", L"ㅥㅦㅧㅨㅩㅪㅫㅬㅭㅮㅯㅰㅱㅲㅳㅴㅵㅶㅷㅸㅹㅺㅻㅼㅽㅾㅿㆀㆁㆂㆃㆄㆅㆆㆇㆈㆉㆊㆋㆌㆍㆎ" },
    { L"그리스 문자 (ㅎ)", L"ΑΒΓΔΕΖΗΘΙΚΛΜΝΞΟΠΡΣΤΥΦΧΨΩαβγδεζηθικλμνξοπρστυφχψω" }
};

const int kSpecialCategoryCount = sizeof(kSpecialSymbols) / sizeof(kSpecialSymbols[0]);
// ==============================================
// 로그 창 유틸리티 함수
// ==============================================
int GetLogVisibleLineCount(HWND hwndLog)
{
    RECT rc = {};
    GetClientRect(hwndLog, &rc);

    int firstVisible = (int)SendMessageW(hwndLog, EM_GETFIRSTVISIBLELINE, 0, 0);
    int firstChar = (int)SendMessageW(hwndLog, EM_LINEINDEX, firstVisible, 0);
    int nextChar = (int)SendMessageW(hwndLog, EM_LINEINDEX, firstVisible + 1, 0);

    if (firstChar < 0 || nextChar < 0 || nextChar == firstChar)
        return 1;

    POINTL pt1 = {};
    POINTL pt2 = {};
    SendMessageW(hwndLog, EM_POSFROMCHAR, (WPARAM)&pt1, firstChar);
    SendMessageW(hwndLog, EM_POSFROMCHAR, (WPARAM)&pt2, nextChar);

    int lineHeight = (int)(pt2.y - pt1.y);
    if (lineHeight <= 0)
        lineHeight = 18;

    int visible = (rc.bottom - rc.top) / lineHeight;
    if (visible < 1)
        visible = 1;
    return visible;
}

bool IsLogAtBottom(HWND hwndLog)
{
    int firstVisible = (int)SendMessageW(hwndLog, EM_GETFIRSTVISIBLELINE, 0, 0);
    int visibleCount = GetLogVisibleLineCount(hwndLog);
    int totalLines = (int)SendMessageW(hwndLog, EM_GETLINECOUNT, 0, 0);
    return (firstVisible + visibleCount >= totalLines - 1);
}

// ==============================================
// 로그 찾기 핵심 엔진
// ==============================================
bool PerformLogFind(HWND hwndLog, bool reverseOverride, bool useReverseOverride)
{
    if (!g_app || !g_app->termBuffer || g_findState.query.empty())
        return false;

    bool up = useReverseOverride ? reverseOverride : g_findState.directionUp;

    // 검색어 준비 (대소문자 구분이 꺼져있으면 전부 소문자로 변환)
    std::wstring query = g_findState.query;
    if (!g_findState.matchCase) {
        std::transform(query.begin(), query.end(), query.begin(), ::towlower);
    }

    std::lock_guard<std::recursive_mutex> lock(g_app->termBuffer->mtx);

    int totalLines = (int)g_app->termBuffer->history.size() + g_app->termBuffer->height;

    // 터미널 버퍼 전체를 하나의 긴 문자열로 펼치고, 각 글자의 (X, Y) 좌표를 기억
    std::wstring fullText;
    struct CharMap { int x, y; };
    std::vector<CharMap> cmap;

    for (int y = 0; y < totalLines; ++y) {
        for (int x = 0; x < g_app->termBuffer->width; ++x) {
            TerminalCell c;
            if (y < (int)g_app->termBuffer->history.size()) c = g_app->termBuffer->history[y][x];
            else c = g_app->termBuffer->cells[(y - (int)g_app->termBuffer->history.size()) * g_app->termBuffer->width + x];

            if (!c.isWideTrailer) {
                wchar_t ch = c.ch;
                if (!g_findState.matchCase) ch = std::towlower(ch);
                fullText.push_back(ch);
                cmap.push_back({ x, y });
            }
        }
        fullText.push_back(L'\n'); // 줄바꿈 구분자
        cmap.push_back({ g_app->termBuffer->width - 1, y });
    }

    int startIdx = 0;

    // "처음부터 찾기"가 아니고 현재 블럭 지정(선택)된 텍스트가 있다면, 그 위치부터 검색 시작!
    if (!g_findState.fromStart && g_app->termBuffer->hasSelection) {
        int targetX = up ? g_app->termBuffer->selStartX : g_app->termBuffer->selEndX;
        int targetY = up ? g_app->termBuffer->selStartY : g_app->termBuffer->selEndY;

        // 드래그 방향에 따라 정확한 시작/끝 좌표 보정
        if (g_app->termBuffer->selStartY > g_app->termBuffer->selEndY ||
            (g_app->termBuffer->selStartY == g_app->termBuffer->selEndY && g_app->termBuffer->selStartX > g_app->termBuffer->selEndX)) {
            targetX = up ? g_app->termBuffer->selEndX : g_app->termBuffer->selStartX;
            targetY = up ? g_app->termBuffer->selEndY : g_app->termBuffer->selStartY;
        }

        for (size_t i = 0; i < cmap.size(); ++i) {
            if (cmap[i].y > targetY || (cmap[i].y == targetY && cmap[i].x >= targetX)) {
                startIdx = (int)i;
                break;
            }
        }
        if (up) startIdx--;
        else startIdx++;
    }
    else if (up) {
        startIdx = (int)fullText.length() - 1;
    }

    size_t foundPos = std::wstring::npos;

    // C++ 표준 문자열 검색(find) 활용
    if (up) {
        if (startIdx >= 0 && startIdx < (int)fullText.length()) {
            foundPos = fullText.rfind(query, startIdx); // 위로 찾기 (역방향)
        }
    }
    else {
        if (startIdx >= 0 && startIdx < (int)fullText.length()) {
            foundPos = fullText.find(query, startIdx); // 아래로 찾기 (정방향)
        }
    }

    // ★ 검색어를 찾았다면!
    if (foundPos != std::wstring::npos) {
        int startX = cmap[foundPos].x;
        int startY = cmap[foundPos].y;

        size_t endIdx = foundPos + query.length() - 1;
        if (endIdx >= cmap.size()) endIdx = cmap.size() - 1;

        int endX = cmap[endIdx].x;
        int endY = cmap[endIdx].y;

        // 찾은 글자에 자동으로 은색 블럭(선택)을 씌움
        g_app->termBuffer->SetSelectionStart(startX, startY);
        g_app->termBuffer->SetSelectionEnd(endX, endY);

        int viewTop = (int)g_app->termBuffer->history.size() - g_app->termBuffer->scrollOffset;
        int viewBottom = viewTop + g_app->termBuffer->height - 1;

        // 찾은 글자가 현재 화면 밖에 있다면(과거 로그라면), 그곳으로 자동 스크롤(점프)!
        if (startY < viewTop || startY > viewBottom) {
            g_app->termBuffer->scrollOffset = (int)g_app->termBuffer->history.size() - startY + (g_app->termBuffer->height / 2);
            if (g_app->termBuffer->scrollOffset < 0) g_app->termBuffer->scrollOffset = 0;
            if (g_app->termBuffer->scrollOffset > (int)g_app->termBuffer->history.size()) g_app->termBuffer->scrollOffset = (int)g_app->termBuffer->history.size();
        }

        InvalidateRect(hwndLog, nullptr, TRUE);
        return true;
    }

    return false;
}

// ==============================================
// 찾기 대화상자 폰트 관리
// ==============================================

HFONT EnsureFindDialogFont(HWND hwndRef)
{
    if (g_hFontFindDialog)
        return g_hFontFindDialog;

    LOGFONTW lf = {};

    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);

    lf.lfHeight = ncm.lfMenuFont.lfHeight;
    lf.lfWeight = ncm.lfMenuFont.lfWeight;
    lf.lfCharSet = ncm.lfMenuFont.lfCharSet;
    lf.lfOutPrecision = ncm.lfMenuFont.lfOutPrecision;
    lf.lfClipPrecision = ncm.lfMenuFont.lfClipPrecision;
    lf.lfQuality = ncm.lfMenuFont.lfQuality;
    lf.lfPitchAndFamily = ncm.lfMenuFont.lfPitchAndFamily;
    lstrcpynW(lf.lfFaceName, ncm.lfMenuFont.lfFaceName, LF_FACESIZE);

    g_hFontFindDialog = CreateFontIndirectW(&lf);
    return g_hFontFindDialog;
}

static LRESULT CALLBACK QuickConnectProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) 
{
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = GetPopupUIFont(hwnd);
        // ★ 입력 예시 문구 변경
        CreateWindowExW(0, L"STATIC", L"연결할 주소나 IP를 입력하세요. (123.123.123.123 9999)", WS_CHILD | WS_VISIBLE, 12, 12, 380, 20, hwnd, nullptr, 0, 0);

        HWND hCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL | WS_TABSTOP, 12, 35, 230, 200, hwnd, (HMENU)(UINT_PTR)ID_QUICK_COMBO, 0, 0);

        // ★ 문자셋 드롭다운 추가
        HWND hCharset = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP, 250, 35, 138, 100, hwnd, (HMENU)(UINT_PTR)ID_QUICK_CHARSET, 0, 0);
        SendMessageW(hCharset, CB_ADDSTRING, 0, (LPARAM)L"UTF-8");
        SendMessageW(hCharset, CB_ADDSTRING, 0, (LPARAM)L"EUC-KR (CP949)");

        // 주소만 보이도록 히스토리 채우고, 문자셋 데이터(ItemData) 연결
        for (const auto& item : g_app->quickConnectHistory) {
            int idx = (int)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)item.first.c_str());
            SendMessageW(hCombo, CB_SETITEMDATA, idx, item.second);
        }

        if (!g_app->quickConnectHistory.empty()) {
            SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
            SendMessageW(hCharset, CB_SETCURSEL, g_app->quickConnectHistory[0].second, 0);
        }
        else {
            SendMessageW(hCharset, CB_SETCURSEL, 1, 0);
        }

        CreateWindowExW(0, L"STATIC", L"기록 삭제: Shift+Del", WS_CHILD | WS_VISIBLE, 12, 82, 150, 20, hwnd, nullptr, 0, 0);

        CreateWindowExW(0, L"BUTTON", L"접속", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP, 200, 75, 90, 30, hwnd, (HMENU)(UINT_PTR)IDOK, 0, 0);
        CreateWindowExW(0, L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 300, 75, 90, 30, hwnd, (HMENU)(UINT_PTR)IDCANCEL, 0, 0);

        EnumChildWindows(hwnd, [](HWND c, LPARAM f) { SendMessage(c, WM_SETFONT, f, TRUE); return TRUE; }, (LPARAM)hFont);
        SetFocus(hCombo);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
    }
    case WM_SYSCHAR:  // ← ALT + 단축키 처리 추가
    {
        wchar_t ch = towlower((wchar_t)wParam);
        if (ch == 's') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0); return 0; }
        if (ch == 'c') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0); return 0; }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            wchar_t buf[512];
            GetWindowTextW(GetDlgItem(hwnd, ID_QUICK_COMBO), buf, 512);

            std::wstring addr = Trim(buf);
            int charset = (int)SendMessageW(GetDlgItem(hwnd, ID_QUICK_CHARSET), CB_GETCURSEL, 0, 0);

            if (!addr.empty()) {
                // 중복 제거 후 맨 앞으로 기록
                auto it = std::remove_if(g_app->quickConnectHistory.begin(), g_app->quickConnectHistory.end(),
                    [&](const auto& pair) { return pair.first == addr; });

                if (it != g_app->quickConnectHistory.end())
                    g_app->quickConnectHistory.erase(it, g_app->quickConnectHistory.end());

                g_app->quickConnectHistory.insert(g_app->quickConnectHistory.begin(), { addr, charset });
                SaveQuickConnectHistory();

                KillTimer(g_app->hwndMain, ID_TIMER_AUTORECONNECT);
                KillTimer(g_app->hwndMain, ID_TIMER_SWITCH_QUICK_CONNECT);

                // buildfix38: 빠른연결도 기존 세션을 먼저 종료합니다.
                // 특히 빠른연결은 #session new 를 쓰기 때문에 기존 new 세션이 남아 있으면
                // 다음 접속이 같은 세션명 충돌로 실패하거나 이전 세션이 살아 있을 수 있습니다.
                bool zapped = ZapKnownTinTinSession();

                // 빠른 연결은 전역 자동 로그인 설정을 접속 후 60초 동안만 검사합니다.
                StartAutoLoginWindowFromGlobal();

                // 빠른 연결은 주소록 세션이 아니므로 activeSession은 비움
                g_app->hasActiveSession = false;

                // 문자셋 설정 먼저 전송
                std::wstring charsetCmd = (charset == 1)
                    ? L"#CONFIG {CHARSET} {CP949TOUTF8}"
                    : L"#CONFIG {CHARSET} {UTF-8}";
                std::wstring sessionCmd = L"#session new " + addr;

                if (zapped) {
                    // #zap 처리 후 바로 같은 이름(new)으로 #session을 열면 충돌할 수 있으므로
                    // 주소록 전환과 같은 방식으로 잠깐 늦춰 실행합니다.
                    g_app->pendingQuickCharsetCommand = charsetCmd;
                    g_app->pendingQuickConnectCommand = sessionCmd;
                    g_app->hasPendingQuickConnect = true;
                    SetTimer(g_app->hwndMain, ID_TIMER_SWITCH_QUICK_CONNECT, 500, nullptr);
                }
                else {
                    SendRawCommandToMud(charsetCmd);
                    SendRawCommandToMud(sessionCmd);
                    MarkKnownTinTinSession(L"new");
                }

                // StartAutoLoginWindowFromGlobal()에서 이미 초기화했습니다.
            }

            DestroyWindow(hwnd);
        }
        else if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd);
        }
        else if (LOWORD(wParam) == ID_QUICK_COMBO) {
            // 리스트에서 선택을 바꿀 때 문자셋 자동 동기화
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                int sel = (int)SendMessageW((HWND)lParam, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) {
                    int charset = (int)SendMessageW((HWND)lParam, CB_GETITEMDATA, sel, 0);
                    SendMessageW(GetDlgItem(hwnd, ID_QUICK_CHARSET), CB_SETCURSEL, charset, 0);
                }
            }
            // ★ Shift+Del 처리 (삭제 로직)
            else if (HIWORD(wParam) == 0x9999) {
                HWND hCombo = GetDlgItem(hwnd, ID_QUICK_COMBO);
                wchar_t buf[512] = { 0 }; GetWindowTextW(hCombo, buf, 512);
                std::wstring text = Trim(buf);
                if (!text.empty()) {
                    int idx = (int)SendMessageW(hCombo, CB_FINDSTRINGEXACT, -1, (LPARAM)text.c_str());
                    if (idx != CB_ERR) {
                        SendMessageW(hCombo, CB_DELETESTRING, idx, 0);
                        SetWindowTextW(hCombo, L"");
                        auto it = std::remove_if(g_app->quickConnectHistory.begin(), g_app->quickConnectHistory.end(),
                            [&](const auto& pair) { return pair.first == text; });
                        if (it != g_app->quickConnectHistory.end()) {
                            g_app->quickConnectHistory.erase(it, g_app->quickConnectHistory.end());
                            SaveQuickConnectHistory();
                        }
                    }
                }
            }
        }
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowQuickConnectDialog(HWND owner)
{
    static const wchar_t* kClass = L"TTGuiQuickConnectClass";
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = QuickConnectProc;
        wc.hInstance = GetModuleHandle(0);
        wc.lpszClassName = kClass;
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    RECT rc; GetWindowRect(owner, &rc);
    int w = 420, h = 160;
    int x = rc.left + (rc.right - rc.left - w) / 2;
    int y = rc.top + (rc.bottom - rc.top - h) / 2;

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"빠른 연결",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h, owner, nullptr, GetModuleHandle(0), nullptr);

    if (!hwnd) return;

    EnableWindow(owner, FALSE);
    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
        // ★ 여기서 Shift+Del 입력을 가로채서 콤보박스 아이템 삭제 신호를 보냄!
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_DELETE && (GetKeyState(VK_SHIFT) & 0x8000)) {
            HWND hCombo = GetDlgItem(hwnd, ID_QUICK_COMBO);
            if (GetFocus() == hCombo || GetParent(GetFocus()) == hCombo) {
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_QUICK_COMBO, 0x9999), 0);
                continue;
            }
        }
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
}

void ShowChatCaptureDialog(HWND owner) 
{
    static const wchar_t* kClass = L"TTGuiChatCaptureClass";
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = { 0 }; wc.lpfnWndProc = ChatCaptureDialogProc; wc.hInstance = GetModuleHandle(0);
        wc.lpszClassName = kClass; wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassW(&wc); reg = true;
    }
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"채팅 캡처 패턴 설정", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 0, 0, 580, 440, owner, nullptr, GetModuleHandle(0), nullptr);
    RECT rcO, rcD; GetWindowRect(owner, &rcO); GetWindowRect(hDlg, &rcD);
    SetWindowPos(hDlg, 0, rcO.left + (rcO.right - rcO.left - 580) / 2, rcO.top + (rcO.bottom - rcO.top - 440) / 2, 0, 0, SWP_NOSIZE);

    EnableWindow(owner, FALSE);
    MSG msg; while (IsWindow(hDlg) && GetMessageW(&msg, 0, 0, 0)) { if (!IsDialogMessageW(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    EnableWindow(owner, TRUE); SetActiveWindow(owner);
}

void ProcessChatCaptureApply(HWND hwnd) {
    for (int i = 0; i < 10; ++i) {
        wchar_t patBuf[1024] = { 0 }, fmtBuf[1024] = { 0 };
        GetWindowTextW(GetDlgItem(hwnd, ID_CAP_PAT_BASE + i), patBuf, 1024);
        GetWindowTextW(GetDlgItem(hwnd, ID_CAP_FMT_BASE + i), fmtBuf, 1024);

        g_chatCaptures[i].pattern = Trim(patBuf);
        g_chatCaptures[i].format = Trim(fmtBuf);
        g_chatCaptures[i].active = (SendMessageW(GetDlgItem(hwnd, ID_CAP_CHK_BASE + i), BM_GETCHECK, 0, 0) == BST_CHECKED);
    }
    SaveChatCaptureSettings();
}

static LRESULT CALLBACK ChatCaptureDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = GetPopupUIFont(hwnd);

        CreateWindowExW(0, L"STATIC", L"사용", WS_CHILD | WS_VISIBLE, 15, 15, 35, 20, hwnd, nullptr, 0, nullptr);
        CreateWindowExW(0, L"STATIC", L"인식할 패턴 (예: %1가 %2 라고 말합니다.)", WS_CHILD | WS_VISIBLE, 60, 15, 240, 20, hwnd, nullptr, 0, nullptr);
        CreateWindowExW(0, L"STATIC", L"채팅 캡쳐창 출력 형태 (예: [대화] %1: %2)", WS_CHILD | WS_VISIBLE, 310, 15, 240, 20, hwnd, nullptr, 0, nullptr);

        for (int i = 0; i < 10; ++i) {
            int y = 40 + (i * 30);
            HWND hChk = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 20, y + 2, 20, 20, hwnd, (HMENU)(UINT_PTR)(ID_CAP_CHK_BASE + i), 0, nullptr);
            SendMessageW(hChk, BM_SETCHECK, g_chatCaptures[i].active ? BST_CHECKED : BST_UNCHECKED, 0);

            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_chatCaptures[i].pattern.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 60, y, 240, 24, hwnd, (HMENU)(UINT_PTR)(ID_CAP_PAT_BASE + i), 0, nullptr);
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_chatCaptures[i].format.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 310, y, 240, 24, hwnd, (HMENU)(UINT_PTR)(ID_CAP_FMT_BASE + i), 0, nullptr);
        }

        int by = 360;
        CreateWindowExW(0, L"BUTTON", L"초기화(X)", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 15, by, 80, 30, hwnd, (HMENU)(UINT_PTR)ID_CAP_BTN_RESET, 0, nullptr);
        CreateWindowExW(0, L"BUTTON", L"적용(A)", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 375, by, 80, 30, hwnd, (HMENU)(UINT_PTR)ID_CAP_BTN_APPLY, 0, nullptr);
        CreateWindowExW(0, L"BUTTON", L"닫기(C)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 470, by, 80, 30, hwnd, (HMENU)(UINT_PTR)IDCANCEL, 0, nullptr);

        EnumChildWindows(hwnd, [](HWND c, LPARAM f) { SendMessage(c, WM_SETFONT, f, TRUE); return TRUE; }, (LPARAM)hFont);
        return 0;
    }
    case WM_SYSCHAR:  // ← ALT + 단축키 처리 추가
    {
        wchar_t ch = towlower((wchar_t)wParam);
        if (ch == 'x') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_CAP_BTN_RESET, BN_CLICKED), 0); return 0; }
        if (ch == 'a') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_CAP_BTN_APPLY, BN_CLICKED), 0); return 0; }
        if (ch == 'c') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0); return 0; }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_CAP_BTN_APPLY) {
            ProcessChatCaptureApply(hwnd);
        }
        else if (id == IDCANCEL) {
            ProcessChatCaptureApply(hwnd); // 닫을 때 자동 저장
            DestroyWindow(hwnd);
        }
        else if (id == ID_CAP_BTN_RESET) {
            for (int i = 0; i < 10; ++i) {
                SetWindowTextW(GetDlgItem(hwnd, ID_CAP_PAT_BASE + i), L"");
                SetWindowTextW(GetDlgItem(hwnd, ID_CAP_FMT_BASE + i), L"");
                SendMessageW(GetDlgItem(hwnd, ID_CAP_CHK_BASE + i), BM_SETCHECK, BST_UNCHECKED, 0);
            }
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK FindDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HFONT hFont = EnsureFindDialogFont(hwnd);

        HWND hLabel = CreateWindowExW(
            0, L"STATIC", L"찾을 글월(&W):",
            WS_CHILD | WS_VISIBLE,
            12, 14, 82, 20,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

        HWND hEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", g_findState.query.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            98, 12, 250, 24,
            hwnd, (HMENU)(INT_PTR)ID_FIND_EDIT, GetModuleHandleW(nullptr), nullptr);

        HWND hFindBtn = CreateWindowExW(
            0, L"BUTTON", L"찾기(&S)",
            // ★ BS_DEFPUSHBUTTON 추가: 엔터키를 누르면 이 버튼이 눌린 것으로 간주됨
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            360, 10, 86, 26,
            hwnd, (HMENU)(INT_PTR)ID_FIND_BUTTON, GetModuleHandleW(nullptr), nullptr);

        HWND hCancelBtn = CreateWindowExW(
            0, L"BUTTON", L"취소(&C)",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            360, 42, 86, 26,
            hwnd, (HMENU)(INT_PTR)ID_FIND_CANCEL, GetModuleHandleW(nullptr), nullptr);

        HWND hMatchCase = CreateWindowExW(
            0, L"BUTTON", L"대/소문자 일치(&M)",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            12, 48, 150, 22,
            hwnd, (HMENU)(INT_PTR)ID_FIND_MATCHCASE, GetModuleHandleW(nullptr), nullptr);

        HWND hFromStart = CreateWindowExW(
            0, L"BUTTON", L"처음부터 찾기(&T)",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            12, 74, 130, 22,
            hwnd, (HMENU)(INT_PTR)ID_FIND_FROM_START, GetModuleHandleW(nullptr), nullptr);

        HWND hUp = CreateWindowExW(
            0, L"BUTTON", L"위로(&U)",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
            190, 48, 72, 22,
            hwnd, (HMENU)(INT_PTR)ID_FIND_UP, GetModuleHandleW(nullptr), nullptr);

        HWND hDown = CreateWindowExW(
            0, L"BUTTON", L"아래로(&D)",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
            270, 48, 82, 22,
            hwnd, (HMENU)(INT_PTR)ID_FIND_DOWN, GetModuleHandleW(nullptr), nullptr);

        HWND ctrls[] = { hLabel, hEdit, hFindBtn, hCancelBtn, hMatchCase, hFromStart, hUp, hDown };
        for (HWND h : ctrls)
        {
            if (h)
                SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        }

        SendMessageW(hMatchCase, BM_SETCHECK, g_findState.matchCase ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(hFromStart, BM_SETCHECK, g_findState.fromStart ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(GetDlgItem(hwnd, g_findState.directionUp ? ID_FIND_UP : ID_FIND_DOWN),
            BM_SETCHECK, BST_CHECKED, 0);

        SetFocus(hEdit);
        SendMessageW(hEdit, EM_SETSEL, 0, -1);
        return 0;
    }
    case WM_SYSCHAR:  // ← ALT + 단축키 처리 추가
    {
        wchar_t ch = towlower((wchar_t)wParam);
        if (ch == 's') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_FIND_BUTTON, BN_CLICKED), 0); return 0; }
        if (ch == 'c') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_FIND_CANCEL, BN_CLICKED), 0); return 0; }
        if (ch == 'm') { SendMessageW(GetDlgItem(hwnd, ID_FIND_MATCHCASE), BM_SETCHECK, !SendMessageW(GetDlgItem(hwnd, ID_FIND_MATCHCASE), BM_GETCHECK, 0, 0), 0); return 0; }
        if (ch == 't') { SendMessageW(GetDlgItem(hwnd, ID_FIND_FROM_START), BM_SETCHECK, !SendMessageW(GetDlgItem(hwnd, ID_FIND_FROM_START), BM_GETCHECK, 0, 0), 0); return 0; }
        if (ch == 'u') { SendMessageW(GetDlgItem(hwnd, ID_FIND_UP), BM_SETCHECK, BST_CHECKED, 0); return 0; }
        if (ch == 'd') { SendMessageW(GetDlgItem(hwnd, ID_FIND_DOWN), BM_SETCHECK, BST_CHECKED, 0); return 0; }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_FIND_BUTTON:
        case IDOK: // ★ 엔터키 입력 처리
        {
            wchar_t buf[512] = {};
            GetWindowTextW(GetDlgItem(hwnd, ID_FIND_EDIT), buf, 512);

            g_findState.query = buf;
            g_findState.matchCase =
                (SendMessageW(GetDlgItem(hwnd, ID_FIND_MATCHCASE), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_findState.fromStart =
                (SendMessageW(GetDlgItem(hwnd, ID_FIND_FROM_START), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_findState.directionUp =
                (SendMessageW(GetDlgItem(hwnd, ID_FIND_UP), BM_GETCHECK, 0, 0) == BST_CHECKED);

            if (!PerformLogFind(g_app ? g_app->hwndLog : nullptr, false, false))
            {
                MessageBoxW(hwnd, L"찾는 글월이 없습니다.", L"찾기", MB_OK | MB_ICONINFORMATION);
            }

            g_findState.fromStart = false;
            SendMessageW(GetDlgItem(hwnd, ID_FIND_FROM_START), BM_SETCHECK, BST_UNCHECKED, 0);
            return 0;
        }

        case ID_FIND_CANCEL:
        case IDCANCEL: // ★ ESC 키 입력 처리
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

    case WM_DESTROY:
        g_findState.hwndDialog = nullptr;
        g_findState.dialogOpen = false;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowFindDialog(HWND owner)
{
    // 이미 창이 만들어져 있는 경우
    if (g_findState.hwndDialog && IsWindow(g_findState.hwndDialog))
    {
        // ★ 창이 이미 있더라도 확실하게 중앙으로 다시 끌어옵니다!
        RECT rcOwner = {};
        GetWindowRect(owner, &rcOwner);
        int dlgW = 470;
        int dlgH = 145;
        int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - dlgW) / 2;
        int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - dlgH) / 2;

        SetWindowPos(g_findState.hwndDialog, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);

        ShowWindow(g_findState.hwndDialog, SW_SHOWNORMAL);
        SetForegroundWindow(g_findState.hwndDialog);
        SetFocus(GetDlgItem(g_findState.hwndDialog, ID_FIND_EDIT));
        return;
    }

    static const wchar_t* kFindDialogClass = L"TTGuiFindDialogClass";
    static bool s_registered = false;

    if (!s_registered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = FindDialogProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kFindDialogClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        s_registered = true;
    }

    RECT rcOwner = {};
    GetWindowRect(owner, &rcOwner);

    int dlgW = 470;
    int dlgH = 145;

    // ★ 새로 창을 만들 때 세로 높이(y)를 정중앙으로 계산합니다! (이전의 y = rcOwner.top + 40; 제거)
    int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - dlgW) / 2;
    int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - dlgH) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kFindDialogClass,
        L"찾고 싶은 글월을 입력해 주세요.",
        WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE,
        x, y, dlgW, dlgH,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    g_findState.hwndDialog = hwnd;
    g_findState.dialogOpen = (hwnd != nullptr);
}

static LRESULT CALLBACK SymbolDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        // [기존 코드 유지: 폰트 설정 및 리스트박스/버튼 생성]
        HFONT hFont = GetPopupUIFont(hwnd);

        HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | WS_VSCROLL |
            LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
            10, 10, 140, 280, hwnd, (HMENU)(UINT_PTR)ID_SYMBOL_LIST, GetModuleHandle(0), nullptr);

        for (int i = 0; i < kSpecialCategoryCount; ++i) {
            SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)kSpecialSymbols[i].name);
        }

        for (int i = 0; i < 100; ++i) {
            int x = 160 + (i % 10) * 28;
            int y = 10 + (i / 10) * 28;
            CreateWindowExW(0, L"BUTTON", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_FLAT, // WS_VISIBLE 추가 확인
                x, y, 26, 26, hwnd, (HMENU)(UINT_PTR)(ID_SYMBOL_BTN_BASE + i), GetModuleHandle(0), nullptr);
        }

        CreateWindowExW(0, L"BUTTON", L"닫기(&C)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            360, 300, 80, 30, hwnd, (HMENU)(UINT_PTR)IDCANCEL, GetModuleHandle(0), nullptr);

        EnumChildWindows(hwnd, [](HWND c, LPARAM f) { SendMessage(c, WM_SETFONT, f, TRUE); return TRUE; }, (LPARAM)hFont);
        SendMessageW(hList, LB_SETCURSEL, 0, 0);
        SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_SYMBOL_LIST, LBN_SELCHANGE), (LPARAM)hList);
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_MEASUREITEM:
    {
        MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*)lParam;
        if (mis && mis->CtlID == ID_SYMBOL_LIST)
        {
            mis->itemHeight = 20;   // 여기 숫자로 행간 조절
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (!dis || dis->CtlID != ID_SYMBOL_LIST)
            break;

        if ((int)dis->itemID < 0)
            return TRUE;

        wchar_t text[256] = {};
        SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, (LPARAM)text);

        bool selected = (dis->itemState & ODS_SELECTED) != 0;

        COLORREF bg = selected ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_WINDOW);
        COLORREF fg = selected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_WINDOWTEXT);

        HBRUSH hbr = CreateSolidBrush(bg);
        FillRect(dis->hDC, &dis->rcItem, hbr);
        DeleteObject(hbr);

        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, fg);

        RECT rcText = dis->rcItem;
        rcText.left += 6;  // 왼쪽 여백
        DrawTextW(dis->hDC, text, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if (dis->itemState & ODS_FOCUS)
            DrawFocusRect(dis->hDC, &dis->rcItem);

        return TRUE;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);

        if (id == IDOK || id == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }

        if (id == ID_SYMBOL_LIST && HIWORD(wParam) == LBN_SELCHANGE) {
            // [기존 리스트박스 갱신 로직 유지]
            int sel = (int)SendMessageW((HWND)lParam, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < kSpecialCategoryCount) {
                const wchar_t* syms = kSpecialSymbols[sel].symbols;
                size_t len = wcslen(syms);
                for (int i = 0; i < 100; ++i) {
                    HWND hBtn = GetDlgItem(hwnd, ID_SYMBOL_BTN_BASE + i);
                    if (i < (int)len) {
                        wchar_t single[2] = { syms[i], L'\0' };
                        SetWindowTextW(hBtn, single);
                        ShowWindow(hBtn, SW_SHOW);
                    }
                    else {
                        ShowWindow(hBtn, SW_HIDE);
                    }
                }
            }
            return 0;
        }

        if (id >= ID_SYMBOL_BTN_BASE && id < ID_SYMBOL_BTN_BASE + 100) {
            wchar_t btnText[8] = { 0 };
            GetWindowTextW((HWND)lParam, btnText, 8);

            if (btnText[0] != L'\0') {
                HWND targetEdit = (g_app && IsWindow(g_app->hwndTargetEdit)) ? g_app->hwndTargetEdit : nullptr;
                if (!targetEdit && g_app) targetEdit = g_app->hwndEdit[g_app->activeEditIndex];

                if (targetEdit && IsWindow(targetEdit)) {
                    // 1. 메모장 데이터 동기화
                    if (g_memo.hwndEdit && targetEdit == g_memo.hwndEdit) {
                        g_memo.lastSymbol = btnText;
                        UpdateMemoStatus();
                    }

                    // 2. 글자 삽입
                    SendMessageW(targetEdit, EM_REPLACESEL, TRUE, (LPARAM)btnText);

                    // ★ 핵심 수정 구간 ★

                    // 3. 먼저 메모장에 포커스를 줍니다. 
                    // (이때 메모장이 앞으로 튀어나오려고 시도합니다.)
                    SetFocus(targetEdit);

                    // 4. 그 직후에 특수기호 창(hwnd)을 다시 맨 위로 끌어올립니다.
                    // SWP_NOACTIVATE를 써야 특수기호 창이 포커스를 뺏어오지 않으면서(메모장에 커서 유지) 
                    // 화면상으로만 맨 위에 있게 됩니다.
                    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
            return 0;
        }

        if (id == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;
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

        // ★ 해결책 3: 창이 파괴될 때 핸들을 확실히 비워주기 (F4 두 번 눌러야 하는 문제 해결)
    case WM_DESTROY:
        if (g_app) g_app->hwndSymbolDialog = nullptr;
        return 0;
    case WM_SYSCHAR:
    {
        if (wParam == 'c' || wParam == 'C')
        {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowSymbolDialog(HWND owner)
{
    // 1. 토글 로직: 이미 핸들이 존재하면 닫기
    if (g_app && g_app->hwndSymbolDialog && IsWindow(g_app->hwndSymbolDialog)) {
        DestroyWindow(g_app->hwndSymbolDialog);
        // g_app->hwndSymbolDialog = nullptr; // WM_DESTROY에서 처리되므로 안전함
        return;
    }

    // 2. 현재 포커스가 있는 창 저장 (메모장인지 입력창인지)
    HWND hRealFocus = GetFocus();
    if (g_app) g_app->hwndTargetEdit = hRealFocus;

    // 3. 클래스 등록 (최초 1회)
    static const wchar_t* kClass = L"TTGuiSymbolClass";
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = SymbolDialogProc;
        wc.hInstance = GetModuleHandle(0);
        wc.lpszClassName = kClass;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        reg = true;
    }

    // ★ 해결책 4: 주 GUI(owner)의 중앙 좌표 계산
    RECT rcMain;
    if (owner && IsWindow(owner)) {
        GetWindowRect(owner, &rcMain); // 주 창 위치 가져오기
    }
    else {
        // 혹시 owner가 없으면 화면 중앙
        GetWindowRect(GetDesktopWindow(), &rcMain);
    }

    int w = 465, h = 380;
    int x = rcMain.left + (rcMain.right - rcMain.left - w) / 2;
    int y = rcMain.top + (rcMain.bottom - rcMain.top - h) / 2;

    // 4. 창 생성: WS_EX_TOPMOST를 주어 메모장 뒤로 숨지 않게 함
    HWND hDlg = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_DLGMODALFRAME,
        kClass, L"특수 기호 (F4)",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h,
        owner, nullptr, GetModuleHandle(0), nullptr
    );

    if (g_app) g_app->hwndSymbolDialog = hDlg;
}

static LRESULT CALLBACK InfoPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HWND hTitle = nullptr;
    static HWND hText = nullptr;
    static HWND hBtn = nullptr;
    static HFONT hFontUi = nullptr;
    static HBRUSH hbrBack = nullptr;
    static HBRUSH hbrPanel = nullptr;

    switch (msg)
    {
    case WM_CREATE:
    {
        ApplyPopupTitleBarTheme(hwnd);

        hbrBack = CreateSolidBrush(RGB(32, 34, 37));
        hbrPanel = CreateSolidBrush(RGB(43, 45, 49));

        hFontUi = GetPopupUIFont(hwnd);

        hTitle = CreateWindowExW(
            0, L"STATIC", L"Ktin: TinTin++ GUI Client",
            WS_CHILD | WS_VISIBLE,
            20, 20, 300, 30,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

        // 1. 방금 만든 함수로 버전을 읽어와 텍스트를 조립합니다.
        std::wstring versionText = L"버전 " + GetAppVersionString() + L"\r\n제작: 울보천사\r\n문의: cry1004@gmail.com";

        hText = CreateWindowExW(
            0, L"STATIC",
            versionText.c_str(), // 2. 조립된 텍스트 변수를 여기에 쏙!
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            20, 60, 360, 80,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

        hBtn = CreateWindowExW(
            0, L"BUTTON", L"확인(&O)",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            300, 140, 80, 30,
            hwnd, (HMENU)IDOK, GetModuleHandleW(nullptr), nullptr);

        SendMessageW(hTitle, WM_SETFONT, (WPARAM)hFontUi, TRUE);
        SendMessageW(hText, WM_SETFONT, (WPARAM)hFontUi, TRUE);
        SendMessageW(hBtn, WM_SETFONT, (WPARAM)hFontUi, TRUE);

        return 0;
    }
    case WM_SYSCHAR:  // ← ALT + 단축키 처리 추가
    {
        wchar_t ch = towlower((wchar_t)wParam);
        if (ch == 'o') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0); return 0; }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(230, 230, 230));
        return (INT_PTR)hbrBack;
    }

    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, hbrBack);

        RECT panel = { 10, 10, rc.right - 10, rc.bottom - 10 };
        FillRect(hdc, &panel, hbrPanel);

        return 1;
    }

    case WM_DESTROY:
        DeleteObject(hbrBack);
        DeleteObject(hbrPanel);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowInfoPopup(HWND owner)
{
    static const wchar_t* cls = L"TT_INFO_POPUP";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = InfoPopupProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = cls;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    int w = 420;
    int h = 220;

    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    int x = work.left + ((work.right - work.left) - w) / 2;
    int y = work.top + ((work.bottom - work.top) - h) / 2;

    CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        cls,
        L"정보",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
}

static LRESULT CALLBACK ShortcutEditorPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_SHORTCUT_EDITOR_RESET: // 초기화 버튼
        {
            for (int i = 0; i < SHORTCUT_BUTTON_COUNT; ++i) {
                wchar_t labelText[16]; wsprintfW(labelText, L"%d", i + 1);
                SetWindowTextW(GetDlgItem(hwnd, ID_SHORTCUT_EDITOR_LABEL_BASE + i), labelText);
                SetWindowTextW(GetDlgItem(hwnd, ID_SHORTCUT_EDITOR_COMMAND_BASE + i), L"");
                SetWindowTextW(GetDlgItem(hwnd, ID_SHORTCUT_EDITOR_OFF_BASE + i), L"");
                SendMessageW(GetDlgItem(hwnd, ID_SHORTCUT_EDITOR_TOGGLE_BASE + i), BM_SETCHECK, BST_UNCHECKED, 0);
            }
            return 0;
        }

        case IDOK: // 확인 버튼
        {
            ShortcutEditorState* state = (ShortcutEditorState*)GetPropW(hwnd, L"ShortcutEditorState");
            if (g_app) {
                for (int i = 0; i < SHORTCUT_BUTTON_COUNT; ++i) {
                    wchar_t lb[256], cmdOn[1024], cmdOff[1024];

                    GetWindowTextW(GetDlgItem(hwnd, ID_SHORTCUT_EDITOR_LABEL_BASE + i), lb, 256);
                    GetWindowTextW(GetDlgItem(hwnd, ID_SHORTCUT_EDITOR_COMMAND_BASE + i), cmdOn, 1024);
                    GetWindowTextW(GetDlgItem(hwnd, ID_SHORTCUT_EDITOR_OFF_BASE + i), cmdOff, 1024);

                    bool isToggle = (SendMessageW(GetDlgItem(hwnd, ID_SHORTCUT_EDITOR_TOGGLE_BASE + i), BM_GETCHECK, 0, 0) == BST_CHECKED);

                    g_app->shortcutLabels[i] = Trim(lb).empty() ? std::to_wstring(i + 1) : Trim(lb);
                    g_app->shortcutCommands[i] = Trim(cmdOn);
                    g_app->shortcutOffCommands[i] = Trim(cmdOff);
                    g_app->shortcutIsToggle[i] = isToggle;
                    g_app->shortcutActive[i] = false; // 모드 변경 시 상태 초기화

                    if (!isToggle) {
                        g_app->shortcutActive[i] = false;
                    }
                }
            }
            if (state) state->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        case IDCANCEL: DestroyWindow(hwnd); return 0;
        }
        break;
    case WM_SYSCHAR:  // ← ALT + 단축키 처리 추가
    {
        wchar_t ch = towlower((wchar_t)wParam);
        if (ch == 'r') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_SHORTCUT_EDITOR_RESET, BN_CLICKED), 0); return 0; } // 초기화
        if (ch == 'o') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0); return 0; } // 확인
        if (ch == 'c') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0); return 0; } // 취소
        break;
    }
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: RemovePropW(hwnd, L"ShortcutEditorState"); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool PromptShortcutEditor(HWND hwnd)
{
    if (!g_app) return false;

    const wchar_t kDlgClass[] = L"TTGuiShortcutEditorPopupClass";
    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = ShortcutEditorPopupProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kDlgClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        s_registered = true;
    }

    ShortcutEditorState state; state.accepted = false;

    // 창 생성 (가로 850으로 확장)
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kDlgClass, L"단축버튼 및 토글 매크로 설정",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 850, 520, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!hDlg) return false;
    SetPropW(hDlg, L"ShortcutEditorState", &state);

    // 헤더 출력
    CreateWindowExW(0, L"STATIC", L"번호", WS_CHILD | WS_VISIBLE, 15, 16, 35, 20, hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, L"STATIC", L"버튼 이름", WS_CHILD | WS_VISIBLE, 60, 16, 80, 20, hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, L"STATIC", L"켜기(ON) 명령", WS_CHILD | WS_VISIBLE, 160, 16, 150, 20, hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, L"STATIC", L"끄기(OFF) 명령 (토글 전용)", WS_CHILD | WS_VISIBLE, 420, 16, 200, 20, hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, L"STATIC", L"토글", WS_CHILD | WS_VISIBLE, 775, 16, 40, 20, hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);

    for (int i = 0; i < SHORTCUT_BUTTON_COUNT; ++i) {
        int y = 44 + i * 32;
        wchar_t num[8]; wsprintfW(num, L"%d", i + 1);
        CreateWindowExW(0, L"STATIC", num, WS_CHILD | WS_VISIBLE, 20, y + 4, 30, 20, hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);

        // 1. 이름
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_app->shortcutLabels[i].c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 60, y, 90, 24, hDlg, (HMENU)(INT_PTR)(ID_SHORTCUT_EDITOR_LABEL_BASE + i), GetModuleHandleW(nullptr), nullptr);

        // 2. ON 명령
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_app->shortcutCommands[i].c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 160, y, 250, 24, hDlg, (HMENU)(INT_PTR)(ID_SHORTCUT_EDITOR_COMMAND_BASE + i), GetModuleHandleW(nullptr), nullptr);

        // 3. OFF 명령 (ID_SHORTCUT_EDITOR_OFF_BASE 사용)
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_app->shortcutOffCommands[i].c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 420, y, 340, 24, hDlg, (HMENU)(INT_PTR)(ID_SHORTCUT_EDITOR_OFF_BASE + i), GetModuleHandleW(nullptr), nullptr);

        // 4. 토글 체크박스
        HWND hChk = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
            785, y + 2, 20, 20, hDlg, (HMENU)(INT_PTR)(ID_SHORTCUT_EDITOR_TOGGLE_BASE + i), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(hChk, BM_SETCHECK, g_app->shortcutIsToggle[i] ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    // 하단 제어 버튼
    CreateWindowExW(0, L"BUTTON", L"초기화", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 540, 435, 90, 32, hDlg, (HMENU)(INT_PTR)ID_SHORTCUT_EDITOR_RESET, GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, L"BUTTON", L"확인", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 640, 435, 90, 32, hDlg, (HMENU)(INT_PTR)IDOK, GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 740, 435, 90, 32, hDlg, (HMENU)(INT_PTR)IDCANCEL, GetModuleHandleW(nullptr), nullptr);

    HFONT hFont = GetPopupUIFont(hDlg);
    EnumChildWindows(hDlg, [](HWND c, LPARAM lp) -> BOOL { SendMessageW(c, WM_SETFONT, lp, TRUE); return TRUE; }, (LPARAM)hFont);

    RECT rcO{}, rcD{}; GetWindowRect(hwnd, &rcO); GetWindowRect(hDlg, &rcD);
    SetWindowPos(hDlg, HWND_TOP, rcO.left + ((rcO.right - rcO.left) - (rcD.right - rcD.left)) / 2, rcO.top + ((rcO.bottom - rcO.top) - (rcD.bottom - rcD.top)) / 2, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);

    EnableWindow(hwnd, FALSE);
    MSG msg; while (IsWindow(hDlg) && GetMessageW(&msg, 0, 0, 0)) { if (!IsDialogMessageW(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    EnableWindow(hwnd, TRUE); SetActiveWindow(hwnd);

    if (state.accepted) {
        ApplyShortcutButtons(g_app->hwndMain);
        SaveShortcutSettings();
    }
    return state.accepted;
}

struct AutoSaveIntervalState { int* sec; bool accepted; };

static LRESULT CALLBACK AutoSaveIntervalProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) 
{
    AutoSaveIntervalState* state = (AutoSaveIntervalState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE: SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lParam)->lpCreateParams); return DefWindowProcW(hwnd, msg, wParam, lParam);
    case WM_CREATE: {
        state = (AutoSaveIntervalState*)((CREATESTRUCTW*)lParam)->lpCreateParams;
        ApplyPopupTitleBarTheme(hwnd);
        HFONT hF = GetPopupUIFont(hwnd);
        CreateWindowExW(0, L"STATIC", L"자동저장 간격(초):", WS_CHILD | WS_VISIBLE, 15, 20, 150, 20, hwnd, 0, 0, 0);
        HWND hEd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 15, 45, 100, 24, hwnd, (HMENU)101, 0, 0);
        CreateWindowExW(0, L"BUTTON", L"적용", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 130, 42, 60, 28, hwnd, (HMENU)IDOK, 0, 0);
        CreateWindowExW(0, L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 200, 42, 60, 28, hwnd, (HMENU)IDCANCEL, 0, 0);
        EnumChildWindows(hwnd, [](HWND c, LPARAM lp)->BOOL {SendMessageW(c, WM_SETFONT, lp, TRUE); return TRUE; }, (LPARAM)hF);
        wchar_t b[32]; wsprintfW(b, L"%d", *state->sec); SetWindowTextW(hEd, b);
        SetFocus(hEd); SendMessageW(hEd, EM_SETSEL, 0, -1);
        return 0;
    }
    case WM_SYSCHAR:  // ← ALT + 단축키 처리 추가
    {
        wchar_t ch = towlower((wchar_t)wParam);
        if (ch == 'a') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0); return 0; }
        if (ch == 'c') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0); return 0; }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            wchar_t b[32]; GetWindowTextW(GetDlgItem(hwnd, 101), b, 32);
            *state->sec = _wtoi(b); if (*state->sec < 1) *state->sec = 1;
            state->accepted = true; DestroyWindow(hwnd); return 0;
        }
        else if (LOWORD(wParam) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
        break;
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)wParam, TRANSPARENT); return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool PromptMemoAutoSaveInterval(HWND owner, int& sec) {
    static bool reg = false;
    if (!reg) { WNDCLASSW wc = { 0 }; wc.lpfnWndProc = AutoSaveIntervalProc; wc.lpszClassName = L"TTAUtoSaveInt"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassW(&wc); reg = true; }
    AutoSaveIntervalState st = { &sec, false };
    RECT rc; GetWindowRect(owner, &rc);
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"TTAUtoSaveInt", L"자동저장 설정", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, rc.left + 50, rc.top + 50, 300, 130, owner, 0, 0, &st);
    EnableWindow(owner, FALSE); MSG msg; while (IsWindow(hDlg) && GetMessageW(&msg, 0, 0, 0)) { if (!IsDialogMessageW(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    EnableWindow(owner, TRUE); SetActiveWindow(owner); return st.accepted;
}

