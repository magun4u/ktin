// utils.cpp
#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "theme.h"
#include "settings.h"
#include "resource.h"
#include "app_version.h"
#include "abbreviation.h"
#include "timer.h"
#include "memo.h"
#include "auto_login.h"
#include "win_util.h"
#include <commdlg.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <shlwapi.h>
#include <algorithm>
#include <limits>
static LRESULT CALLBACK CenterMsgBoxHookProc(int nCode, WPARAM wParam, LPARAM lParam);
// ==============================================
// 전역 변수
// ==============================================
HHOOK g_hMsgBoxHook = nullptr;
HWND g_hMsgBoxOwner = nullptr;

static HFONT g_popupUiFont = nullptr;
static HFONT g_shortcutButtonFont = nullptr;

#ifdef _MSC_VER
#pragma comment(lib, "Version.lib")
#endif

// ==============================================
// 파일 쓰기 유틸
// ==============================================
bool WriteAllToWinFile(HANDLE file, const void* data, size_t len)
{
    if (file == INVALID_HANDLE_VALUE || !file)
        return false;
    if (len == 0)
        return true;
    if (!data)
        return false;

    const char* ptr = static_cast<const char*>(data);
    size_t left = len;
    while (left > 0)
    {
        const DWORD block = static_cast<DWORD>(
            std::min<size_t>(left, static_cast<size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (!WriteFile(file, ptr, block, &written, nullptr) || written == 0)
            return false;
        ptr += written;
        left -= written;
    }
    return true;
}

bool WriteBytesToFile(const std::wstring& path, const void* data, size_t len, bool append)
{
    const DWORD access = append ? FILE_APPEND_DATA : GENERIC_WRITE;
    const DWORD creation = append ? OPEN_ALWAYS : CREATE_ALWAYS;
    UniqueHandle file(CreateFileW(path.c_str(), access, FILE_SHARE_READ, nullptr,
        creation, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.IsValid())
        return false;

    if (append)
    {
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(file.Get(), zero, nullptr, FILE_END))
            return false;
    }

    return WriteAllToWinFile(file.Get(), data, len);
}

bool WriteStringToFile(const std::wstring& path, const std::string& data, bool append)
{
    return WriteBytesToFile(path, data.data(), data.size(), append);
}

bool WriteUtf8BomTextFile(const std::wstring& path, const std::string& utf8)
{
    UniqueHandle file(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.IsValid())
        return false;

    static const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    if (!WriteAllToWinFile(file.Get(), bom, sizeof(bom)))
        return false;
    if (!utf8.empty() && !WriteAllToWinFile(file.Get(), utf8.data(), utf8.size()))
        return false;
    return true;
}

// ==============================================
// 1. 문자열 처리 유틸
// ==============================================
std::wstring Trim(const std::wstring& s)
{
    size_t b = 0;
    while (b < s.size() && iswspace(static_cast<unsigned short>(s[b])))
        ++b;
    size_t e = s.size();
    while (e > b && iswspace(static_cast<unsigned short>(s[e - 1])))
        --e;
    return s.substr(b, e - b);
}

std::wstring FormatNumberWithCommas(const std::wstring& s)
{
    if (s.empty()) return s;
    bool isNumeric = true;
    for (size_t i = 0; i < s.length(); ++i) {
        if (!iswdigit(s[i]) && !(i == 0 && s[i] == L'-')) {
            isNumeric = false;
            break;
        }
    }
    if (!isNumeric) return s;

    std::wstring res = s;
    int pos = (int)res.length() - 3;
    while (pos > 0) {
        if (res[pos - 1] == L'-') break;
        res.insert(pos, L",");
        pos -= 3;
    }
    return res;
}

// ==============================================
// 2. 인코딩 변환 유틸
// ==============================================
std::string WideToUtf8(const std::wstring& ws)
{
    if (ws.empty()) return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return std::string();
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), &out[0], size, nullptr, nullptr);
    return out;
}

std::string WideToMultiByte(const std::wstring& ws, UINT codePage)
{
    if (ws.empty()) return "";
    int size = WideCharToMultiByte(codePage, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(codePage, 0, ws.c_str(), (int)ws.size(), &out[0], size, nullptr, nullptr);
    return out;
}

std::wstring MultiByteToWide(const std::string& s, UINT codePage)
{
    if (s.empty()) return L"";
    int size = MultiByteToWideChar(codePage, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(codePage, 0, s.data(), (int)s.size(), &out[0], size);
    return out;
}

std::wstring Utf8ToWide(const std::string& s)
{
    return MultiByteToWide(s, CP_UTF8);
}

// ==============================================
// 3. 경로 관련 유틸
// ==============================================
std::wstring GetModuleDirectory()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) *slash = 0;
    return path;
}

std::wstring MakeAbsolutePath(const std::wstring& baseDir, const std::wstring& relativePath)
{
    std::wstring combined = baseDir;
    if (!combined.empty() && combined.back() != L'\\' && combined.back() != L'/')
        combined += L'\\';
    combined += relativePath;
    wchar_t full[MAX_PATH] = {};
    GetFullPathNameW(combined.c_str(), MAX_PATH, full, nullptr);
    return full;
}

std::wstring GetSessionsPath()
{
    return MakeAbsolutePath(GetModuleDirectory(), L"sessions.ini");
}

std::wstring GetSettingsPath()
{
    return MakeAbsolutePath(GetModuleDirectory(), L"config.ini");
}

// ==============================================
// 4. 클립보드 / 파일 저장 / 사운드
// ==============================================
bool SetClipboardUnicodeText(HWND hwnd, const std::wstring& text)
{
    if (text.empty() || !OpenClipboard(hwnd))
        return false;

    bool ok = false;
    HGLOBAL hglb = nullptr;

    do
    {
        if (!EmptyClipboard())
            break;

        const size_t size = (text.size() + 1) * sizeof(wchar_t);
        hglb = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!hglb)
            break;

        void* dst = GlobalLock(hglb);
        if (!dst)
            break;

        memcpy(dst, text.c_str(), size);
        GlobalUnlock(hglb);

        if (!SetClipboardData(CF_UNICODETEXT, hglb))
            break;

        hglb = nullptr;
        ok = true;
    } while (false);

    if (hglb)
        GlobalFree(hglb);

    CloseClipboard();
    return ok;
}

void CopyToClipboard(HWND hwnd, const std::wstring& text)
{
    if (SetClipboardUnicodeText(hwnd, text))
        MessageBoxW(hwnd, L"클립보드에 복사되었습니다.", L"알림", MB_OK | MB_ICONINFORMATION);
}

void SaveTextToFile(HWND hwnd, const std::wstring& text)
{
    if (text.empty()) {
        MessageBoxW(hwnd, L"저장할 내용이 없습니다.", L"알림", MB_OK | MB_ICONWARNING);
        return;
    }
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
    if (GetSaveFileNameW(&ofn)) {
        const bool ok = WriteUtf8BomTextFile(fileName, WideToUtf8(text));

        MessageBoxW(hwnd,
            ok ? L"파일이 성공적으로 저장되었습니다." : L"파일 저장 중 오류가 발생했습니다.",
            ok ? L"저장 완료" : L"저장 실패",
            MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
    }
}

void PlayAudioFile(const std::wstring& path)
{
    if (path.empty()) return;
    std::wstring ext = path.length() >= 4 ? path.substr(path.length() - 4) : L"";
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    if (ext == L".mp3") {
        mciSendStringW(L"close hi_sound", nullptr, 0, nullptr);
        std::wstring cmdOpen = L"open \"" + path + L"\" alias hi_sound";
        mciSendStringW(cmdOpen.c_str(), nullptr, 0, nullptr);
        mciSendStringW(L"play hi_sound", nullptr, 0, nullptr);
    }
    else {
        PlaySoundW(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
    }
}

// ==============================================
// 5. 컨트롤 헬퍼
// ==============================================
std::wstring GetEditTextW(HWND hEdit)
{
    int len = GetWindowTextLengthW(hEdit);
    std::wstring s(len, L'\0');
    if (len > 0)
        GetWindowTextW(hEdit, &s[0], len + 1);
    return s;
}

std::wstring GetWindowTextString(HWND hwnd)
{
    int len = GetWindowTextLengthW(hwnd);
    std::wstring s(len, 0);
    GetWindowTextW(hwnd, &s[0], len + 1);
    return s;
}

// ==============================================
// 6. 폰트 / UI 테마 관련
// ==============================================
HFONT GetPopupUIFont(HWND hwnd)
{
    (void)hwnd;

    if (g_popupUiFont) return g_popupUiFont;
    LOGFONTW lf = {};
    GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf);
    g_popupUiFont = CreateFontIndirectW(&lf);
    return g_popupUiFont;
}

HFONT GetShortcutButtonUIFont(HWND hwnd)
{
    (void)hwnd;

    if (g_shortcutButtonFont) return g_shortcutButtonFont;
    LOGFONTW lf = {};
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    lf = ncm.lfMenuFont;
    g_shortcutButtonFont = CreateFontIndirectW(&lf);
    return g_shortcutButtonFont;
}

void CleanupCachedUiFonts()
{
    if (g_popupUiFont) {
        ResetGdiObjectRef(g_popupUiFont);
    }
    if (g_shortcutButtonFont) {
        ResetGdiObjectRef(g_shortcutButtonFont);
    }
}

void ApplyPopupTitleBarTheme(HWND hwnd)
{
    if (!hwnd) return;
    BOOL dark = FALSE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    COLORREF capColor = RGB(240, 240, 240);
    COLORREF textColor = RGB(0, 0, 0);
    COLORREF borderColor = RGB(180, 180, 180);
    DwmSetWindowAttribute(hwnd, 35, &capColor, sizeof(capColor));
    DwmSetWindowAttribute(hwnd, 36, &textColor, sizeof(textColor));
    DwmSetWindowAttribute(hwnd, 34, &borderColor, sizeof(borderColor));
}

LONG MakeLfHeightFromPointSize(HWND hwnd, int pt)
{
    ScopedWindowDC dc(hwnd);
    const int dpi = dc ? GetDeviceCaps(dc.Get(), LOGPIXELSY) : 96;
    return -MulDiv(pt, dpi, 72);
}

int GetFontPointSizeFromLogFont(const LOGFONTW& lf)
{
    ScopedWindowDC dc(nullptr);
    const int dpi = dc ? GetDeviceCaps(dc.Get(), LOGPIXELSY) : 96;
    int height = lf.lfHeight;
    if (height < 0) height = -height;
    return MulDiv(height, 72, dpi);
}

// ==============================================
// 7. 대화상자 헬퍼
// ==============================================
// utils.cpp 또는 해당 함수가 정의된 파일
bool ChooseFontOnly(HWND hwnd, LOGFONTW& font)
{
    CHOOSEFONTW cf = {};
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = hwnd;
    
    // 1. 대화상자에 현재 폰트 정보를 넘겨줍니다.
    cf.lpLogFont = &font; 

    // 2. 핵심 플래그 설정
    // CF_SCREENFONTS: 화면용 폰트들을 리스트에 표시
    // CF_INITTOLOGFONTSTRUCT: ★ 중요! lpLogFont에 담긴 정보(Mud둥근모 등)로 대화상자를 초기화함
    // CF_EFFECTS: 취소선, 밑줄, 색상 등 추가 효과를 표시 (필요 없다면 빼도 무방합니다)
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_EFFECTS;

    // 3. 사용자가 [확인]을 누르면 true, [취소]를 누르면 false 반환
    if (ChooseFontW(&cf))
    {
        return true;
    }
    
    return false;
}

bool ChooseColorOnly(HWND hwnd, COLORREF& color)
{
    static COLORREF customColors[16] = {};
    CHOOSECOLORW cc = {};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwnd;
    cc.rgbResult = color;
    cc.lpCustColors = customColors;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&cc))
    {
        color = cc.rgbResult;
        return true;
    }
    return false;
}

bool ChooseBackgroundColor(HWND hwnd, COLORREF& color)
{
    return ChooseColorOnly(hwnd, color);
}

bool ChooseScriptFile(HWND hwnd, std::wstring& outPath)
{
    wchar_t fileName[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"스크립트 파일 (*.tin;*.txt)\0*.tin;*.txt\0모든 파일 (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return false;
    outPath = fileName;
    return true;
}

bool ChooseFontAndColor(HWND hwnd, UiStyle& style)
{
    CHOOSEFONTW cf = {};
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = hwnd;
    cf.lpLogFont = &style.font;
    cf.rgbColors = style.textColor;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_EFFECTS;
    if (ChooseFontW(&cf))
    {
        style.textColor = cf.rgbColors;
        return true;
    }
    return false;
}

// ==============================================
// 8. 기타 유틸 (원본에 있던 모든 함수 포함)
// ==============================================
int GetStatusBarHeight()
{
    return STATUS_BAR_HEIGHT;
}

int ClampInt(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

int ClampByteRange(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

int ShowCenteredMessageBox(HWND hwnd, const wchar_t* text, const wchar_t* caption, UINT type)
{
    g_hMsgBoxOwner = hwnd;
    g_hMsgBoxHook = SetWindowsHookExW(WH_CBT, CenterMsgBoxHookProc, nullptr, GetCurrentThreadId());
    return MessageBoxW(hwnd, text, caption, type);
}

static LRESULT CALLBACK CenterMsgBoxHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HCBT_ACTIVATE)
    {
        HWND hMsgBox = (HWND)wParam;
        if (g_hMsgBoxOwner)
        {
            RECT rcOwner, rcMsg;
            GetWindowRect(g_hMsgBoxOwner, &rcOwner);
            GetWindowRect(hMsgBox, &rcMsg);
            int msgW = rcMsg.right - rcMsg.left;
            int msgH = rcMsg.bottom - rcMsg.top;
            int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - msgW) / 2;
            int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - msgH) / 2;
            SetWindowPos(hMsgBox, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        UnhookWindowsHookEx(g_hMsgBoxHook);
        g_hMsgBoxHook = nullptr;
    }
    return CallNextHookEx(g_hMsgBoxHook, nCode, wParam, lParam);
}

void MeasureOwnerDrawMenuItem(HWND hwnd, MEASUREITEMSTRUCT* mis)
{
    if (!mis || mis->CtlType != ODT_MENU) return;
    const wchar_t* text = reinterpret_cast<const wchar_t*>(mis->itemData);
    if (!text || !*text) { mis->itemWidth = 20; mis->itemHeight = 10; return; }
    ScopedWindowDC dc(hwnd);
    if (!dc) { mis->itemWidth = 60; mis->itemHeight = 26; return; }

    HDC hdc = dc.Get();
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    ScopedSelectObject fontSel(hdc, hFont);
    RECT rc = { 0, 0, 0, 0 };
    DrawTextW(hdc, text, -1, &rc, DT_SINGLELINE | DT_CALCRECT | DT_EXPANDTABS);
    mis->itemWidth = (UINT)RectWidth(rc) + 40;
    mis->itemHeight = (UINT)RectHeight(rc) + 10;
    if (mis->itemHeight < 26) mis->itemHeight = 26;
}

void DrawOwnerDrawMenuItem(DRAWITEMSTRUCT* dis)
{
    if (!dis || dis->CtlType != ODT_MENU) return;
    const wchar_t* text = reinterpret_cast<const wchar_t*>(dis->itemData);
    if (!text) text = L"";
    bool selected = (dis->itemState & ODS_SELECTED) != 0;
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    COLORREF bg = selected ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_BTNFACE);
    COLORREF fg = disabled ? GetSysColor(COLOR_GRAYTEXT)
        : (selected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_MENUTEXT));
    UniqueGdiObject hbr(CreateSolidBrush(bg));
    if (hbr.IsValid())
        FillRect(dis->hDC, &dis->rcItem, (HBRUSH)hbr.Get());
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, fg);
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    ScopedSelectObject fontSel(dis->hDC, hFont);
    RECT rcText = dis->rcItem;
    rcText.left += 12;
    rcText.right -= 16;
    bool hasSubmenu = false;
    if (g_app && g_app->hMainMenu)
    {
        if (!FindOwnerDrawMenuMeta(g_app->hMainMenu, dis->itemData, &hasSubmenu))
        {
            int topCount = GetMenuItemCount(g_app->hMainMenu);
            for (int i = 0; i < topCount; ++i)
            {
                HMENU hSub = GetSubMenu(g_app->hMainMenu, i);
                if (FindOwnerDrawMenuMeta(hSub, dis->itemData, &hasSubmenu)) break;
                if (hSub)
                {
                    int subCount = GetMenuItemCount(hSub);
                    for (int j = 0; j < subCount; ++j)
                    {
                        HMENU hSub2 = GetSubMenu(hSub, j);
                        if (FindOwnerDrawMenuMeta(hSub2, dis->itemData, &hasSubmenu)) break;
                    }
                }
                if (hasSubmenu) break;
            }
        }
    }
    if (hasSubmenu)
        rcText.right -= 14;
    const wchar_t* tab = wcschr(text, L'\t');
    if (tab)
    {
        std::wstring left(text, tab - text);
        std::wstring right(tab + 1);
        RECT rcLeft = rcText;
        RECT rcRight = rcText;
        rcRight.left = rcText.right - 80;
        DrawTextW(dis->hDC, left.c_str(), -1, &rcLeft, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        DrawTextW(dis->hDC, right.c_str(), -1, &rcRight, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
    }
    else
    {
        DrawTextW(dis->hDC, text, -1, &rcText, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }
    if (hasSubmenu)
    {
        int cx = dis->rcItem.right - 12;
        int cy = (dis->rcItem.top + dis->rcItem.bottom) / 2;
        POINT pts[3] = { { cx - 4, cy - 4 }, { cx - 4, cy + 4 }, { cx + 1, cy } };
        UniqueGdiObject hArrowBrush(CreateSolidBrush(fg));
        UniqueGdiObject hArrowPen(CreatePen(PS_SOLID, 1, fg));
        ScopedSelectObject brushSel(dis->hDC, hArrowBrush.Get());
        ScopedSelectObject penSel(dis->hDC, hArrowPen.Get());
        Polygon(dis->hDC, pts, 3);
    }
    if (dis->itemState & ODS_FOCUS)
    {
        RECT rcFocus = dis->rcItem;
        rcFocus.left += 2;
        rcFocus.right -= 2;
        DrawFocusRect(dis->hDC, &rcFocus);
    }
}

bool FindOwnerDrawMenuMeta(HMENU hMenu, ULONG_PTR itemData, bool* hasSubmenu)
{
    if (!hMenu) return false;
    int count = GetMenuItemCount(hMenu);
    for (int i = 0; i < count; ++i)
    {
        MENUITEMINFOW mii = {};
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_DATA | MIIM_SUBMENU;
        if (GetMenuItemInfoW(hMenu, i, TRUE, &mii))
        {
            if (mii.dwItemData == itemData)
            {
                if (hasSubmenu) *hasSubmenu = (mii.hSubMenu != nullptr);
                return true;
            }
        }
    }
    return false;
}

void GetTerminalOffset(HWND hwnd, int& offsetX, int& offsetY)
{
    offsetX = 0;
    offsetY = 0;
    if (!g_app || !g_app->termBuffer) return;

    SIZE cell = GetLogCellPixelSize(hwnd);
    RECT rc;
    GetClientRect(hwnd, &rc);

    TerminalBufferMetrics metrics = g_app->termBuffer->GetMetrics();
    const int clientW = RectWidth(rc);
    const int clientH = RectHeight(rc);
    const int gridW = metrics.width * cell.cx;
    const int gridH = metrics.height * cell.cy;

    int ml = max(0, g_app->termMarginLeft);
    int mr = max(0, g_app->termMarginRight);
    int mt = max(0, g_app->termMarginTop);
    int mb = max(0, g_app->termMarginBottom);

    // 여백이 창보다 커서 계산이 뒤집히지 않도록 방어합니다.
    if (ml + mr > clientW) { ml = 0; mr = 0; }
    if (mt + mb > clientH) { mt = 0; mb = 0; }

    const int areaW = max(0, clientW - ml - mr);
    const int areaH = max(0, clientH - mt - mb);

    if (g_app->termAlign == 0) {
        offsetX = ml;
    }
    else if (g_app->termAlign == 2) {
        offsetX = max(ml, clientW - mr - gridW);
    }
    else {
        offsetX = ml + max(0, (areaW - gridW) / 2);
    }

    // 출력 높이가 남으면 설정된 영역 안에서 세로 중앙 정렬합니다.
    // 출력 높이가 더 크면 아래쪽 프롬프트가 보이도록 아래 여백을 보존하며 붙입니다.
    if (gridH > areaH) {
        offsetY = clientH - mb - gridH;
    }
    else {
        offsetY = mt + max(0, (areaH - gridH) / 2);
    }
}

namespace
{
struct LogCellPixelSizeCache
{
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    int dpiX = 0;
    int dpiY = 0;
    SIZE cell = { 8, 16 };
    bool valid = false;
};

LogCellPixelSizeCache g_logCellPixelSizeCache;
}

void ResetLogCellPixelSizeCache()
{
    g_logCellPixelSizeCache = {};
}

SIZE GetLogCellPixelSize(HWND hwnd)
{
    SIZE cell = { 8, 16 };

    if (!g_app || !g_app->hFontLog)
        return cell;

    HWND target = hwnd ? hwnd : g_app->hwndLog;
    if (!target)
        return cell;

    ScopedWindowDC dc(target);
    if (!dc)
        return cell;

    HDC hdc = dc.Get();
    const int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    const int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);

    if (g_logCellPixelSizeCache.valid &&
        g_logCellPixelSizeCache.hwnd == target &&
        g_logCellPixelSizeCache.font == g_app->hFontLog &&
        g_logCellPixelSizeCache.dpiX == dpiX &&
        g_logCellPixelSizeCache.dpiY == dpiY)
    {
        return g_logCellPixelSizeCache.cell;
    }

    ScopedSelectObject fontSel(hdc, g_app->hFontLog);

    TEXTMETRICW tm = {};
    GetTextMetricsW(hdc, &tm);

    SIZE szHalf = { 0, 0 };
    SIZE szBox = { 0, 0 };
    SIZE szHangul = { 0, 0 };

    // 1칸 기준 문자
    GetTextExtentPoint32W(hdc, L"M", 1, &szHalf);
    GetTextExtentPoint32W(hdc, L"─", 1, &szBox);

    // 2칸 기준 문자
    GetTextExtentPoint32W(hdc, L"가", 1, &szHangul);

    // 기본 1칸 폭은 "반각 문자" 기준으로만 잡는다.
    int cx = szHalf.cx;
    if (szBox.cx > 0 && szBox.cx < cx)
        cx = szBox.cx;

    if (cx <= 0)
        cx = tm.tmAveCharWidth;

    // 너무 작으면 한글 반폭 기준으로만 최소 보정
    int halfHangul = (szHangul.cx + 1) / 2;
    if (halfHangul > 0 && cx < halfHangul - 1)
        cx = halfHangul - 1;

    if (cx < 1)
        cx = 1;

    int cy = tm.tmHeight + tm.tmExternalLeading;
    if (szHalf.cy > cy)   cy = szHalf.cy;
    if (szBox.cy > cy)    cy = szBox.cy;
    if (szHangul.cy > cy) cy = szHangul.cy;
    if (cy < 1)
        cy = 1;

    cell.cx = cx;
    cell.cy = cy;

    g_logCellPixelSizeCache.hwnd = target;
    g_logCellPixelSizeCache.font = g_app->hFontLog;
    g_logCellPixelSizeCache.dpiX = dpiX;
    g_logCellPixelSizeCache.dpiY = dpiY;
    g_logCellPixelSizeCache.cell = cell;
    g_logCellPixelSizeCache.valid = true;

    return cell;
}

bool FitWindowToScreenGrid(HWND hwnd, int cols, int rows, bool onlyIfTooSmall)
{
    if (!g_app || !hwnd) return false;
    if (cols < 20) cols = 20;
    if (rows < 5) rows = 5;
    SIZE cell = GetLogCellPixelSize(hwnd);
    int logClientW = cell.cx * cols + max(0, g_app->termMarginLeft) + max(0, g_app->termMarginRight);
    int logClientH = cell.cy * rows + max(0, g_app->termMarginTop) + max(0, g_app->termMarginBottom);
    int inputHeight = GetInputAreaHeight();
    int shortcutHeight = GetShortcutBarHeight();
    int statusHeight = GetStatusBarHeight();
    RECT rcClient = { 0, 0, logClientW, logClientH + shortcutHeight + inputHeight + statusHeight };
    DWORD style = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
    DWORD exStyle = (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    HMENU hMenu = GetMenu(hwnd);
    AdjustWindowRectEx(&rcClient, style, hMenu ? TRUE : FALSE, exStyle);
    int reqW = rcClient.right - rcClient.left;
    int reqH = rcClient.bottom - rcClient.top;
    RECT rcWnd{}; GetWindowRect(hwnd, &rcWnd);
    int curW = rcWnd.right - rcWnd.left;
    int curH = rcWnd.bottom - rcWnd.top;
    int newW = curW, newH = curH;
    if (onlyIfTooSmall)
    {
        if (curW < reqW) newW = reqW;
        if (curH < reqH) newH = reqH;
    }
    else
    {
        newW = reqW;
        newH = reqH;
    }
    if (newW != curW || newH != curH)
    {
        SetWindowPos(hwnd, nullptr, rcWnd.left, rcWnd.top, newW, newH, SWP_NOZORDER | SWP_NOACTIVATE);
        return true;
    }
    return false;
}

void ScrollRichEditByLines(HWND hwndRich, int lineDelta)
{
    if (!hwndRich) return;
    SendMessageW(hwndRich, EM_LINESCROLL, 0, lineDelta);
}

void JumpRichEditToTop(HWND hwndRich)
{
    if (!hwndRich) return;
    SendMessageW(hwndRich, WM_VSCROLL, SB_TOP, 0);
}

void JumpRichEditToBottom(HWND hwndRich)
{
    if (!hwndRich) return;
    SendMessageW(hwndRich, WM_VSCROLL, SB_BOTTOM, 0);
    int len = GetWindowTextLengthW(hwndRich);
    SendMessageW(hwndRich, EM_SETSEL, len, len);
    SendMessageW(hwndRich, EM_SCROLLCARET, 0, 0);
}


static void ReturnTerminalViewToLive()
{
    if (!g_app || !g_app->termBuffer)
        return;

    g_app->termBuffer->ScrollToLive();

    if (g_app->hwndLog && IsWindow(g_app->hwndLog))
        InvalidateRect(g_app->hwndLog, nullptr, FALSE);
}

void SendTextToMud(const std::wstring& text)
{
    if (!g_app) return;

    // 사용자가 새 명령을 입력하면 지난 화면 보기 상태를 자동으로 해제한다.
    // 그래야 지도처럼 여러 줄 출력되는 명령의 마지막 프롬프트까지 자동으로 따라간다.
    ReturnTerminalViewToLive();

    // 빈 엔터도 MUD에 전달되어야 한다.
    if (text.empty()) {
        SendCommandToProcess(L"");
        return;
    }

    // 1. 줄임말 변환 
    std::wstring finalText;
    TryExpandAbbreviation(text, finalText);

    if (finalText.empty()) {
        SendCommandToProcess(L"");
        return;
    }

    // 2. 타이머 제어 명령 가로채기 (타이머도 서버 접속과 무관하게 언제든 제어되어야 하므로 위로 올림)
    if (InterceptTimerCommand(finalText)) return;

    // 안전판 수정: isConnected 값은 TinTin++ 출력/이벤트 파싱에 의존하므로
    // 실제로 연결되어 있어도 false로 남을 수 있습니다.
    // 그래서 입력 명령은 막지 않고 항상 TinTin++ 프로세스로 보냅니다.
    SendCommandToProcess(finalText);
}

void SendRawCommandToMud(const std::wstring& text)
{
    if (!g_app) return;

    // 주소록/빠른연결/스크립트/단축버튼 같은 내부 명령도 새 출력을 볼 때는 live 화면으로 복귀한다.
    ReturnTerminalViewToLive();

    if (text.empty()) {
        SendCommandToProcess(L"");
        return;
    }

    // (참고: 단축버튼/트리거 등 내부 시스템에서 쏘는 명령은 줄임말을 거치지 않는 것이 정석입니다.)

    // 타이머 제어 명령 가로채기
    if (InterceptTimerCommand(text)) return;

    SendCommandToProcess(text);
}

void MarkKnownTinTinSession(const std::wstring& sessionName)
{
    if (!g_app) return;

    std::wstring name = Trim(sessionName);
    if (name.empty()) return;

    g_app->activeTinTinSessionName = name;
    g_app->hasActiveTinTinSession = true;
}

void ResetKnownTinTinSession()
{
    if (!g_app) return;

    g_app->activeTinTinSessionName.clear();
    g_app->hasActiveTinTinSession = false;
    g_app->isConnected = false;
    SetSessionActiveState(g_app->hwndMain, false);
}

bool ZapKnownTinTinSession()
{
    if (!g_app) return false;

    std::wstring sessionName;

    if (g_app->hasActiveTinTinSession)
        sessionName = Trim(g_app->activeTinTinSessionName);

    if (sessionName.empty() && g_app->hasActiveSession)
        sessionName = Trim(g_app->activeSession.name);

    if (sessionName.empty())
        return false;

    SendRawCommandToMud(L"#zap {" + sessionName + L"}");

    g_app->activeTinTinSessionName.clear();
    g_app->hasActiveTinTinSession = false;
    g_app->hasActiveSession = false;
    g_app->isConnected = false;
    SetSessionActiveState(g_app->hwndMain, false);
    g_app->autoLoginWindowActive = false;

    return true;
}

void SendKeepAliveNow()
{
    if (!g_app || !g_app->keepAliveEnabled)
        return;

    // 자동 로그인/로그인 대기 시간에는 접속 유지 명령을 보내지 않습니다.
    if (IsAutoLoginKeepAliveBlocked())
        return;

    std::wstring cmd = Trim(g_app->keepAliveCommand);
    if (cmd.empty())
        return;

    // 접속 유지 명령은 실제 MUD 세션이 잡힌 뒤에만 보냅니다.
    // isConnected/isSessionActive가 간혹 흔들릴 수 있으므로, TinTin++ 접속 성공/끊김
    // 문구를 감지해 기록한 마지막 성공/끊김 tick도 함께 확인합니다.
    bool definitelyConnected = (g_app->isConnected || g_app->isSessionActive);
    if (g_app->lastConnectionSuccessTick != 0 &&
        (g_app->lastConnectionDownTick == 0 ||
         (LONG)(g_app->lastConnectionSuccessTick - g_app->lastConnectionDownTick) > 0))
    {
        definitelyConnected = true;
    }

    if (!definitelyConnected)
        return;

    SendCommandToProcess(cmd);
}

void SaveLastConnectCommand(const std::wstring& text)
{
    if (!g_app) return;
    std::wstring trimmed = Trim(text);
    if (trimmed.empty()) return;
    std::wstring lower = trimmed;
    std::transform(lower.begin(), lower.end(), lower.begin(), towlower);
    if (lower.rfind(L"#session", 0) == 0 || lower.rfind(L"#connect", 0) == 0)
    {
        g_app->lastConnectCommand = trimmed;
        WritePrivateProfileStringW(L"recent", L"last_connect", g_app->lastConnectCommand.c_str(), GetSettingsPath().c_str());
    }
}

void AddStyledText(HWND hRich, const wchar_t* text, int fontSize, bool bold, COLORREF color, int spaceBefore)
{
    (void)fontSize;

    // 원본 코드 그대로 유지
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_SIZE | CFM_BOLD | CFM_COLOR | CFM_FACE;
    ScopedWindowDC dc(nullptr);
    int dpiY = dc ? GetDeviceCaps(dc.Get(), LOGPIXELSY) : 96;
    cf.yHeight = MulDiv(abs(ncm.lfMenuFont.lfHeight), 72, dpiY) * 20;
    cf.dwEffects = bold ? CFE_BOLD : 0;
    cf.crTextColor = color;
    lstrcpynW(cf.szFaceName, ncm.lfMenuFont.lfFaceName, LF_FACESIZE);
    PARAFORMAT2 pf = {};
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_SPACEBEFORE | PFM_LINESPACING;
    pf.dySpaceBefore = spaceBefore;
    pf.bLineSpacingRule = 5;
    pf.dyLineSpacing = 20;
    SendMessageW(hRich, EM_SETSEL, -1, -1);
    SendMessageW(hRich, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(hRich, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    SendMessageW(hRich, EM_REPLACESEL, FALSE, (LPARAM)text);
}

void AppendRunsToRichEdit(HWND hwndRich, const std::vector<StyledRun>& runs)
{
    if (!hwndRich || runs.empty()) return;
    SendMessageW(hwndRich, WM_SETREDRAW, FALSE, 0);
    for (auto run : runs)
    {
        if (run.text.empty()) continue;
        NormalizeRunTextForRichEdit(run.text);
        SendMessageW(hwndRich, EM_SETSEL, -1, -1);
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR | CFM_BACKCOLOR | CFM_BOLD;
        COLORREF textColor = run.style.fg;
        if (GetRValue(textColor) < 90 && GetGValue(textColor) < 90 && GetBValue(textColor) < 90)
            textColor = RGB(170, 170, 170);
        cf.crTextColor = textColor;
        cf.crBackColor = run.style.bg;
        cf.dwEffects = run.style.bold ? CFE_BOLD : 0;
        SendMessageW(hwndRich, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        SendMessageW(hwndRich, EM_REPLACESEL, FALSE, (LPARAM)run.text.c_str());
    }
    SendMessageW(hwndRich, WM_SETREDRAW, TRUE, 0);
    SendMessageW(hwndRich, WM_VSCROLL, SB_BOTTOM, 0);
    int len = GetWindowTextLengthW(hwndRich);
    SendMessageW(hwndRich, EM_SETSEL, len, len);
    SendMessageW(hwndRich, EM_SCROLLCARET, 0, 0);
    RedrawWindow(hwndRich, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

void NormalizeRunTextForRichEdit(std::wstring& s)
{
    std::wstring out;
    out.reserve(s.size() + 16);
    for (size_t i = 0; i < s.size(); ++i)
    {
        wchar_t ch = s[i];
        if (ch == L'\r')
        {
            if (i + 1 < s.size() && s[i + 1] == L'\n')
            {
                out += L"\r\n";
                ++i;
            }
            else
            {
                out += L"\r\n";
            }
        }
        else if (ch == L'\n')
        {
            out += L"\r\n";
        }
        else
        {
            out += ch;
        }
    }
    s.swap(out);
}

std::wstring ColorToString(COLORREF c)
{
    wchar_t buf[32] = {};
    wsprintfW(buf, L"%u,%u,%u", (UINT)GetRValue(c), (UINT)GetGValue(c), (UINT)GetBValue(c));
    return buf;
}

COLORREF StringToColor(const wchar_t* s, COLORREF def)
{
    if (!s || !*s) return def;
    int r = -1, g = -1, b = -1;
    if (swscanf_s(s, L"%d,%d,%d", &r, &g, &b) == 3)
    {
        r = ClampByteRange(r, 0, 255);
        g = ClampByteRange(g, 0, 255);
        b = ClampByteRange(b, 0, 255);
        return RGB(r, g, b);
    }
    return def;
}

int GetSyntaxLanguageFromPath(const std::wstring& path)
{
    std::wstring lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    size_t dot = lower.find_last_of(L'.');
    if (dot == std::wstring::npos)
        return 0;

    std::wstring ext = lower.substr(dot);

    if (ext == L".tin")
        return 1; // TinTin

    if (ext == L".c" || ext == L".cpp" || ext == L".cc" || ext == L".cxx" ||
        ext == L".h" || ext == L".hpp" || ext == L".hh" || ext == L".hxx")
        return 2; // C / C++

    if (ext == L".cs")
        return 3; // C#

    return 0;
}

void SetupRichEditDefaults(HWND hwndRich)
{
    if (!g_app) return;
    SendMessageW(hwndRich, EM_SETBKGNDCOLOR, 0, g_app->logStyle.backColor);
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BACKCOLOR | CFM_BOLD;
    cf.yHeight = GetFontPointSizeFromLogFont(g_app->logStyle.font) * 20;
    cf.crTextColor = g_app->logStyle.textColor;
    cf.crBackColor = g_app->logStyle.backColor;
    lstrcpynW(cf.szFaceName, g_app->logStyle.font.lfFaceName, LF_FACESIZE);
    cf.dwEffects = (g_app->logStyle.font.lfWeight >= FW_BOLD) ? CFE_BOLD : 0;
    SendMessageW(hwndRich, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    PARAFORMAT2 pf = {};
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_ALIGNMENT;
    pf.wAlignment = PFA_LEFT;
    SendMessageW(hwndRich, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    SendMessageW(hwndRich, EM_SETUNDOLIMIT, 0, 0);
}

void SetupChatRichEditDefaults(HWND hwndRich)
{
    if (!g_app || !hwndRich) return;
    SendMessageW(hwndRich, EM_SETBKGNDCOLOR, 0, g_app->chatStyle.backColor);
    ShowScrollBar(hwndRich, SB_VERT, FALSE);
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BACKCOLOR | CFM_BOLD;
    cf.yHeight = GetFontPointSizeFromLogFont(g_app->chatStyle.font) * 20;
    cf.crTextColor = g_app->chatStyle.textColor;
    cf.crBackColor = g_app->chatStyle.backColor;
    lstrcpynW(cf.szFaceName, g_app->chatStyle.font.lfFaceName, LF_FACESIZE);
    cf.dwEffects = (g_app->chatStyle.font.lfWeight >= FW_BOLD) ? CFE_BOLD : 0;
    SendMessageW(hwndRich, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf); pf.dwMask = PFM_ALIGNMENT; pf.wAlignment = PFA_LEFT;
    SendMessageW(hwndRich, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

int GetCustomMenuItemWidth(int index)
{
    static const int widths[] = { 64, 64, 64, 64, 78 };
    if (index < 0 || index >= 5) return 64;
    return widths[index];
}

int HitTestCustomMenuBar(int x, int y)
{
    if (!g_app || g_app->menuHidden) return -1;
    if (y < 0 || y >= g_app->customMenuHeight) return -1;
    int curX = 6;
    for (int i = 0; i < 5; ++i)
    {
        int w = GetCustomMenuItemWidth(i);
        RECT rc = { curX, 0, curX + w, g_app->customMenuHeight };
        POINT pt = { x, y };
        if (PtInRect(&rc, pt)) return i;
        curX += w + 4;
    }
    return -1;
}

void DrawCustomMenuBar(HDC hdc, HWND hwnd)
{
    if (!g_app || g_app->menuHidden) return;
    RECT rcClient; GetClientRect(hwnd, &rcClient);
    RECT rcMenu = { 0, 0, rcClient.right, g_app->customMenuHeight };
    FillRect(hdc, &rcMenu, GetSysColorBrush(COLOR_BTNFACE));
    SetBkMode(hdc, TRANSPARENT);
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT hOld = (HFONT)SelectObject(hdc, hFont);
    const wchar_t* items[5] = { L"파일(F)", L"편집(E)", L"보기(V)", L"옵션(O)", L"도움말(H)" };
    int curX = 6;
    for (int i = 0; i < 5; ++i)
    {
        int w = GetCustomMenuItemWidth(i);
        RECT rc = { curX, 2, curX + w, g_app->customMenuHeight - 2 };
        if (g_app->hotMenuIndex == i)
        {
            FillRect(hdc, &rc, GetSysColorBrush(COLOR_HIGHLIGHT));
            SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
        }
        else
        {
            SetTextColor(hdc, GetSysColor(COLOR_MENUTEXT));
        }
        DrawTextW(hdc, items[i], -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        curX += w + 4;
    }
    SelectObject(hdc, hOld);
    UniqueGdiObject hPen(CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW)));
    ScopedSelectObject penSel(hdc, hPen.Get());
    MoveToEx(hdc, 0, g_app->customMenuHeight - 1, nullptr);
    LineTo(hdc, rcClient.right, g_app->customMenuHeight - 1);
}

bool IsRichEditNearBottom(HWND hwndRich)
{
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_TRACKPOS;
    if (!GetScrollInfo(hwndRich, SB_VERT, &si)) return true;
    int maxPos = si.nMax - (int)si.nPage + 1;
    if (maxPos < 0) maxPos = 0;
    return si.nPos >= maxPos - 1;
}


void ShowTrayIcon(HWND hwnd) {
    if (!g_app || g_app->trayIconVisible) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid); nid.hWnd = hwnd; nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAYICON;
    nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_ICON1));
    std::wstring tip = L"TinTin++ GUI v" + GetAppVersionString() + L" (백그라운드 실행 중)";
    lstrcpynW(nid.szTip, tip.c_str(), ARRAYSIZE(nid.szTip));
    Shell_NotifyIconW(NIM_ADD, &nid);
    g_app->trayIconVisible = true;
}

void HideTrayIcon(HWND hwnd) {
    if (!g_app || !g_app->trayIconVisible) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid); nid.hWnd = hwnd; nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_app->trayIconVisible = false;
}

void EnsureVisibleEditCaret(HWND hwndEdit) {
    if (!g_app || !hwndEdit) return;
    SendMessageW(hwndEdit, EM_SCROLLCARET, 0, 0);
    if (GetFocus() == hwndEdit) ShowCaret(hwndEdit);
}

void SendCommandToProcess(const std::wstring& line)
{
    if (!g_app || !g_app->proc.stdinWrite)
    {
        OutputDebugStringW(L"[SEND] stdinWrite invalid\r\n");
        return;
    }

    // ★ 추가: 접속 안 된 상태에서 전송 시 경고음
    if (!g_app->isConnected && g_app->soundEnabled)
    {
        MessageBeep(MB_ICONWARNING);
    }

    std::wstring sendText = line;

    if (!line.empty())
        NotifyPossibleConnectionCommand(line);

    if (sendText.empty())
        sendText = L"\r";
    else
        sendText += L"\r";

    std::string utf8 = WideToUtf8(sendText);

    bool ok = WriteAllToWinFile(g_app->proc.stdinWrite, utf8.data(), utf8.size());

    wchar_t dbg[256];
    wsprintfW(dbg, L"[SEND] ok=%d size=%lu textlen=%zu\r\n",
        ok ? 1 : 0,
        (unsigned long)utf8.size(),
        sendText.size());
    OutputDebugStringW(dbg);

    if (!ok)
    {
        DWORD err = GetLastError();
        wsprintfW(dbg, L"[SEND] WriteFile error=%lu\r\n", (unsigned long)err);
        OutputDebugStringW(dbg);
    }
}

std::wstring ApplyAbbreviationToText(const std::wstring& input)
{
    std::wstring out;
    if (TryExpandAbbreviation(input, out))
        return out;
    return input;
}

HFONT GetCurrentAppFont(int size, int weight)
{
    std::wstring fontFace = L"Mud둥근모";   // 입력창과 동일한 폰트 이름

    return CreateFontW(
        -size,                    // 크기 (입력창과 맞추기 위해 16~18 추천)
        0, 0, 0,
        weight,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY,          // ← 입력창과 동일한 선명도 (CLEARTYPE에서 바꿈)
        FIXED_PITCH | FF_MODERN,
        fontFace.c_str()
    );
}

void InitStyleFont(LOGFONTW& lf, HWND hwnd, int pointSize)
{
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = MakeLfHeightFromPointSize(hwnd, pointSize);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = HANGEUL_CHARSET;
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = DEFAULT_QUALITY;        // ← 입력창과 동일
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    lstrcpyW(lf.lfFaceName, L"Mud둥근모");
}

void RegisterEmbeddedFont()
{
    if (!g_app)
        return;

    // WM_CREATE와 WinMain 쪽에서 중복 호출될 수 있으므로 한 번만 등록합니다.
    if (g_app->hFontRes || g_app->privateFontFileRegistered)
        return;

    HMODULE hMod = GetModuleHandleW(nullptr);

    // 1순위: exe 안에 포함된 MudDunggeunmo-Regular.ttf 리소스 사용
    HRSRC hRes = FindResourceW(hMod, MAKEINTRESOURCEW(IDR_MY_CUSTOM_FONT), RT_RCDATA);
    if (hRes) {
        HGLOBAL hData = LoadResource(hMod, hRes);
        void* pFontData = hData ? LockResource(hData) : nullptr;
        DWORD fontSize = SizeofResource(hMod, hRes);

        if (pFontData && fontSize > 0) {
            DWORD numFonts = 0;
            g_app->hFontRes = AddFontMemResourceEx(pFontData, fontSize, nullptr, &numFonts);
            if (g_app->hFontRes && numFonts > 0)
                return;
        }
    }

    // 2순위: exe와 같은 폴더의 MudDunggeunmo-Regular.ttf를 현재 프로세스 전용으로 등록
    // 설치 없이 사용할 수 있지만, 폰트 파일은 exe 옆에 따로 있어야 합니다.
    wchar_t exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH) || exePath[0] == L'\0')
        return;

    std::wstring fontPath = exePath;
    size_t slash = fontPath.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        fontPath.resize(slash + 1);
    else
        fontPath.clear();
    fontPath += L"MudDunggeunmo-Regular.ttf";

    DWORD attr = GetFileAttributesW(fontPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
        return;

    if (AddFontResourceExW(fontPath.c_str(), FR_PRIVATE, nullptr) > 0) {
        g_app->privateFontFileRegistered = true;
        g_app->privateFontFilePath = fontPath;
    }
}

void UnloadEmbeddedFont()
{
    if (!g_app)
        return;

    if (g_app->hFontRes) {
        RemoveFontMemResourceEx(g_app->hFontRes);
        g_app->hFontRes = nullptr;
    }

    if (g_app->privateFontFileRegistered && !g_app->privateFontFilePath.empty()) {
        RemoveFontResourceExW(g_app->privateFontFilePath.c_str(), FR_PRIVATE, nullptr);
        g_app->privateFontFileRegistered = false;
        g_app->privateFontFilePath.clear();
    }
}

#if KTIN_APP_VER_ALPHA_INDEX <= 0
static std::wstring FormatKtinVersion(int major, int minor, int build)
{
    wchar_t verStr[64] = { 0 };
    wsprintfW(verStr, L"%d.%d.%02d", major, minor, build);
    return std::wstring(verStr);
}
#endif

static std::wstring FormatTinTinVersion(int major, int minor, int patch, int build)
{
    wchar_t verStr[64] = { 0 };
    wsprintfW(verStr, L"%d.%d.%d.%02d", major, minor, patch, build);
    return std::wstring(verStr);
}

std::wstring GetAppVersionString()
{
#if KTIN_APP_VER_ALPHA_INDEX > 0
    wchar_t verStr[64] = { 0 };
    wsprintfW(verStr, L"%d.%d-a%02d",
        KTIN_APP_VER_MAJOR,
        KTIN_APP_VER_MINOR,
        KTIN_APP_VER_ALPHA_INDEX);
    return std::wstring(verStr);
#else
    return FormatKtinVersion(KTIN_APP_VER_MAJOR, KTIN_APP_VER_MINOR, KTIN_APP_VER_BUILD);
#endif
}

std::wstring GetTinTinVersionString()
{
    return FormatTinTinVersion(
        KTIN_TINTIN_VER_MAJOR,
        KTIN_TINTIN_VER_MINOR,
        KTIN_TINTIN_VER_PATCH,
        KTIN_TINTIN_VER_BUILD);
}

std::wstring SimpleEncrypt(const std::wstring& plain)
{
    if (plain.empty()) return L"";
    std::wstring result = plain;
    const wchar_t key = 0x5A;        // 임의의 키 (변경 가능)
    for (wchar_t& ch : result)
        ch ^= key;
    return result;
}

std::wstring SimpleDecrypt(const std::wstring& cipher)
{
    return SimpleEncrypt(cipher);    // XOR은 대칭 연산이므로 동일 함수 사용
}
