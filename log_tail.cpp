#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "chat_capture.h"
#include "log_tail.h"
#include "theme.h"

#include <commctrl.h>
#include <richedit.h>
#include <dwmapi.h>

#ifndef EM_SETEDITSTYLE
#define EM_SETEDITSTYLE (WM_USER + 204)
#endif
#ifndef SES_EXTENDBACKCOLOR
#define SES_EXTENDBACKCOLOR 0x00000004
#endif

#include <deque>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdlib>
#include <cstring>

static const wchar_t* kTailWndClass = L"KTinCaptureTailWindow";
static const wchar_t* kTailPatternWndClass = L"KTinTailPatternWindow";
static const wchar_t* kTailFilterWndClass = L"KTinTailFilterSettingsWindow";
static const wchar_t* kTailTabSettingsWndClass = L"KTinTailTabSettingsWindow";

struct TailFilterSettings
{
    std::wstring chat = L"[]:;<>:;잡담 :;잡담:";
    std::wstring auction = L"경매;입찰;낙찰";
    std::wstring talk = L"말합니다;말했다;말한다;귓속말;속삭;외칩니다;대화";
    std::wstring item = L"획득;얻었;얻었습니다;주웠;가져;받았습니다;손에 넣";
    std::wstring exp = L"경험치;경험;수련치;숙련도";
    std::wstring userName1 = L"사용자1";
    std::wstring userPattern1 = L"";
    std::wstring userName2 = L"사용자2";
    std::wstring userPattern2 = L"";
    std::wstring userName3 = L"사용자3";
    std::wstring userPattern3 = L"";

    bool ansiAll = false;
    bool ansiChat = false;
    bool ansiAuction = false;
    bool ansiTalk = false;
    bool ansiItem = false;
    bool ansiExp = false;
    bool ansiUser1 = false;
    bool ansiUser2 = false;
    bool ansiUser3 = false;
};

static TailFilterSettings g_tailFilters;
static bool g_tailFiltersLoaded = false;
static void LoadTailFilterSettings();

struct TailState
{
    HWND hwnd = nullptr;
    HWND hwndTab = nullptr;
    HWND hwndEdit = nullptr;
    HWND hwndStatus = nullptr;
    bool menuHidden = false;
    bool statusHidden = false;
    bool alwaysOnTop = false;
    std::vector<int> visibleModes;
    int activeMode = 0;
    std::wstring customPattern;
    std::wstring logPath;
    LONGLONG lastPos = 0;
    bool firstRead = true;
    bool startedFromMiddle = false;
    std::wstring fragment;
    std::deque<std::wstring> lines;
    int maxLines = 2000;              // RichEdit에 유지할 표시 줄 수
    LONGLONG totalMatchedLines = 0;   // 상태바에 표시할 누적 매칭 줄 수
    bool reading = false;             // tail 공통 타이머 중 재진입 방지
    WNDPROC oldEditProc = nullptr;
};

static std::vector<TailState*> g_tailWindows;
static HWND g_tailTimerOwner = nullptr;
static LOGFONTW g_lastAppliedTailFont{};
static bool g_haveLastAppliedTailFont = false;

static void TailApplyMainLogFontToEdit(HWND hwndEdit);
static void TailLayout(TailState* st);
static void TailSnapToNearbyWindows(TailState* st);
static bool TailGetVisibleWindowRect(HWND hwnd, RECT& out);
static void TailMoveWindowToVisibleRect(TailState* st, const RECT& desiredVisibleRect);

bool HasCaptureTailWindows()
{
    return !g_tailWindows.empty();
}

void CloseAllCaptureTailWindows()
{
    std::vector<HWND> windows;
    for (auto* st : g_tailWindows)
        if (st && st->hwnd && IsWindow(st->hwnd))
            windows.push_back(st->hwnd);
    for (HWND hwnd : windows)
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

void ApplyTailWindowFonts()
{
    if (!g_app || !g_app->hFontLog)
        return;

    // buildfix33:
    // 환경설정에서 "메인창 항상 위"만 바꿔도 ApplyStyles()가 호출됩니다.
    // 그때 갈무리 RichEdit 전체 서식을 다시 만지면 일부 환경에서 ANSI 글자색이
    // 검정 배경/검정 글자로 보이는 문제가 생길 수 있어, 실제 메인 출력 폰트가
    // 바뀐 경우에만 갈무리창 전체 폰트를 다시 적용합니다.
    if (g_haveLastAppliedTailFont && memcmp(&g_lastAppliedTailFont, &g_app->logStyle.font, sizeof(LOGFONTW)) == 0)
        return;
    g_lastAppliedTailFont = g_app->logStyle.font;
    g_haveLastAppliedTailFont = true;

    for (auto* st : g_tailWindows)
    {
        if (!st || !st->hwnd || !IsWindow(st->hwnd))
            continue;

        if (st->hwndEdit && IsWindow(st->hwndEdit))
            TailApplyMainLogFontToEdit(st->hwndEdit);

        if (st->hwndStatus && IsWindow(st->hwndStatus))
        {
            HFONT hStatusFont = GetPopupUIFont(st->hwndStatus);
            SendMessageW(st->hwndStatus, WM_SETFONT, (WPARAM)hStatusFont, TRUE);
            InvalidateRect(st->hwndStatus, nullptr, TRUE);
        }

        if (st->hwndTab && IsWindow(st->hwndTab))
        {
            SendMessageW(st->hwndTab, WM_SETFONT, (WPARAM)g_app->hFontLog, TRUE);
            InvalidateRect(st->hwndTab, nullptr, TRUE);
        }

        TailLayout(st);
        InvalidateRect(st->hwnd, nullptr, TRUE);
    }
}

static bool ReadTailIniBool(const wchar_t* key, bool defValue)
{
    return GetPrivateProfileIntW(L"tail_filters", key, defValue ? 1 : 0, GetSettingsPath().c_str()) != 0;
}

static bool TailAnsiEnabledForMode(int /*mode*/)
{
    // buildfix34:
    // RichEdit에 ANSI 색상을 run 단위로 계속 입히는 방식은 장시간 tail 보기에서
    // UI 스레드 정체/검은 글자/메뉴 무응답을 일으킬 수 있어 일단 비활성화합니다.
    // 갈무리 보기창은 항상 ANSI 코드를 제거한 평문으로 표시합니다.
    // ANSI 원문은 갈무리 로그 파일에는 그대로 저장되므로, 나중에 별도 GDI 렌더러 방식으로
    // 다시 구현할 수 있습니다.
    return false;
}

static void CenterPopupToOwner(HWND hwnd, HWND owner);
static void ApplyPopupFontToChildren(HWND hwnd);
static void TailApplyMainLogFontToEdit(HWND hwndEdit);

static std::wstring TailModeTitleRaw(int mode)
{
    switch (mode)
    {
    case 1: return L"잡담";
    case 2: return L"경매";
    case 3: return L"아이템 획득";
    case 4: return L"임시 문자열";
    case 5: return L"대화";
    case 6: return L"경험치";
    case 7: return g_tailFilters.userName1.empty() ? L"사용자1" : g_tailFilters.userName1;
    case 8: return g_tailFilters.userName2.empty() ? L"사용자2" : g_tailFilters.userName2;
    case 9: return g_tailFilters.userName3.empty() ? L"사용자3" : g_tailFilters.userName3;
    default: return L"전체";
    }
}

[[maybe_unused]] static std::wstring TailModeTitle(int mode)
{
    return TailModeTitleRaw(mode);
}

static bool ModeInList(const std::vector<int>& modes, int mode)
{
    return std::find(modes.begin(), modes.end(), mode) != modes.end();
}

static std::vector<int> DefaultTailModesFor(int mode)
{
    if (mode < 0 || mode > 9)
        mode = 0;
    return { mode };
}

static std::wstring GetCaptureLogDir()
{
    std::wstring logDir = MakeAbsolutePath(GetModuleDirectory(), L"log");
    CreateDirectoryW(logDir.c_str(), nullptr);
    return logDir;
}

void OpenCaptureLogFolder(HWND owner)
{
    std::wstring dir = GetCaptureLogDir();
    ShellExecuteW(owner, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static bool EnsureCaptureLogStarted(HWND owner)
{
    if (!g_app)
        return false;

    if (!g_app->captureLogEnabled)
    {
        g_app->captureLogEnabled = true;
        SaveCaptureLogSettings();
    }

    if (g_app->hCaptureLogFile == INVALID_HANDLE_VALUE)
        StartCaptureLog();

    if (g_app->hCaptureLogFile == INVALID_HANDLE_VALUE || g_app->captureLogPath.empty())
    {
        MessageBoxW(owner, L"갈무리 로그 파일을 열 수 없습니다.", L"갈무리", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

static bool FileSizeOf(const std::wstring& path, LONGLONG& size)
{
    size = 0;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER li{};
    BOOL ok = GetFileSizeEx(h, &li);
    CloseHandle(h);
    if (!ok)
        return false;
    size = li.QuadPart;
    return true;
}

static std::wstring ReadFileRangeUtf8(const std::wstring& path, LONGLONG from, LONGLONG& to)
{
    to = from;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return L"";

    LARGE_INTEGER pos{};
    pos.QuadPart = from;
    SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);

    std::string bytes;
    char buf[8192];
    DWORD read = 0;
    while (ReadFile(h, buf, sizeof(buf), &read, nullptr) && read > 0)
    {
        bytes.append(buf, buf + read);
        to += read;
        if (bytes.size() > 1024 * 1024)
            break;
    }

    CloseHandle(h);
    if (bytes.empty())
        return L"";
    return Utf8ToWide(bytes);
}

static std::wstring ReadTailIniString(const wchar_t* key, const wchar_t* defValue)
{
    wchar_t buf[4096] = {};
    GetPrivateProfileStringW(L"tail_filters", key, defValue, buf, 4096, GetSettingsPath().c_str());
    return buf;
}

static void LoadTailFilterSettings()
{
    if (g_tailFiltersLoaded)
        return;

    g_tailFilters.chat = ReadTailIniString(L"chat", L"[]:;<>:;잡담 :;잡담:");
    if (g_tailFilters.chat.find(L"잡담") == std::wstring::npos)
        g_tailFilters.chat += L";잡담 :;잡담:";
    g_tailFilters.auction = ReadTailIniString(L"auction", L"경매;입찰;낙찰");
    g_tailFilters.talk = ReadTailIniString(L"talk", L"말합니다;말했다;말한다;귓속말;속삭;외칩니다;대화");
    g_tailFilters.item = ReadTailIniString(L"item", L"획득;얻었;얻었습니다;주웠;가져;받았습니다;손에 넣");
    g_tailFilters.exp = ReadTailIniString(L"exp", L"경험치;경험;수련치;숙련도");
    g_tailFilters.userName1 = ReadTailIniString(L"user1_name", L"사용자1");
    g_tailFilters.userPattern1 = ReadTailIniString(L"user1_pattern", L"");
    g_tailFilters.userName2 = ReadTailIniString(L"user2_name", L"사용자2");
    g_tailFilters.userPattern2 = ReadTailIniString(L"user2_pattern", L"");
    g_tailFilters.userName3 = ReadTailIniString(L"user3_name", L"사용자3");
    g_tailFilters.userPattern3 = ReadTailIniString(L"user3_pattern", L"");

    g_tailFilters.ansiAll = ReadTailIniBool(L"ansi_all", false);
    g_tailFilters.ansiChat = ReadTailIniBool(L"ansi_chat", false);
    g_tailFilters.ansiAuction = ReadTailIniBool(L"ansi_auction", false);
    g_tailFilters.ansiTalk = ReadTailIniBool(L"ansi_talk", false);
    g_tailFilters.ansiItem = ReadTailIniBool(L"ansi_item", false);
    g_tailFilters.ansiExp = ReadTailIniBool(L"ansi_exp", false);
    g_tailFilters.ansiUser1 = ReadTailIniBool(L"ansi_user1", false);
    g_tailFilters.ansiUser2 = ReadTailIniBool(L"ansi_user2", false);
    g_tailFilters.ansiUser3 = ReadTailIniBool(L"ansi_user3", false);

    g_tailFiltersLoaded = true;
}

static void SaveTailFilterSettings()
{
    std::wstring ini = GetSettingsPath();
    WritePrivateProfileStringW(L"tail_filters", L"chat", g_tailFilters.chat.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"tail_filters", L"auction", g_tailFilters.auction.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"tail_filters", L"talk", g_tailFilters.talk.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"tail_filters", L"item", g_tailFilters.item.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"tail_filters", L"exp", g_tailFilters.exp.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"tail_filters", L"user1_name", g_tailFilters.userName1.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"tail_filters", L"user1_pattern", g_tailFilters.userPattern1.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"tail_filters", L"user2_name", g_tailFilters.userName2.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"tail_filters", L"user2_pattern", g_tailFilters.userPattern2.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"tail_filters", L"user3_name", g_tailFilters.userName3.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"tail_filters", L"user3_pattern", g_tailFilters.userPattern3.c_str(), ini.c_str());

    WritePrivateProfileStringW(L"tail_filters", L"ansi_all", g_tailFilters.ansiAll ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"tail_filters", L"ansi_chat", g_tailFilters.ansiChat ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"tail_filters", L"ansi_auction", g_tailFilters.ansiAuction ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"tail_filters", L"ansi_talk", g_tailFilters.ansiTalk ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"tail_filters", L"ansi_item", g_tailFilters.ansiItem ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"tail_filters", L"ansi_exp", g_tailFilters.ansiExp ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"tail_filters", L"ansi_user1", g_tailFilters.ansiUser1 ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"tail_filters", L"ansi_user2", g_tailFilters.ansiUser2 ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"tail_filters", L"ansi_user3", g_tailFilters.ansiUser3 ? L"1" : L"0", ini.c_str());
}

static std::wstring TrimCopy(std::wstring s)
{
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t' || s.front() == L'\r' || s.front() == L'\n'))
        s.erase(s.begin());
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' || s.back() == L'\r' || s.back() == L'\n'))
        s.pop_back();
    return s;
}

static std::vector<std::wstring> SplitTailTerms(const std::wstring& src)
{
    std::vector<std::wstring> terms;
    std::wstring cur;
    for (wchar_t ch : src)
    {
        if (ch == L';' || ch == L'\n' || ch == L'\r' || ch == L'|')
        {
            cur = TrimCopy(cur);
            if (!cur.empty())
                terms.push_back(cur);
            cur.clear();
        }
        else
            cur += ch;
    }
    cur = TrimCopy(cur);
    if (!cur.empty())
        terms.push_back(cur);
    return terms;
}

static bool TailSpecialOrContains(const std::wstring& s, const std::wstring& term)
{
    if (term == L"[]:" || term == L"[%]:" || term == L"[%1]:")
        return !s.empty() && s.front() == L'[' && s.find(L"]:") != std::wstring::npos;
    if (term == L"<>:" || term == L"<%>:" || term == L"<%1>:")
        return !s.empty() && s.front() == L'<' && s.find(L">:") != std::wstring::npos;
    if (term == L"잡담" || term == L"잡담:" || term == L"잡담 :")
    {
        size_t p = s.find(L"잡담");
        if (p == std::wstring::npos)
            return false;
        return s.find(L':', p + 2) != std::wstring::npos;
    }
    return s.find(term) != std::wstring::npos;
}

static bool ContainsTailTerms(const std::wstring& s, const std::wstring& termsText)
{
    std::vector<std::wstring> terms = SplitTailTerms(termsText);
    for (const auto& term : terms)
    {
        if (TailSpecialOrContains(s, term))
            return true;
    }
    return false;
}

static std::wstring MakeTailLineTimestamp()
{
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[32] = {};
    wsprintfW(buf, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static std::wstring TrimLeftCopy(std::wstring s)
{
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t'))
        s.erase(s.begin());
    return s;
}


static COLORREF TailColorFrom256(int idx)
{
    if (idx < 0) idx = 0;
    if (idx > 255) idx = 255;
    if (idx < 16) return BaseAnsi16(idx);
    if (idx >= 232)
    {
        int gray = 8 + (idx - 232) * 10;
        return RGB(gray, gray, gray);
    }
    idx -= 16;
    int r = idx / 36;
    int g = (idx / 6) % 6;
    int b = idx % 6;
    static const int steps[6] = { 0, 95, 135, 175, 215, 255 };
    return RGB(steps[r], steps[g], steps[b]);
}

static std::wstring StripAnsiForTail(const std::wstring& src)
{
    std::wstring out;
    enum class S { Normal, Esc, Csi, Osc, OscEsc } state = S::Normal;
    for (wchar_t ch : src)
    {
        switch (state)
        {
        case S::Normal:
            if (ch == 0x1B) state = S::Esc;
            else if (ch >= 0x20 || ch == L'\t' || ch == L'\r' || ch == L'\n') out += ch;
            break;
        case S::Esc:
            if (ch == L'[') state = S::Csi;
            else if (ch == L']') state = S::Osc;
            else state = S::Normal;
            break;
        case S::Csi:
            if (ch >= 0x40 && ch <= 0x7E) state = S::Normal;
            break;
        case S::Osc:
            if (ch == 0x07) state = S::Normal;
            else if (ch == 0x1B) state = S::OscEsc;
            break;
        case S::OscEsc:
            state = (ch == L'\\') ? S::Normal : S::Osc;
            break;
        }
    }
    return out;
}

static std::vector<int> ParseAnsiParams(const std::wstring& p)
{
    std::vector<int> nums;
    int value = 0;
    bool have = false;
    for (wchar_t ch : p)
    {
        if (ch >= L'0' && ch <= L'9')
        {
            value = value * 10 + (ch - L'0');
            have = true;
        }
        else if (ch == L';')
        {
            nums.push_back(have ? value : 0);
            value = 0; have = false;
        }
    }
    nums.push_back(have ? value : 0);
    return nums;
}

static void TailFlushRun(std::vector<StyledRun>& runs, std::wstring& text, const TextStyle& style)
{
    if (text.empty()) return;
    if (!runs.empty() && runs.back().style == style)
        runs.back().text += text;
    else
        runs.push_back({ style, text });
    text.clear();
}

static std::vector<StyledRun> TailAnsiToRuns(const std::wstring& src)
{
    std::vector<StyledRun> runs;
    TextStyle style{};
    style.fg = g_app ? g_app->logStyle.textColor : RGB(220, 220, 220);
    style.bg = RGB(0, 0, 0);
    style.bold = false;
    int fgBase = -1;
    int bgBase = -1;
    std::wstring text;
    enum class S { Normal, Esc, Csi, Osc, OscEsc } state = S::Normal;
    std::wstring params;
    const COLORREF* table = GetAnsiThemeTable(g_app ? g_app->ansiTheme : ID_THEME_CAMPBELL);
    auto baseColor = [&](int idx)->COLORREF { if (idx < 0) idx = 0; if (idx > 15) idx = 15; return table[idx]; };
    auto resetStyle = [&]() { style.fg = g_app ? g_app->logStyle.textColor : RGB(220,220,220); style.bg = RGB(0,0,0); style.bold = false; fgBase = bgBase = -1; };

    for (wchar_t ch : src)
    {
        switch (state)
        {
        case S::Normal:
            if (ch == 0x1B) { TailFlushRun(runs, text, style); state = S::Esc; }
            else if (ch >= 0x20 || ch == L'\t') text += ch;
            break;
        case S::Esc:
            if (ch == L'[') { params.clear(); state = S::Csi; }
            else if (ch == L']') state = S::Osc;
            else state = S::Normal;
            break;
        case S::Csi:
            if ((ch >= L'0' && ch <= L'9') || ch == L';') params += ch;
            else
            {
                if (ch == L'm')
                {
                    auto nums = ParseAnsiParams(params.empty() ? L"0" : params);
                    for (size_t i = 0; i < nums.size(); ++i)
                    {
                        int n = nums[i];
                        if (n == 0) resetStyle();
                        else if (n == 1) { style.bold = true; if (fgBase >= 0 && fgBase <= 7) style.fg = baseColor(fgBase + 8); }
                        else if (n == 22) { style.bold = false; if (fgBase >= 0 && fgBase <= 7) style.fg = baseColor(fgBase); }
                        else if (n >= 30 && n <= 37) { fgBase = n - 30; style.fg = baseColor(fgBase + (style.bold ? 8 : 0)); }
                        else if (n >= 90 && n <= 97) { fgBase = n - 90; style.fg = baseColor(fgBase + 8); }
                        else if (n == 39) { fgBase = -1; style.fg = g_app ? g_app->logStyle.textColor : RGB(220,220,220); }
                        else if (n >= 40 && n <= 47) { bgBase = n - 40; style.bg = baseColor(bgBase); }
                        else if (n >= 100 && n <= 107) { bgBase = n - 100; style.bg = baseColor(bgBase + 8); }
                        else if (n == 49) { bgBase = -1; style.bg = RGB(0,0,0); }
                        else if ((n == 38 || n == 48) && i + 1 < nums.size())
                        {
                            bool fg = (n == 38);
                            if (nums[i + 1] == 5 && i + 2 < nums.size())
                            {
                                COLORREF c = TailColorFrom256(nums[i + 2]);
                                if (fg) { style.fg = c; fgBase = -1; } else { style.bg = c; bgBase = -1; }
                                i += 2;
                            }
                            else if (nums[i + 1] == 2 && i + 4 < nums.size())
                            {
                                int r = max(0, min(255, nums[i + 2]));
                                int g = max(0, min(255, nums[i + 3]));
                                int b = max(0, min(255, nums[i + 4]));
                                COLORREF c = RGB(r, g, b);
                                if (fg) { style.fg = c; fgBase = -1; } else { style.bg = c; bgBase = -1; }
                                i += 4;
                            }
                        }
                    }
                }
                state = S::Normal;
            }
            break;
        case S::Osc:
            if (ch == 0x07) state = S::Normal;
            else if (ch == 0x1B) state = S::OscEsc;
            break;
        case S::OscEsc:
            state = (ch == L'\\') ? S::Normal : S::Osc;
            break;
        }
    }
    TailFlushRun(runs, text, style);
    return runs;
}

static bool TailLineMatchesMode(int mode, const std::wstring& line, const std::wstring& customPattern)
{
    LoadTailFilterSettings();
    if (mode == 0)
        return true;
    std::wstring s = TrimLeftCopy(StripAnsiForTail(line));
    if (mode == 1) return ContainsTailTerms(s, g_tailFilters.chat);
    if (mode == 2) return ContainsTailTerms(s, g_tailFilters.auction);
    if (mode == 3) return ContainsTailTerms(s, g_tailFilters.item);
    if (mode == 4) return customPattern.empty() ? true : (s.find(customPattern) != std::wstring::npos);
    if (mode == 5) return ContainsTailTerms(s, g_tailFilters.talk);
    if (mode == 6) return ContainsTailTerms(s, g_tailFilters.exp);
    if (mode == 7) return !g_tailFilters.userPattern1.empty() && ContainsTailTerms(s, g_tailFilters.userPattern1);
    if (mode == 8) return !g_tailFilters.userPattern2.empty() && ContainsTailTerms(s, g_tailFilters.userPattern2);
    if (mode == 9) return !g_tailFilters.userPattern3.empty() && ContainsTailTerms(s, g_tailFilters.userPattern3);
    return false;
}

static void TailSetTitle(TailState* st)
{
    if (!st || !st->hwnd) return;
    std::wstring title = L"KTin 갈무리 보기 - " + TailModeTitleRaw(st->activeMode);
    if (st->activeMode == 4 && !st->customPattern.empty())
        title += L" : " + st->customPattern;
    SetWindowTextW(st->hwnd, title.c_str());
}

static void TailUpdateStatus(TailState* st)
{
    if (!st || !st->hwndStatus || !IsWindow(st->hwndStatus))
        return;
    std::wstring mode = TailModeTitleRaw(st->activeMode);
    if (st->activeMode == 4 && !st->customPattern.empty())
        mode += L" : " + st->customPattern;
    wchar_t buf[1024];
    _snwprintf_s(buf, 1024, _TRUNCATE, L"%s 보기 | %lld줄 | 표시 %d줄 | %s",
        mode.c_str(), st->totalMatchedLines, (int)st->lines.size(), st->logPath.c_str());
    SetWindowTextW(st->hwndStatus, buf);
}

static void FillTailLogFontFormat(CHARFORMAT2W& cf)
{
    if (!g_app)
        return;

    cf.dwMask |= CFM_FACE | CFM_SIZE | CFM_CHARSET;
    wcsncpy_s(cf.szFaceName, g_app->logStyle.font.lfFaceName, _TRUNCATE);
    cf.yHeight = GetFontPointSizeFromLogFont(g_app->logStyle.font) * 20;
    cf.bCharSet = HANGEUL_CHARSET;
}

static void TailSetCharFormat(HWND hwndEdit, COLORREF fg, COLORREF bg, bool bold)
{
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BACKCOLOR | CFM_BOLD;
    FillTailLogFontFormat(cf);
    cf.crTextColor = fg;
    cf.crBackColor = bg;
    cf.dwEffects = bold ? CFE_BOLD : 0;
    SendMessageW(hwndEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

static void TailApplyMainLogFontToEdit(HWND hwndEdit)
{
    if (!hwndEdit || !IsWindow(hwndEdit) || !g_app || !g_app->hFontLog)
        return;

    SendMessageW(hwndEdit, WM_SETFONT, (WPARAM)g_app->hFontLog, TRUE);
    SendMessageW(hwndEdit, EM_SETBKGNDCOLOR, 0, RGB(0, 0, 0));
    // RichEdit는 기본 상태에서 글자가 있는 부분만 배경색이 칠해지고,
    // 아직 글자가 없는 빈 영역은 흰색으로 남을 수 있습니다.
    // SES_EXTENDBACKCOLOR를 켜면 마지막 글자 이후의 빈 영역까지
    // 같은 배경색으로 확장되어 갈무리 보기 첫 실행 시 흰 화면이 보이지 않습니다.
    SendMessageW(hwndEdit, EM_SETEDITSTYLE, SES_EXTENDBACKCOLOR, SES_EXTENDBACKCOLOR);

    // 기본 입력 서식과 이미 출력된 글자의 글꼴/크기만 메인 출력창과 맞춥니다.
    // 색상은 건드리지 않으므로 ANSI 보기 색상은 유지됩니다.
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    FillTailLogFontFormat(cf);
    if (cf.dwMask)
    {
        SendMessageW(hwndEdit, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
        SendMessageW(hwndEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    }
    InvalidateRect(hwndEdit, nullptr, TRUE);
}

static void TailAppendPlainText(TailState* st, const std::wstring& text, COLORREF fg = RGB(220,220,220), bool bold = false)
{
    if (!st || !st->hwndEdit || text.empty())
        return;
    int len = GetWindowTextLengthW(st->hwndEdit);
    SendMessageW(st->hwndEdit, EM_SETSEL, len, len);
    TailSetCharFormat(st->hwndEdit, fg, RGB(0,0,0), bold);
    SendMessageW(st->hwndEdit, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
}

static void TailAppendLineEx(TailState* st, const std::wstring& plainLine, const std::wstring& rawLine, bool useAnsi, bool addTimestamp)
{
    if (!st || !st->hwndEdit || !IsWindow(st->hwndEdit))
        return;
    st->totalMatchedLines++;
    st->lines.push_back(plainLine);
    while ((int)st->lines.size() > st->maxLines)
        st->lines.pop_front();

    int len = GetWindowTextLengthW(st->hwndEdit);
    SendMessageW(st->hwndEdit, EM_SETSEL, len, len);
    if (len > 0)
        TailAppendPlainText(st, L"\r\n");

    if (addTimestamp)
        TailAppendPlainText(st, MakeTailLineTimestamp(), RGB(160, 160, 160), false);

    if (useAnsi)
    {
        std::vector<StyledRun> runs = TailAnsiToRuns(rawLine);
        for (const auto& run : runs)
        {
            if (run.text.empty()) continue;
            TailSetCharFormat(st->hwndEdit, run.style.fg, run.style.bg, run.style.bold);
            SendMessageW(st->hwndEdit, EM_REPLACESEL, FALSE, (LPARAM)run.text.c_str());
        }
    }
    else
    {
        TailAppendPlainText(st, plainLine);
    }

    len = GetWindowTextLengthW(st->hwndEdit);
    SendMessageW(st->hwndEdit, EM_SETSEL, len, len);

    int lc = (int)SendMessageW(st->hwndEdit, EM_GETLINECOUNT, 0, 0);
    if (lc > st->maxLines + 200)
    {
        int charIndex = (int)SendMessageW(st->hwndEdit, EM_LINEINDEX, 500, 0);
        if (charIndex > 0)
        {
            SendMessageW(st->hwndEdit, WM_SETREDRAW, FALSE, 0);
            SendMessageW(st->hwndEdit, EM_SETSEL, 0, charIndex);
            SendMessageW(st->hwndEdit, EM_REPLACESEL, FALSE, (LPARAM)L"");
            SendMessageW(st->hwndEdit, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(st->hwndEdit, nullptr, TRUE);
        }
    }
}

static void TailProcessText(TailState* st, const std::wstring& text)
{
    if (!st || text.empty())
        return;
    st->fragment += text;
    while (true)
    {
        size_t p = st->fragment.find(L'\n');
        if (p == std::wstring::npos)
            break;
        std::wstring line = st->fragment.substr(0, p);
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();
        st->fragment.erase(0, p + 1);

        if (st->firstRead && !line.empty() && st->startedFromMiddle)
        {
            st->firstRead = false;
            st->startedFromMiddle = false;
            continue;
        }
        st->firstRead = false;
        st->startedFromMiddle = false;

        std::wstring plainLine = StripAnsiForTail(line);
        if (TailLineMatchesMode(st->activeMode, plainLine, st->customPattern))
        {
            bool ansiView = TailAnsiEnabledForMode(st->activeMode);
            TailAppendLineEx(st, plainLine, line, ansiView, st->activeMode != 0);
        }
    }
}

static void TailReadNewData(TailState* st, bool initial)
{
    if (!st || !g_app)
        return;
    if (st->reading)
        return;
    st->reading = true;
    struct TailReadGuard { TailState* s; ~TailReadGuard(){ if (s) s->reading = false; } } guard{ st };
    FlushCaptureLogBuffer();

    // 갈무리 파일은 갈무리를 새로 켜거나 재접속 과정에서 바뀔 수 있습니다.
    // 갈무리 보기창은 열린 시점의 파일만 고정하지 말고 현재 KTin이 쓰는 파일을 따라갑니다.
    if (!g_app->captureLogPath.empty() && st->logPath != g_app->captureLogPath)
    {
        st->logPath = g_app->captureLogPath;
        st->lastPos = 0;
        st->fragment.clear();
        st->lines.clear();
        st->totalMatchedLines = 0;
        if (st->hwndEdit && IsWindow(st->hwndEdit))
            SetWindowTextW(st->hwndEdit, L"");
        st->firstRead = false;
        st->startedFromMiddle = false;
    }

    if (st->logPath.empty())
        st->logPath = g_app->captureLogPath;
    if (st->logPath.empty())
        return;

    LONGLONG size = 0;
    if (!FileSizeOf(st->logPath, size))
        return;
    if (initial)
    {
        const LONGLONG kInitialBytes = 1024 * 1024;
        st->lastPos = (size > kInitialBytes) ? (size - kInitialBytes) : 0;
        st->startedFromMiddle = (st->lastPos > 0);
        st->firstRead = st->startedFromMiddle;
    }
    else if (st->lastPos > size)
    {
        st->lastPos = 0;
        st->fragment.clear();
        st->lines.clear();
        st->totalMatchedLines = 0;
        if (st->hwndEdit && IsWindow(st->hwndEdit))
            SetWindowTextW(st->hwndEdit, L"");
        st->firstRead = false;
        st->startedFromMiddle = false;
    }
    if (st->lastPos >= size)
    {
        TailUpdateStatus(st);
        return;
    }
    LONGLONG newPos = st->lastPos;
    std::wstring text = ReadFileRangeUtf8(st->logPath, st->lastPos, newPos);
    st->lastPos = newPos;
    TailProcessText(st, text);
    TailUpdateStatus(st);
}

static void TailLayout(TailState* st);
static void TailSnapToNearbyWindows(TailState* st);
static bool TailGetVisibleWindowRect(HWND hwnd, RECT& out);
static void TailMoveWindowToVisibleRect(TailState* st, const RECT& desiredVisibleRect);

static void TailReloadActiveMode(TailState* st)
{
    if (!st) return;
    if (st->hwndEdit && IsWindow(st->hwndEdit))
        SetWindowTextW(st->hwndEdit, L"");
    st->lines.clear();
    st->totalMatchedLines = 0;
    st->lastPos = 0;
    st->firstRead = true;
    st->startedFromMiddle = false;
    st->fragment.clear();
    TailSetTitle(st);
    TailReadNewData(st, true);
}

static void TailRebuildTabs(TailState* st)
{
    if (!st || !st->hwndTab)
        return;
    TabCtrl_DeleteAllItems(st->hwndTab);
    for (size_t i = 0; i < st->visibleModes.size(); ++i)
    {
        int mode = st->visibleModes[i];
        std::wstring title = TailModeTitleRaw(mode);
        if (mode == 4 && !st->customPattern.empty())
            title = L"임시";
        TCITEMW item{};
        item.mask = TCIF_TEXT | TCIF_PARAM;
        item.pszText = (LPWSTR)title.c_str();
        item.lParam = mode;
        TabCtrl_InsertItem(st->hwndTab, (int)i, &item);
        if (mode == st->activeMode)
            TabCtrl_SetCurSel(st->hwndTab, (int)i);
    }

    ShowWindow(st->hwndTab, (st->visibleModes.size() <= 1) ? SW_HIDE : SW_SHOW);
    TailLayout(st);
}

static void TailSetActiveMode(TailState* st, int mode)
{
    if (!st) return;
    st->activeMode = mode;
    TailRebuildTabs(st);
    TailReloadActiveMode(st);
}

static HMENU CreateTailWindowMenu(TailState* st, bool hidden, bool statusHidden)
{
    HMENU bar = CreateMenu();
    HMENU view = CreatePopupMenu();
    AppendMenuW(view, MF_STRING, ID_TAIL_MENU_TAB_SETTINGS, L"탭 설정...");
    AppendMenuW(view, MF_STRING, ID_TAIL_MENU_HIDE_MENU, hidden ? L"메뉴 보이기" : L"메뉴 숨기기");
    AppendMenuW(view, MF_STRING, ID_TAIL_MENU_TOGGLE_STATUS, statusHidden ? L"상태바 보기" : L"상태바 숨기기");
    AppendMenuW(view, MF_STRING | ((st && st->alwaysOnTop) ? MF_CHECKED : MF_UNCHECKED), ID_TAIL_MENU_TOPMOST, L"항상 위");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, ID_TAIL_MENU_COPY, L"선택/전체 복사");
    AppendMenuW(view, MF_STRING, ID_TAIL_MENU_SELECT_ALL, L"모두 선택");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, ID_TAIL_MENU_CLOSE, L"닫기");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, L"보기");
    return bar;
}

static void TailApplyMenuVisibility(TailState* st)
{
    if (!st || !st->hwnd) return;
    if (st->menuHidden)
        SetMenu(st->hwnd, nullptr);
    else
        SetMenu(st->hwnd, CreateTailWindowMenu(st, false, st->statusHidden));
    DrawMenuBar(st->hwnd);
}

static void TailLayout(TailState* st)
{
    if (!st || !st->hwnd)
        return;
    RECT rc{};
    GetClientRect(st->hwnd, &rc);
    int statusH = st->statusHidden ? 0 : 24;
    int tabH = (st->visibleModes.size() <= 1) ? 0 : 28;

    if (st->hwndStatus)
    {
        ShowWindow(st->hwndStatus, st->statusHidden ? SW_HIDE : SW_SHOW);
        if (!st->statusHidden)
            MoveWindow(st->hwndStatus, 0, rc.bottom - statusH, rc.right, statusH, TRUE);
    }

    if (st->hwndTab)
    {
        ShowWindow(st->hwndTab, (st->visibleModes.size() <= 1) ? SW_HIDE : SW_SHOW);
        if (st->visibleModes.size() > 1)
            MoveWindow(st->hwndTab, 0, 0, rc.right, tabH, TRUE);
    }

    if (st->hwndEdit)
    {
        int editW = static_cast<int>(rc.right - rc.left);
        int editH = static_cast<int>(rc.bottom - rc.top - statusH - tabH);
        if (editH < 0) editH = 0;
        MoveWindow(st->hwndEdit, 0, tabH, editW, editH, TRUE);
    }
}

static std::wstring TailGetEditSelectionOrAll(TailState* st)
{
    if (!st || !st->hwndEdit) return L"";
    DWORD start = 0, end = 0;
    SendMessageW(st->hwndEdit, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
    int len = GetWindowTextLengthW(st->hwndEdit);
    if (len <= 0) return L"";
    std::wstring all(len, L'\0');
    GetWindowTextW(st->hwndEdit, &all[0], len + 1);
    if (end > start && end <= (DWORD)len)
        return all.substr(start, end - start);
    return all;
}

static void TailShowContextMenu(TailState* st, POINT ptScreen)
{
    if (!st || !st->hwnd) return;
    HMENU menu = CreatePopupMenu();
    if (st->menuHidden)
        AppendMenuW(menu, MF_STRING, ID_TAIL_MENU_SHOW_MENU, L"메뉴 보이기");
    else
        AppendMenuW(menu, MF_STRING, ID_TAIL_MENU_HIDE_MENU, L"메뉴 숨기기");
    AppendMenuW(menu, MF_STRING, ID_TAIL_MENU_TOGGLE_STATUS, st->statusHidden ? L"상태바 보기" : L"상태바 숨기기");
    AppendMenuW(menu, MF_STRING | (st->alwaysOnTop ? MF_CHECKED : MF_UNCHECKED), ID_TAIL_MENU_TOPMOST, L"항상 위");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TAIL_MENU_TAB_SETTINGS, L"탭 설정...");
    AppendMenuW(menu, MF_STRING, ID_TAIL_MENU_COPY, L"선택/전체 복사");
    AppendMenuW(menu, MF_STRING, ID_TAIL_MENU_SELECT_ALL, L"모두 선택");
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN, ptScreen.x, ptScreen.y, 0, st->hwnd, nullptr);
    DestroyMenu(menu);
}

static LRESULT CALLBACK TailEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    TailState* st = (TailState*)GetPropW(hwnd, L"TailState");
    if (msg == WM_LBUTTONUP && st)
    {
        LRESULT r = CallWindowProcW(st->oldEditProc, hwnd, msg, wParam, lParam);
        DWORD a = 0, b = 0;
        SendMessageW(hwnd, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
        if (b > a)
        {
            std::wstring sel = TailGetEditSelectionOrAll(st);
            if (!sel.empty())
                CopyToClipboard(st->hwnd, sel);
        }
        return r;
    }
    if (msg == WM_CONTEXTMENU && st)
    {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (pt.x == -1 && pt.y == -1)
        {
            RECT rc{}; GetWindowRect(hwnd, &rc); pt.x = rc.left + 20; pt.y = rc.top + 20;
        }
        TailShowContextMenu(st, pt);
        return 0;
    }
    if (msg == WM_SETFOCUS)
    {
        if (!st || !st->oldEditProc)
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        LRESULT r = CallWindowProcW(st->oldEditProc, hwnd, msg, wParam, lParam);
        HideCaret(hwnd);
        return r;
    }
    if (!st || !st->oldEditProc)
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    return CallWindowProcW(st->oldEditProc, hwnd, msg, wParam, lParam);
}

struct TailTabSettingsState
{
    TailState* tail = nullptr;
    std::vector<int> modes;
    bool accepted = false;
};

static LRESULT CALLBACK TailTabSettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    TailTabSettingsState* st = (TailTabSettingsState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    static int modeList[] = { 0, 1, 5, 2, 3, 6, 7, 8, 9 };
    switch (msg)
    {
    case WM_CREATE:
    {
        st = (TailTabSettingsState*)((CREATESTRUCTW*)lParam)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        ApplyPopupTitleBarTheme(hwnd);
        CreateWindowExW(0, L"STATIC", L"이 갈무리 보기 창에서 표시할 탭을 선택하세요.", WS_CHILD | WS_VISIBLE,
            12, 12, 330, 22, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        int y = 42;
        for (int i = 0; i < 9; ++i)
        {
            int mode = modeList[i];
            std::wstring title = TailModeTitleRaw(mode);
            HWND chk = CreateWindowExW(0, L"BUTTON", title.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                22, y, 260, 22, hwnd, (HMENU)(UINT_PTR)(ID_TAIL_TAB_CHECK_BASE + mode), GetModuleHandleW(nullptr), nullptr);
            if (ModeInList(st->modes, mode))
                SendMessageW(chk, BM_SETCHECK, BST_CHECKED, 0);
            y += 26;
        }
        CreateWindowExW(0, L"BUTTON", L"확인", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            145, 288, 80, 28, hwnd, (HMENU)(UINT_PTR)ID_TAIL_TAB_SET_OK, GetModuleHandleW(nullptr), nullptr);
        CreateWindowExW(0, L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            235, 288, 80, 28, hwnd, (HMENU)(UINT_PTR)ID_TAIL_TAB_SET_CANCEL, GetModuleHandleW(nullptr), nullptr);
        ApplyPopupFontToChildren(hwnd);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TAIL_TAB_SET_OK)
        {
            st->modes.clear();
            for (int i = 0; i < 9; ++i)
            {
                int mode = modeList[i];
                if (SendMessageW(GetDlgItem(hwnd, ID_TAIL_TAB_CHECK_BASE + mode), BM_GETCHECK, 0, 0) == BST_CHECKED)
                    st->modes.push_back(mode);
            }
            if (st->modes.empty())
                st->modes.push_back(st->tail ? st->tail->activeMode : 0);
            st->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == ID_TAIL_TAB_SET_CANCEL)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void CenterPopupToOwner(HWND hwnd, HWND owner)
{
    RECT rcDlg{}; GetWindowRect(hwnd, &rcDlg);
    int w = rcDlg.right - rcDlg.left;
    int h = rcDlg.bottom - rcDlg.top;
    RECT rcOwner{};
    if (owner && IsWindow(owner)) GetWindowRect(owner, &rcOwner);
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcOwner, 0);
    int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - w) / 2;
    int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - h) / 2;
    HMONITOR mon = MonitorFromRect(&rcOwner, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{}; mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi))
    {
        if (x < mi.rcWork.left) x = mi.rcWork.left;
        if (y < mi.rcWork.top) y = mi.rcWork.top;
        if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
        if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
    }
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void ApplyPopupFontToChildren(HWND hwnd)
{
    HFONT hFont = GetPopupUIFont(hwnd);
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)hFont, TRUE);
    EnumChildWindows(hwnd, [](HWND child, LPARAM lp) -> BOOL {
        SendMessageW(child, WM_SETFONT, (WPARAM)lp, TRUE);
        return TRUE;
        }, (LPARAM)hFont);
}

static void ShowTailTabSettingsDialog(TailState* tail)
{
    if (!tail || !tail->hwnd) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = TailTabSettingsWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kTailTabSettingsWndClass;
    RegisterClassW(&wc);

    TailTabSettingsState st;
    st.tail = tail;
    st.modes = tail->visibleModes;
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kTailTabSettingsWndClass, L"갈무리 보기 탭 설정",
        WS_CAPTION | WS_SYSMENU | WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 350, 365,
        tail->hwnd, nullptr, GetModuleHandleW(nullptr), &st);
    if (!hwnd) return;
    CenterPopupToOwner(hwnd, tail->hwnd);
    EnableWindow(tail->hwnd, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0))
    {
        if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(tail->hwnd, TRUE);
    SetForegroundWindow(tail->hwnd);
    if (st.accepted)
    {
        tail->visibleModes = st.modes;
        if (!ModeInList(tail->visibleModes, tail->activeMode))
            tail->activeMode = tail->visibleModes.front();
        TailRebuildTabs(tail);
        TailReloadActiveMode(tail);
    }
}



// buildfix32: Windows 10/11의 GetWindowRect에는 보이지 않는 프레임 여백이 포함될 수 있습니다.
// 그래서 창끼리 "붙이기"를 할 때는 실제 화면에 보이는 프레임 기준(DWM extended frame)으로 계산합니다.
static bool TailGetVisibleWindowRect(HWND hwnd, RECT& out)
{
    if (!hwnd || !IsWindow(hwnd))
        return false;

    RECT rc{};
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc));
    if (SUCCEEDED(hr) && rc.right > rc.left && rc.bottom > rc.top)
    {
        out = rc;
        return true;
    }

    return !!GetWindowRect(hwnd, &out);
}

static RECT TailVisibleRectFromWindowRect(HWND hwnd, const RECT& windowRect)
{
    RECT nowWindow{};
    RECT nowVisible{};
    if (!GetWindowRect(hwnd, &nowWindow) || !TailGetVisibleWindowRect(hwnd, nowVisible))
        return windowRect;

    RECT vis{};
    vis.left   = windowRect.left   + (nowVisible.left   - nowWindow.left);
    vis.top    = windowRect.top    + (nowVisible.top    - nowWindow.top);
    vis.right  = windowRect.right  - (nowWindow.right   - nowVisible.right);
    vis.bottom = windowRect.bottom - (nowWindow.bottom  - nowVisible.bottom);
    return vis;
}

static void TailMoveWindowToVisibleRect(TailState* st, const RECT& desiredVisibleRect)
{
    if (!st || !st->hwnd || !IsWindow(st->hwnd))
        return;

    RECT win{};
    RECT vis{};
    if (!GetWindowRect(st->hwnd, &win) || !TailGetVisibleWindowRect(st->hwnd, vis))
        return;

    int leftPad = vis.left - win.left;
    int topPad = vis.top - win.top;
    int rightPad = win.right - vis.right;
    int bottomPad = win.bottom - vis.bottom;

    int x = desiredVisibleRect.left - leftPad;
    int y = desiredVisibleRect.top - topPad;
    int w = (desiredVisibleRect.right - desiredVisibleRect.left) + leftPad + rightPad;
    int h = (desiredVisibleRect.bottom - desiredVisibleRect.top) + topPad + bottomPad;

    if (w < 120) w = 120;
    if (h < 80) h = 80;

    SetWindowPos(st->hwnd, st->alwaysOnTop ? HWND_TOPMOST : HWND_TOP,
        x, y, w, h, SWP_NOACTIVATE);
}

static int TailAbsInt(int v)
{
    return v < 0 ? -v : v;
}

static void TailAdjustRectToWorkArea(RECT& rc, bool snappedAbove, const RECT& target)
{
    HMONITOR mon = MonitorFromRect(&target, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi))
        return;

    int w = rc.right - rc.left;
    const int minH = 120;

    if (snappedAbove && rc.top < mi.rcWork.top)
    {
        rc.top = mi.rcWork.top;
        rc.bottom = target.top;
        if (rc.bottom - rc.top < minH)
            rc.bottom = min((int)mi.rcWork.bottom, (int)rc.top + minH);
    }
    else if (!snappedAbove && rc.bottom > mi.rcWork.bottom)
    {
        rc.bottom = mi.rcWork.bottom;
        if (rc.bottom - rc.top < minH)
            rc.top = max((int)mi.rcWork.top, (int)rc.bottom - minH);
    }

    if (rc.left < mi.rcWork.left)
    {
        rc.left = mi.rcWork.left;
        rc.right = rc.left + w;
    }
    if (rc.right > mi.rcWork.right)
    {
        rc.right = mi.rcWork.right;
        rc.left = rc.right - w;
        if (rc.left < mi.rcWork.left)
            rc.left = mi.rcWork.left;
    }
}

static bool TailTrySnapToTarget(RECT cur, const RECT& target, RECT& out)
{
    const int dist = 18;
    int curW = cur.right - cur.left;
    int curH = cur.bottom - cur.top;

    // 메인창/갈무리창 위쪽에 붙기: [갈무리창][대상창]
    if (TailAbsInt(cur.bottom - target.top) <= dist)
    {
        out.left = target.left;
        out.right = target.right;
        out.bottom = target.top;
        out.top = out.bottom - curH;
        TailAdjustRectToWorkArea(out, true, target);
        return true;
    }

    // 대상창 아래쪽에 붙기: [대상창][갈무리창]
    if (TailAbsInt(cur.top - target.bottom) <= dist)
    {
        out.left = target.left;
        out.right = target.right;
        out.top = target.bottom;
        out.bottom = out.top + curH;
        TailAdjustRectToWorkArea(out, false, target);
        return true;
    }

    // 좌우 붙기는 크기를 강제로 바꾸지 않고 가장자리만 맞춥니다.
    if (TailAbsInt(cur.right - target.left) <= dist)
    {
        out = cur;
        out.right = target.left;
        out.left = out.right - curW;
        out.top = target.top;
        out.bottom = out.top + curH;
        TailAdjustRectToWorkArea(out, false, target);
        return true;
    }
    if (TailAbsInt(cur.left - target.right) <= dist)
    {
        out = cur;
        out.left = target.right;
        out.right = out.left + curW;
        out.top = target.top;
        out.bottom = out.top + curH;
        TailAdjustRectToWorkArea(out, false, target);
        return true;
    }

    return false;
}

static void TailSnapToNearbyWindows(TailState* st)
{
    if (!st || !st->hwnd || !IsWindow(st->hwnd) || !g_app || !g_app->tailSnapEnabled)
        return;

    // Shift 키를 누른 채 이동하면 자동 붙기를 잠시 무시합니다.
    if (GetKeyState(VK_SHIFT) & 0x8000)
        return;

    RECT cur{};
    if (!TailGetVisibleWindowRect(st->hwnd, cur))
        return;
    RECT snapped{};

    if (g_app->hwndMain && IsWindow(g_app->hwndMain))
    {
        RECT target{};
        if (TailGetVisibleWindowRect(g_app->hwndMain, target) && TailTrySnapToTarget(cur, target, snapped))
        {
            TailMoveWindowToVisibleRect(st, snapped);
            return;
        }
    }

    for (auto* other : g_tailWindows)
    {
        if (!other || other == st || !other->hwnd || !IsWindow(other->hwnd))
            continue;
        RECT target{};
        if (TailGetVisibleWindowRect(other->hwnd, target) && TailTrySnapToTarget(cur, target, snapped))
        {
            TailMoveWindowToVisibleRect(st, snapped);
            return;
        }
    }
}


static int TailDockKind(const RECT& child, const RECT& target)
{
    const int tol = 10;
    int childW = child.right - child.left;
    int childH = child.bottom - child.top;
    int targetW = target.right - target.left;
    int targetH = target.bottom - target.top;

    int overlapX = min(child.right, target.right) - max(child.left, target.left);
    int overlapY = min(child.bottom, target.bottom) - max(child.top, target.top);

    if (TailAbsInt(child.bottom - target.top) <= tol && overlapX > min(childW, targetW) / 3)
        return 1; // child is above target
    if (TailAbsInt(child.top - target.bottom) <= tol && overlapX > min(childW, targetW) / 3)
        return 2; // child is below target
    if (TailAbsInt(child.right - target.left) <= tol && overlapY > min(childH, targetH) / 3)
        return 3; // child is left of target
    if (TailAbsInt(child.left - target.right) <= tol && overlapY > min(childH, targetH) / 3)
        return 4; // child is right of target
    return 0;
}

static RECT TailDesiredRectForDock(const RECT& childOld, const RECT& targetNew, int kind)
{
    RECT out = childOld;
    int w = childOld.right - childOld.left;
    int h = childOld.bottom - childOld.top;

    if (kind == 1) // above target
    {
        out.left = targetNew.left;
        out.right = targetNew.right;
        out.bottom = targetNew.top;
        out.top = out.bottom - h;
        TailAdjustRectToWorkArea(out, true, targetNew);
    }
    else if (kind == 2) // below target
    {
        out.left = targetNew.left;
        out.right = targetNew.right;
        out.top = targetNew.bottom;
        out.bottom = out.top + h;
        TailAdjustRectToWorkArea(out, false, targetNew);
    }
    else if (kind == 3) // left of target
    {
        out.right = targetNew.left;
        out.left = out.right - w;
        out.top = targetNew.top;
        out.bottom = out.top + h;
        TailAdjustRectToWorkArea(out, false, targetNew);
    }
    else if (kind == 4) // right of target
    {
        out.left = targetNew.right;
        out.right = out.left + w;
        out.top = targetNew.top;
        out.bottom = out.top + h;
        TailAdjustRectToWorkArea(out, false, targetNew);
    }
    return out;
}

void TailNotifyMainWindowMoved(HWND hwndMain, const RECT& oldMainWindowRect, const RECT& newMainWindowRect)
{
    if (!g_app || !g_app->tailSnapEnabled || !hwndMain || !IsWindow(hwndMain))
        return;
    if (g_tailWindows.empty())
        return;

    RECT oldMain = TailVisibleRectFromWindowRect(hwndMain, oldMainWindowRect);
    RECT newMain = TailVisibleRectFromWindowRect(hwndMain, newMainWindowRect);
    if (EqualRect(&oldMain, &newMain))
        return;

    struct Entry
    {
        TailState* st;
        RECT oldVis;
        RECT newVis;
        bool moved;
    };

    std::vector<Entry> entries;
    entries.reserve(g_tailWindows.size());
    for (auto* tw : g_tailWindows)
    {
        if (!tw || !tw->hwnd || !IsWindow(tw->hwnd))
            continue;
        RECT vis{};
        if (!TailGetVisibleWindowRect(tw->hwnd, vis))
            continue;
        entries.push_back({ tw, vis, vis, false });
    }

    struct Target
    {
        RECT oldRc;
        RECT newRc;
    };
    std::vector<Target> movedTargets;
    movedTargets.push_back({ oldMain, newMain });

    bool changed = true;
    int guard = 0;
    while (changed && guard++ < 8)
    {
        changed = false;
        for (auto& e : entries)
        {
            if (e.moved)
                continue;

            for (const auto& t : movedTargets)
            {
                int kind = TailDockKind(e.oldVis, t.oldRc);
                if (!kind)
                    continue;

                RECT desired = TailDesiredRectForDock(e.oldVis, t.newRc, kind);
                TailMoveWindowToVisibleRect(e.st, desired);
                RECT after{};
                if (TailGetVisibleWindowRect(e.st->hwnd, after))
                    e.newVis = after;
                else
                    e.newVis = desired;
                e.moved = true;
                movedTargets.push_back({ e.oldVis, e.newVis });
                changed = true;
                break;
            }
        }
    }
}

static void TailHandleCommand(TailState* st, int id)
{
    if (!st) return;
    switch (id)
    {
    case ID_TAIL_MENU_TAB_SETTINGS:
        ShowTailTabSettingsDialog(st);
        return;
    case ID_TAIL_MENU_HIDE_MENU:
        st->menuHidden = true;
        TailApplyMenuVisibility(st);
        return;
    case ID_TAIL_MENU_SHOW_MENU:
        st->menuHidden = false;
        TailApplyMenuVisibility(st);
        return;
    case ID_TAIL_MENU_TOGGLE_STATUS:
        st->statusHidden = !st->statusHidden;
        if (!st->menuHidden)
        {
            SetMenu(st->hwnd, CreateTailWindowMenu(st, false, st->statusHidden));
            DrawMenuBar(st->hwnd);
        }
        TailLayout(st);
        return;
    case ID_TAIL_MENU_TOPMOST:
        st->alwaysOnTop = !st->alwaysOnTop;
        SetWindowPos(st->hwnd, st->alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
            0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (!st->menuHidden)
        {
            SetMenu(st->hwnd, CreateTailWindowMenu(st, false, st->statusHidden));
            DrawMenuBar(st->hwnd);
        }
        return;
    case ID_TAIL_MENU_COPY:
    {
        std::wstring text = TailGetEditSelectionOrAll(st);
        if (!text.empty()) CopyToClipboard(st->hwnd, text);
        return;
    }
    case ID_TAIL_MENU_SELECT_ALL:
        if (st->hwndEdit) SendMessageW(st->hwndEdit, EM_SETSEL, 0, -1);
        return;
    case ID_TAIL_MENU_CLOSE:
        DestroyWindow(st->hwnd);
        return;
    }
}

static LRESULT CALLBACK TailWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    TailState* st = (TailState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg)
    {
    case WM_CREATE:
    {
        st = (TailState*)((CREATESTRUCTW*)lParam)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        st->hwnd = hwnd;
        SetMenu(hwnd, CreateTailWindowMenu(st, false, st->statusHidden));
        st->hwndTab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, 100, 28, hwnd, (HMENU)(UINT_PTR)ID_TAIL_TABCTRL, GetModuleHandleW(nullptr), nullptr);
        EnsureRichEditLoaded();
        st->hwndEdit = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL,
            0, 0, 100, 100, hwnd, (HMENU)(UINT_PTR)ID_TAIL_EDIT, GetModuleHandleW(nullptr), nullptr);
        st->hwndStatus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
            0, 0, 100, 24, hwnd, (HMENU)(UINT_PTR)ID_TAIL_STATUS, GetModuleHandleW(nullptr), nullptr);
        if (st->hwndEdit)
        {
            SendMessageW(st->hwndEdit, EM_SETBKGNDCOLOR, 0, RGB(0, 0, 0));
            SendMessageW(st->hwndEdit, EM_SETEDITSTYLE, SES_EXTENDBACKCOLOR, SES_EXTENDBACKCOLOR);
        }
        HFONT font = g_app ? g_app->hFontLog : nullptr;
        if (font)
        {
            TailApplyMainLogFontToEdit(st->hwndEdit);
            g_lastAppliedTailFont = g_app->logStyle.font;
            g_haveLastAppliedTailFont = true;
            TailSetCharFormat(st->hwndEdit, g_app->logStyle.textColor, RGB(0, 0, 0), false);
            SendMessageW(st->hwndStatus, WM_SETFONT, (WPARAM)GetPopupUIFont(st->hwndStatus), TRUE);
            SendMessageW(st->hwndTab, WM_SETFONT, (WPARAM)font, TRUE);
        }
        SetPropW(st->hwndEdit, L"TailState", st);
        st->oldEditProc = (WNDPROC)SetWindowLongPtrW(st->hwndEdit, GWLP_WNDPROC, (LONG_PTR)TailEditSubclassProc);
        TailRebuildTabs(st);
        TailLayout(st);
        TailReloadActiveMode(st);
        if (!g_tailTimerOwner || !IsWindow(g_tailTimerOwner))
        {
            g_tailTimerOwner = hwnd;
            SetTimer(hwnd, ID_TIMER_TAIL_REFRESH, 1000, nullptr);
        }
        return 0;
    }
    case WM_SIZE:
        TailLayout(st);
        return 0;
    case WM_EXITSIZEMOVE:
        TailSnapToNearbyWindows(st);
        return 0;
    case WM_TIMER:
        if (wParam == ID_TIMER_TAIL_REFRESH)
        {
            // buildfix34: 모든 갈무리 보기창이 각자 타이머를 돌리지 않고,
            // 대표 창 하나의 공통 tail 타이머가 열린 갈무리창을 순서대로 갱신합니다.
            std::vector<TailState*> snapshot = g_tailWindows;
            for (auto* tw : snapshot)
                TailReadNewData(tw, false);
            return 0;
        }
        break;
    case WM_NOTIFY:
        if (st && ((LPNMHDR)lParam)->hwndFrom == st->hwndTab && ((LPNMHDR)lParam)->code == TCN_SELCHANGE)
        {
            int idx = TabCtrl_GetCurSel(st->hwndTab);
            if (idx >= 0)
            {
                TCITEMW item{}; item.mask = TCIF_PARAM;
                if (TabCtrl_GetItem(st->hwndTab, idx, &item))
                    TailSetActiveMode(st, (int)item.lParam);
            }
            return 0;
        }
        break;
    case WM_COMMAND:
        TailHandleCommand(st, LOWORD(wParam));
        return 0;
    case WM_CONTEXTMENU:
        if (st)
        {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (pt.x == -1 && pt.y == -1)
            {
                RECT rc{}; GetWindowRect(hwnd, &rc); pt.x = rc.left + 20; pt.y = rc.top + 20;
            }
            TailShowContextMenu(st, pt);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (st)
        {
            KillTimer(hwnd, ID_TIMER_TAIL_REFRESH);
            if (st->hwndEdit)
            {
                RemovePropW(st->hwndEdit, L"TailState");
                if (st->oldEditProc) SetWindowLongPtrW(st->hwndEdit, GWLP_WNDPROC, (LONG_PTR)st->oldEditProc);
            }
            bool wasTimerOwner = (hwnd == g_tailTimerOwner);
            if (wasTimerOwner)
            {
                KillTimer(hwnd, ID_TIMER_TAIL_REFRESH);
                g_tailTimerOwner = nullptr;
            }
            g_tailWindows.erase(std::remove(g_tailWindows.begin(), g_tailWindows.end(), st), g_tailWindows.end());
            if (wasTimerOwner && !g_tailWindows.empty())
            {
                for (auto* next : g_tailWindows)
                {
                    if (next && next->hwnd && IsWindow(next->hwnd))
                    {
                        g_tailTimerOwner = next->hwnd;
                        SetTimer(next->hwnd, ID_TIMER_TAIL_REFRESH, 1000, nullptr);
                        break;
                    }
                }
            }
            delete st;
            if (g_app && g_app->hwndMain)
            {
                CreateMainMenu(g_app->hwndMain);
                InvalidateRect(g_app->hwndMain, nullptr, FALSE);
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

struct TailFilterPromptState
{
    TailFilterSettings filters;
    bool accepted = false;
};

static void SetEditText(HWND hwnd, int id, const std::wstring& text)
{
    SetWindowTextW(GetDlgItem(hwnd, id), text.c_str());
}

static std::wstring GetEditText(HWND hwnd, int id)
{
    HWND hEdit = GetDlgItem(hwnd, id);
    int len = GetWindowTextLengthW(hEdit);
    std::wstring s(len, L'\0');
    if (len > 0)
        GetWindowTextW(hEdit, &s[0], len + 1);
    return s;
}

static void CreateLabel(HWND hwnd, const wchar_t* text, int x, int y, int w, int h)
{
    CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
        x, y, w, h, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
}

static HWND CreateFilterEdit(HWND hwnd, int id, const std::wstring& text, int x, int y, int w, int h)
{
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        x, y, w, h, hwnd, (HMENU)(UINT_PTR)id, GetModuleHandleW(nullptr), nullptr);
}

static HWND CreateAnsiCheck(HWND hwnd, int id, bool /*checked*/, int x, int y)
{
    // buildfix34: 기존 ANSI RichEdit 색상 출력은 안정성 문제로 잠시 비활성화합니다.
    // 체크박스는 위치/설정 호환을 위해 남기되, 사용자가 켤 수 없도록 회색 처리합니다.
    HWND chk = CreateWindowExW(0, L"BUTTON", L"ANSI 보기(보류)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_DISABLED,
        x, y, 125, 22, hwnd, (HMENU)(UINT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(chk, BM_SETCHECK, BST_UNCHECKED, 0);
    return chk;
}

[[maybe_unused]] static bool IsDlgChecked(HWND hwnd, int id)
{
    return SendMessageW(GetDlgItem(hwnd, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static LRESULT CALLBACK TailFilterWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    TailFilterPromptState* st = (TailFilterPromptState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg)
    {
    case WM_CREATE:
    {
        st = (TailFilterPromptState*)((CREATESTRUCTW*)lParam)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        ApplyPopupTitleBarTheme(hwnd);
        CreateLabel(hwnd, L"세미콜론(;)으로 여러 문자열을 구분합니다. 정규식은 쓰지 않고 단순 포함 검색만 합니다.", 12, 10, 690, 22);
        CreateLabel(hwnd, L"예: 잡담 :;잡담:;속삭;귓속말", 12, 32, 690, 22);
        int y = 66; const int labelX = 14; const int editX = 115; const int editW = 455; const int chkX = 585; const int rowH = 30;
        CreateLabel(hwnd, L"전체", labelX, y + 3, 90, 22); CreateLabel(hwnd, L"전체 보기에는 필터를 적용하지 않습니다.", editX, y + 3, editW, 22); CreateAnsiCheck(hwnd, ID_TAIL_FILTER_ANSI_ALL, st->filters.ansiAll, chkX, y + 1); y += rowH;
        CreateLabel(hwnd, L"잡담", labelX, y + 3, 90, 22); CreateFilterEdit(hwnd, ID_TAIL_FILTER_CHAT, st->filters.chat, editX, y, editW, 24); CreateAnsiCheck(hwnd, ID_TAIL_FILTER_ANSI_CHAT, st->filters.ansiChat, chkX, y + 1); y += rowH;
        CreateLabel(hwnd, L"대화", labelX, y + 3, 90, 22); CreateFilterEdit(hwnd, ID_TAIL_FILTER_TALK, st->filters.talk, editX, y, editW, 24); CreateAnsiCheck(hwnd, ID_TAIL_FILTER_ANSI_TALK, st->filters.ansiTalk, chkX, y + 1); y += rowH;
        CreateLabel(hwnd, L"경매", labelX, y + 3, 90, 22); CreateFilterEdit(hwnd, ID_TAIL_FILTER_AUCTION, st->filters.auction, editX, y, editW, 24); CreateAnsiCheck(hwnd, ID_TAIL_FILTER_ANSI_AUCTION, st->filters.ansiAuction, chkX, y + 1); y += rowH;
        CreateLabel(hwnd, L"아이템 획득", labelX, y + 3, 90, 22); CreateFilterEdit(hwnd, ID_TAIL_FILTER_ITEM, st->filters.item, editX, y, editW, 24); CreateAnsiCheck(hwnd, ID_TAIL_FILTER_ANSI_ITEM, st->filters.ansiItem, chkX, y + 1); y += rowH;
        CreateLabel(hwnd, L"경험치", labelX, y + 3, 90, 22); CreateFilterEdit(hwnd, ID_TAIL_FILTER_EXP, st->filters.exp, editX, y, editW, 24); CreateAnsiCheck(hwnd, ID_TAIL_FILTER_ANSI_EXP, st->filters.ansiExp, chkX, y + 1); y += rowH + 8;
        CreateLabel(hwnd, L"사용자 정의 필터", 12, y, 180, 22); y += 26;
        CreateLabel(hwnd, L"이름", 115, y, 120, 20); CreateLabel(hwnd, L"패턴", 250, y, 315, 20); CreateLabel(hwnd, L"표시", chkX, y, 90, 20); y += 22;
        CreateLabel(hwnd, L"사용자1", labelX, y + 3, 90, 22); CreateFilterEdit(hwnd, ID_TAIL_FILTER_USER1_NAME, st->filters.userName1, 115, y, 120, 24); CreateFilterEdit(hwnd, ID_TAIL_FILTER_USER1_PATTERN, st->filters.userPattern1, 250, y, 320, 24); CreateAnsiCheck(hwnd, ID_TAIL_FILTER_ANSI_USER1, st->filters.ansiUser1, chkX, y + 1); y += rowH;
        CreateLabel(hwnd, L"사용자2", labelX, y + 3, 90, 22); CreateFilterEdit(hwnd, ID_TAIL_FILTER_USER2_NAME, st->filters.userName2, 115, y, 120, 24); CreateFilterEdit(hwnd, ID_TAIL_FILTER_USER2_PATTERN, st->filters.userPattern2, 250, y, 320, 24); CreateAnsiCheck(hwnd, ID_TAIL_FILTER_ANSI_USER2, st->filters.ansiUser2, chkX, y + 1); y += rowH;
        CreateLabel(hwnd, L"사용자3", labelX, y + 3, 90, 22); CreateFilterEdit(hwnd, ID_TAIL_FILTER_USER3_NAME, st->filters.userName3, 115, y, 120, 24); CreateFilterEdit(hwnd, ID_TAIL_FILTER_USER3_PATTERN, st->filters.userPattern3, 250, y, 320, 24); CreateAnsiCheck(hwnd, ID_TAIL_FILTER_ANSI_USER3, st->filters.ansiUser3, chkX, y + 1); y += rowH + 4;
        CreateLabel(hwnd, L"ANSI 보기는 안정성 문제로 임시 보류 중입니다. 현재 갈무리 보기는 ANSI 코드를 제거한 평문으로 표시합니다.", 12, y, 690, 22); y += 22;
        CreateLabel(hwnd, L"잡담 특수값: []: 는 [이름]: 형식, <>: 는 <이름>: 형식입니다.", 12, y, 690, 22);
        CreateWindowExW(0, L"BUTTON", L"기본값", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 410, 470, 80, 28, hwnd, (HMENU)(UINT_PTR)ID_TAIL_FILTER_RESET, GetModuleHandleW(nullptr), nullptr);
        CreateWindowExW(0, L"BUTTON", L"확인", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 505, 470, 80, 28, hwnd, (HMENU)(UINT_PTR)ID_TAIL_FILTER_OK, GetModuleHandleW(nullptr), nullptr);
        CreateWindowExW(0, L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 600, 470, 80, 28, hwnd, (HMENU)(UINT_PTR)ID_TAIL_FILTER_CANCEL, GetModuleHandleW(nullptr), nullptr);
        ApplyPopupFontToChildren(hwnd);
        SetFocus(GetDlgItem(hwnd, ID_TAIL_FILTER_CHAT));
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TAIL_FILTER_RESET)
        {
            TailFilterSettings def;
            SetEditText(hwnd, ID_TAIL_FILTER_CHAT, def.chat); SetEditText(hwnd, ID_TAIL_FILTER_TALK, def.talk); SetEditText(hwnd, ID_TAIL_FILTER_AUCTION, def.auction); SetEditText(hwnd, ID_TAIL_FILTER_ITEM, def.item); SetEditText(hwnd, ID_TAIL_FILTER_EXP, def.exp);
            SetEditText(hwnd, ID_TAIL_FILTER_USER1_NAME, def.userName1); SetEditText(hwnd, ID_TAIL_FILTER_USER1_PATTERN, def.userPattern1); SetEditText(hwnd, ID_TAIL_FILTER_USER2_NAME, def.userName2); SetEditText(hwnd, ID_TAIL_FILTER_USER2_PATTERN, def.userPattern2); SetEditText(hwnd, ID_TAIL_FILTER_USER3_NAME, def.userName3); SetEditText(hwnd, ID_TAIL_FILTER_USER3_PATTERN, def.userPattern3);
            SendMessageW(GetDlgItem(hwnd, ID_TAIL_FILTER_ANSI_ALL), BM_SETCHECK, def.ansiAll ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(GetDlgItem(hwnd, ID_TAIL_FILTER_ANSI_CHAT), BM_SETCHECK, def.ansiChat ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(GetDlgItem(hwnd, ID_TAIL_FILTER_ANSI_TALK), BM_SETCHECK, def.ansiTalk ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(GetDlgItem(hwnd, ID_TAIL_FILTER_ANSI_AUCTION), BM_SETCHECK, def.ansiAuction ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(GetDlgItem(hwnd, ID_TAIL_FILTER_ANSI_ITEM), BM_SETCHECK, def.ansiItem ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(GetDlgItem(hwnd, ID_TAIL_FILTER_ANSI_EXP), BM_SETCHECK, def.ansiExp ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(GetDlgItem(hwnd, ID_TAIL_FILTER_ANSI_USER1), BM_SETCHECK, def.ansiUser1 ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(GetDlgItem(hwnd, ID_TAIL_FILTER_ANSI_USER2), BM_SETCHECK, def.ansiUser2 ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(GetDlgItem(hwnd, ID_TAIL_FILTER_ANSI_USER3), BM_SETCHECK, def.ansiUser3 ? BST_CHECKED : BST_UNCHECKED, 0);
            return 0;
        }
        if (LOWORD(wParam) == ID_TAIL_FILTER_OK)
        {
            st->filters.chat = GetEditText(hwnd, ID_TAIL_FILTER_CHAT); st->filters.talk = GetEditText(hwnd, ID_TAIL_FILTER_TALK); st->filters.auction = GetEditText(hwnd, ID_TAIL_FILTER_AUCTION); st->filters.item = GetEditText(hwnd, ID_TAIL_FILTER_ITEM); st->filters.exp = GetEditText(hwnd, ID_TAIL_FILTER_EXP);
            st->filters.userName1 = GetEditText(hwnd, ID_TAIL_FILTER_USER1_NAME); st->filters.userPattern1 = GetEditText(hwnd, ID_TAIL_FILTER_USER1_PATTERN); st->filters.userName2 = GetEditText(hwnd, ID_TAIL_FILTER_USER2_NAME); st->filters.userPattern2 = GetEditText(hwnd, ID_TAIL_FILTER_USER2_PATTERN); st->filters.userName3 = GetEditText(hwnd, ID_TAIL_FILTER_USER3_NAME); st->filters.userPattern3 = GetEditText(hwnd, ID_TAIL_FILTER_USER3_PATTERN);
            st->filters.ansiAll = false;
            st->filters.ansiChat = false;
            st->filters.ansiTalk = false;
            st->filters.ansiAuction = false;
            st->filters.ansiItem = false;
            st->filters.ansiExp = false;
            st->filters.ansiUser1 = false;
            st->filters.ansiUser2 = false;
            st->filters.ansiUser3 = false;
            st->accepted = true; DestroyWindow(hwnd); return 0;
        }
        if (LOWORD(wParam) == ID_TAIL_FILTER_CANCEL) { DestroyWindow(hwnd); return 0; }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void PromptTailFilterSettings(HWND owner)
{
    LoadTailFilterSettings();
    WNDCLASSW wc{};
    wc.lpfnWndProc = TailFilterWndProc; wc.hInstance = GetModuleHandleW(nullptr); wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = kTailFilterWndClass; RegisterClassW(&wc);
    TailFilterPromptState st; st.filters = g_tailFilters;
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kTailFilterWndClass, L"갈무리 필터 설정", WS_CAPTION | WS_SYSMENU | WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 720, 550, owner, nullptr, GetModuleHandleW(nullptr), &st);
    if (!hwnd) return;
    CenterPopupToOwner(hwnd, owner);
    EnableWindow(owner, FALSE); ShowWindow(hwnd, SW_SHOW); UpdateWindow(hwnd);
    MSG msg; while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0)) { if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    EnableWindow(owner, TRUE); SetForegroundWindow(owner);
    if (st.accepted)
    {
        g_tailFilters = st.filters; g_tailFiltersLoaded = true; SaveTailFilterSettings();
        if (g_app && g_app->hwndMain) CreateMainMenu(g_app->hwndMain);
        for (auto* tw : g_tailWindows)
        {
            TailRebuildTabs(tw); TailReloadActiveMode(tw);
        }
    }
}

struct PatternPromptState { std::wstring value; bool accepted = false; };

static LRESULT CALLBACK TailPatternWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PatternPromptState* st = (PatternPromptState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg)
    {
    case WM_CREATE:
    {
        st = (PatternPromptState*)((CREATESTRUCTW*)lParam)->lpCreateParams; SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st); ApplyPopupTitleBarTheme(hwnd);
        CreateWindowExW(0, L"STATIC", L"이번 보기에서만 찾을 임시 문자열을 입력하세요. 정규식이 아니라 단순 포함 검색입니다.", WS_CHILD | WS_VISIBLE, 12, 12, 380, 22, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", st->value.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 12, 42, 380, 24, hwnd, (HMENU)ID_TAIL_PATTERN_EDIT, GetModuleHandleW(nullptr), nullptr);
        CreateWindowExW(0, L"BUTTON", L"확인", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 216, 78, 80, 28, hwnd, (HMENU)ID_TAIL_PATTERN_OK, GetModuleHandleW(nullptr), nullptr);
        CreateWindowExW(0, L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE, 310, 78, 80, 28, hwnd, (HMENU)ID_TAIL_PATTERN_CANCEL, GetModuleHandleW(nullptr), nullptr);
        ApplyPopupFontToChildren(hwnd); SetFocus(edit); SendMessageW(edit, EM_SETSEL, 0, -1); return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TAIL_PATTERN_OK) { wchar_t buf[1024] = {}; GetWindowTextW(GetDlgItem(hwnd, ID_TAIL_PATTERN_EDIT), buf, 1024); st->value = buf; st->accepted = true; DestroyWindow(hwnd); return 0; }
        if (LOWORD(wParam) == ID_TAIL_PATTERN_CANCEL) { DestroyWindow(hwnd); return 0; }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool PromptTailPattern(HWND owner, std::wstring& value)
{
    WNDCLASSW wc{}; wc.lpfnWndProc = TailPatternWndProc; wc.hInstance = GetModuleHandleW(nullptr); wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = kTailPatternWndClass; RegisterClassW(&wc);
    PatternPromptState st; st.value = value;
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kTailPatternWndClass, L"임시 갈무리 문자열", WS_CAPTION | WS_SYSMENU | WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 420, 150, owner, nullptr, GetModuleHandleW(nullptr), &st);
    if (!hwnd) return false;
    CenterPopupToOwner(hwnd, owner); EnableWindow(owner, FALSE); ShowWindow(hwnd, SW_SHOW); UpdateWindow(hwnd);
    MSG msg; while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0)) { if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    EnableWindow(owner, TRUE); SetForegroundWindow(owner);
    if (st.accepted) value = st.value;
    return st.accepted;
}

std::wstring GetTailModeMenuTitle(int mode)
{
    LoadTailFilterSettings();
    return TailModeTitleRaw(mode);
}

void ShowCaptureTailWindow(HWND owner, int mode)
{
    LoadTailFilterSettings();
    if (!EnsureCaptureLogStarted(owner)) return;
    std::wstring custom;
    if (mode == 4)
    {
        if (!PromptTailPattern(owner, custom)) return;
    }
    EnsureRichEditLoaded();
    WNDCLASSW wc{}; wc.lpfnWndProc = TailWndProc; wc.hInstance = GetModuleHandleW(nullptr); wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.lpszClassName = kTailWndClass; RegisterClassW(&wc);

    TailState* st = new TailState();
    st->activeMode = mode;
    st->visibleModes = DefaultTailModesFor(mode);
    st->customPattern = custom;
    st->logPath = g_app ? g_app->captureLogPath : L"";

    std::wstring title = L"KTin 갈무리 보기 - " + TailModeTitleRaw(mode);
    if (mode == 4 && !custom.empty()) title += L" : " + custom;
    HWND hwnd = CreateWindowExW(0, kTailWndClass, title.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 820, 520, owner, nullptr, GetModuleHandleW(nullptr), st);
    if (!hwnd)
    {
        delete st;
        return;
    }
    g_tailWindows.push_back(st);
    if (g_app && g_app->hwndMain)
    {
        CreateMainMenu(g_app->hwndMain);
        InvalidateRect(g_app->hwndMain, nullptr, FALSE);
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
}
