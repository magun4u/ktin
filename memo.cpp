#define KTIN_MEMO_LOCAL_IMPL 1

#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "memo.h"
#include "theme.h"
#include "resource.h"
#include "settings.h"
#include "dialogs.h"
#include "win_util.h"
#include <richedit.h>
#include <commctrl.h>
#include <fstream>
#include <algorithm>

// 전역 변수 정의
MemoState g_memo;
MemoFindState g_memoFind;
int g_currentLineSetIdx = 0;

static unsigned long long s_lastMemoHighlightSig = 0;
static bool s_memoUserKeywordsLoaded = false;
static FILETIME s_memoUserKeywordsFt = {};
static HWND s_hwndMemoBusyPopup = nullptr;
static LRESULT CALLBACK MemoEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
static bool MemoOpenFile(HWND hwnd, const std::wstring& path);
static bool MemoSaveFile(HWND hwnd, const std::wstring& path);
static void UpdateMemoTitle();
void UpdateMemoStatus();
static void MarkMemoDirty(bool dirty);
static void ApplyMemoFontAndFormat();
static void ApplyMemoSyntaxHighlight(HWND hwndEdit);
static void SetMemoThemeBaseColors(int themeIdx);
static LRESULT CALLBACK MemoWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct AutoSaveFile {
    std::wstring path;
    std::wstring display;
    FILETIME ft;
};

struct AutoSaveListState {
    std::wstring* outPath;
    bool accepted;
    std::vector<AutoSaveFile> files;
};

struct MemoColWidthState {
    int* cols;
    bool accepted;
};

static void MemoRebuildRecentMenu(HWND hwnd);
static bool HandleMemoShortcutKey(UINT msg, WPARAM wParam);

static LineSet g_lineSets[] = 
{
    { L'─', L'│', L'┌', L'┐', L'└', L'┘', L'├', L'┤', L'┬', L'┴', L'┼', L"┌─┬─┐ (얇은선)" },
    { L'━', L'┃', L'┏', L'┓', L'┗', L'┛', L'┣', L'┫', L'┳', L'┻', L'╋', L"┏━┳━┓ (두꺼운선)" },
    { L'─', L'┃', L'┎', L'┒', L'┖', L'┚', L'┠', L'┨', L'┰', L'┸', L'╂', L"┎─┰─┒ (가로얇은/세로두꺼운)" },
    { L'━', L'│', L'┍', L'┑', L'┕', L'┙', L'┝', L'┥', L'┯', L'┷', L'┿', L"┍━┯━┑ (가로두꺼운/세로얇은)" }
};

// ==============================================
// 내부 헬퍼 함수들 (static)
// ==============================================
// 메모장 좌표 저장 함수
static void SaveMemoWindowSettings() 
{
    if (!g_memo.hwnd) return;
    std::wstring path = GetSettingsPath();
    RECT rc;
    GetWindowRect(g_memo.hwnd, &rc); // 현재 창 위치와 크기 가져오기

    wchar_t buf[64];
    wsprintfW(buf, L"%ld", rc.left); WritePrivateProfileStringW(L"memo_window", L"x", buf, path.c_str());
    wsprintfW(buf, L"%ld", rc.top); WritePrivateProfileStringW(L"memo_window", L"y", buf, path.c_str());
    wsprintfW(buf, L"%d", RectWidth(rc)); WritePrivateProfileStringW(L"memo_window", L"w", buf, path.c_str());
    wsprintfW(buf, L"%d", RectHeight(rc)); WritePrivateProfileStringW(L"memo_window", L"h", buf, path.c_str());
}

// 메모장 좌표 불러오기 함수
static void LoadMemoWindowSettings() 
{
    std::wstring path = GetSettingsPath();
    g_memo.x = GetPrivateProfileIntW(L"memo_window", L"x", -1, path.c_str());
    g_memo.y = GetPrivateProfileIntW(L"memo_window", L"y", -1, path.c_str());
    g_memo.w = GetPrivateProfileIntW(L"memo_window", L"w", 900, path.c_str());
    g_memo.h = GetPrivateProfileIntW(L"memo_window", L"h", 700, path.c_str());
}


// ★ 메모장 최근 파일 목록 저장
static void SaveMemoRecentFiles() 
{
    std::wstring path = GetSettingsPath();
    int count = (int)g_memo.recentFiles.size();
    wchar_t buf[32];
    wsprintfW(buf, L"%d", count);
    WritePrivateProfileStringW(L"memo_recent", L"count", buf, path.c_str());

    for (int i = 0; i < count; ++i) {
        wchar_t key[32];
        wsprintfW(key, L"file_%d", i);
        WritePrivateProfileStringW(L"memo_recent", key, g_memo.recentFiles[i].c_str(), path.c_str());
    }
    // 예전 찌꺼기 지우기
    for (int i = count; i < 5; ++i) {
        wchar_t key[32];
        wsprintfW(key, L"file_%d", i);
        WritePrivateProfileStringW(L"memo_recent", key, nullptr, path.c_str());
    }
}

// ★ 메모장 최근 파일 목록 불러오기
static void LoadMemoRecentFiles() 
{
    g_memo.recentFiles.clear();
    std::wstring path = GetSettingsPath();
    int count = GetPrivateProfileIntW(L"memo_recent", L"count", 0, path.c_str());
    if (count > 5) count = 5;

    for (int i = 0; i < count; ++i) {
        wchar_t key[32];
        wchar_t buf[MAX_PATH] = { 0 };
        wsprintfW(key, L"file_%d", i);
        GetPrivateProfileStringW(L"memo_recent", key, L"", buf, MAX_PATH, path.c_str());
        if (buf[0] != L'\0') {
            g_memo.recentFiles.push_back(buf);
        }
    }
}

static void ShowMemoBusyPopup(HWND owner, const wchar_t* text)
{
    if (s_hwndMemoBusyPopup && IsWindow(s_hwndMemoBusyPopup))
        return;

    RECT rcOwner{};
    GetWindowRect(owner, &rcOwner);

    const int w = 220;
    const int h = 70;
    const int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - w) / 2;
    const int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - h) / 2;

    s_hwndMemoBusyPopup = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"STATIC",
        L"",
        WS_POPUP | WS_BORDER | WS_VISIBLE,
        x, y, w, h,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (!s_hwndMemoBusyPopup)
        return;

    SetWindowTextW(s_hwndMemoBusyPopup, L"");

    HWND hLabel = CreateWindowExW(
        0,
        L"STATIC",
        text,
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        10, 22, w - 20, 24,
        s_hwndMemoBusyPopup,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    HFONT hFont = GetPopupUIFont(owner);
    if (hFont && hLabel)
        SendMessageW(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    ShowWindow(s_hwndMemoBusyPopup, SW_SHOWNOACTIVATE);
    UpdateWindow(s_hwndMemoBusyPopup);
}

static void HideMemoBusyPopup()
{
    if (s_hwndMemoBusyPopup && IsWindow(s_hwndMemoBusyPopup))
    {
        DestroyWindow(s_hwndMemoBusyPopup);
        s_hwndMemoBusyPopup = nullptr;
    }
}

static void MemoPushRecentFile(const std::wstring& path)
{
    if (path.empty()) return;

    g_memo.recentFiles.erase(
        std::remove(g_memo.recentFiles.begin(), g_memo.recentFiles.end(), path),
        g_memo.recentFiles.end());

    g_memo.recentFiles.insert(g_memo.recentFiles.begin(), path);

    if (g_memo.recentFiles.size() > 5)
        g_memo.recentFiles.resize(5);

    if (g_memo.hwnd)
        MemoRebuildRecentMenu(g_memo.hwnd);

    SaveMemoRecentFiles();
}

static bool MemoDoOpenDialog(HWND hwnd)
{
    wchar_t fileName[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;

    // ★ 필터 변경: txt와 tin을 동시에 보여주는 '지원 파일' 항목을 첫 번째로 배치
    ofn.lpstrFilter =
        L"모든 지원 파일 (*.txt; *.tin; *.c; *.cpp; *.h; *.hpp; *.cs)\0*.txt;*.tin;*.c;*.cpp;*.h;*.hpp;*.cs\0"
        L"텍스트 파일 (*.txt)\0*.txt\0"
        L"TinTin 스크립트 (*.tin)\0*.tin\0"
        L"C / C++ 소스 (*.c; *.cpp; *.h; *.hpp)\0*.c;*.cpp;*.h;*.hpp\0"
        L"C# 소스 (*.cs)\0*.cs\0"
        L"모든 파일 (*.*)\0*.*\0";

    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;
    ofn.lpstrDefExt = L"txt"; // 기본 확장자

    if (!GetOpenFileNameW(&ofn))
        return false;

    return MemoOpenFile(hwnd, fileName);
}

static bool MemoDoSaveDialog(HWND hwnd, bool saveAs)
{
    if (!saveAs && !g_memo.currentPath.empty())
        return MemoSaveFile(hwnd, g_memo.currentPath);

    wchar_t fileName[MAX_PATH] = {};
    if (!g_memo.currentPath.empty())
        lstrcpynW(fileName, g_memo.currentPath.c_str(), MAX_PATH);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;

    // ★ 저장 시에도 사용자가 형식을 고를 수 있도록 필터 제공
    ofn.lpstrFilter =
        L"텍스트 파일 (*.txt)\0*.txt\0"
        L"TinTin 스크립트 (*.tin)\0*.tin\0"
        L"모든 파일 (*.*)\0*.*\0";

    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_EXPLORER;
    ofn.lpstrDefExt = L"txt";

    if (!GetSaveFileNameW(&ofn))
        return false;

    return MemoSaveFile(hwnd, fileName);
}

static void UpdateMemoMenuState(HWND hwnd) 
{
    HMENU hMenu = GetMenu(hwnd);
    if (!hMenu) return;

    // ★ static을 붙여서 메뉴가 그려질 때까지 문자열 주소가 유지되게 합니다.
    static wchar_t buf_draw[128];
    static wchar_t buf_auto[128];
    static wchar_t buf_syntax[128];
    static wchar_t buf_tintin[128];
    static wchar_t buf_cpp[128];
    static wchar_t buf_cs[128];
    static wchar_t buf_marks[128];

    lstrcpyW(buf_draw, g_memo.drawMode ? L"그리기 모드 꺼짐\tAlt+D" : L"그리기 모드 켜짐\tAlt+D");

    lstrcpyW(buf_syntax,
        g_memo.useSyntax ? L"TinTin++ 구문 강조 끄기" : L"TinTin++ 구문 강조 켜기");

    lstrcpyW(buf_tintin,
        (g_memo.syntaxLang == 1) ? L"✔ TinTin 구문강조" : L"　TinTin 구문강조");

    lstrcpyW(buf_cpp,
        (g_memo.syntaxLang == 2) ? L"✔ C, C++ 구문강조" : L"　C, C++ 구문강조");

    lstrcpyW(buf_cs,
        (g_memo.syntaxLang == 3) ? L"✔ C# 구문강조" : L"　C# 구문강조");

    lstrcpyW(buf_marks,
        g_memo.showFormatMarks ? L"✔조판 부호 보기" : L"　조판 부호 보기");

    ModifyMenuW(hMenu, ID_MEMO_FORMAT_SYNTAX_TINTIN, MF_BYCOMMAND | MF_OWNERDRAW,
        ID_MEMO_FORMAT_SYNTAX_TINTIN, (LPCWSTR)buf_syntax);

    if (g_memo.autoSave) {
        wsprintfW(buf_auto, L"자동저장 끄기 (현재 %d초)", g_memo.autoSaveIntervalSec);
    }
    else {
        lstrcpyW(buf_auto, L"자동저장 켜기 (간격 설정)");
    }

    ModifyMenuW(hMenu, ID_MEMO_DRAW_TOGGLE, MF_BYCOMMAND | MF_OWNERDRAW,
        ID_MEMO_DRAW_TOGGLE, (LPCWSTR)buf_draw);

    ModifyMenuW(hMenu, ID_MEMO_AUTOSAVE_TOGGLE, MF_BYCOMMAND | MF_OWNERDRAW,
        ID_MEMO_AUTOSAVE_TOGGLE, (LPCWSTR)buf_auto);


    ModifyMenuW(hMenu, ID_MEMO_FORMAT_SYNTAX_LANG_TINTIN,
        MF_BYCOMMAND | MF_OWNERDRAW,
        ID_MEMO_FORMAT_SYNTAX_LANG_TINTIN, buf_tintin);

    ModifyMenuW(hMenu, ID_MEMO_FORMAT_SYNTAX_LANG_CPP,
        MF_BYCOMMAND | MF_OWNERDRAW,
        ID_MEMO_FORMAT_SYNTAX_LANG_CPP, buf_cpp);

    ModifyMenuW(hMenu, ID_MEMO_FORMAT_SYNTAX_LANG_CSHARP,
        MF_BYCOMMAND | MF_OWNERDRAW,
        ID_MEMO_FORMAT_SYNTAX_LANG_CSHARP, buf_cs);

    ModifyMenuW(hMenu, ID_MEMO_VIEW_FORMATMARKS, MF_BYCOMMAND | MF_OWNERDRAW,
        ID_MEMO_VIEW_FORMATMARKS, (LPCWSTR)buf_marks);

    DrawMenuBar(hwnd);
}

static void CreateMemoMenus(HWND hwnd)
{
    UniqueMenu hMenuBar(CreateMenu());
    UniqueMenu hFile(CreatePopupMenu());
    UniqueMenu hEdit(CreatePopupMenu());
    UniqueMenu hSearch(CreatePopupMenu());
    UniqueMenu hFormat(CreatePopupMenu());
    UniqueMenu hMode(CreatePopupMenu());

    if (!hMenuBar.IsValid() || !hFile.IsValid() || !hEdit.IsValid() ||
        !hSearch.IsValid() || !hFormat.IsValid() || !hMode.IsValid())
        return;

    auto AddODItem = [](HMENU hMenu, UINT_PTR id, const wchar_t* text) {
        AppendMenuW(hMenu, MF_OWNERDRAW | MF_STRING, id, text);
        };
    auto AddODPopup = [](HMENU hMenu, UniqueMenu& hPopup, const wchar_t* text) {
        if (AppendMenuW(hMenu, MF_OWNERDRAW | MF_POPUP, (UINT_PTR)hPopup.Get(), text))
            hPopup.Release();
        };

    AddODItem(hFile.Get(), ID_MEMO_FILE_OPEN, L"열기...\tCtrl+O");
    AddODItem(hFile.Get(), ID_MEMO_FILE_SAVE, L"저장\tCtrl+S");
    AddODItem(hFile.Get(), ID_MEMO_FILE_SAVEAS, L"다른 이름으로 저장...");
    AppendMenuW(hFile.Get(), MF_SEPARATOR, 0, nullptr);
    AddODItem(hFile.Get(), ID_MEMO_EXIT_TO_TINTIN, L"TinTin으로 나가기(&G)\tAlt+G");
    AddODItem(hFile.Get(), ID_MEMO_FILE_LOAD_AUTOSAVE, L"자동 저장 복구(불러오기)...");
    AppendMenuW(hFile.Get(), MF_SEPARATOR, 0, nullptr);
    AddODItem(hFile.Get(), ID_MEMO_FILE_EXIT, L"닫기\tEsc");

    AddODItem(hEdit.Get(), ID_MEMO_EDIT_UNDO, L"실행취소\tCtrl+Z");
    AddODItem(hEdit.Get(), ID_MEMO_EDIT_REDO, L"다시실행\tCtrl+Y");
    AppendMenuW(hEdit.Get(), MF_SEPARATOR, 0, nullptr);
    AddODItem(hEdit.Get(), ID_MEMO_EDIT_CUT, L"잘라내기\tCtrl+X");
    AddODItem(hEdit.Get(), ID_MEMO_EDIT_COPY, L"복사\tCtrl+C");
    AddODItem(hEdit.Get(), ID_MEMO_EDIT_PASTE, L"붙여넣기\tCtrl+V");
    AddODItem(hEdit.Get(), ID_MEMO_EDIT_DELETE, L"삭제\tDel");

    AppendMenuW(hEdit.Get(), MF_SEPARATOR, 0, nullptr);
    AddODItem(hEdit.Get(), ID_MEMO_EDIT_DOC_START, L"글월 처음\tCtrl+Home");
    AddODItem(hEdit.Get(), ID_MEMO_EDIT_DOC_END, L"글월 마지막\tCtrl+End");
    AddODItem(hEdit.Get(), ID_MEMO_EDIT_SCR_START, L"화면 처음\tCtrl+PgUp");
    AddODItem(hEdit.Get(), ID_MEMO_EDIT_SCR_END, L"화면 마지막\tCtrl+PgDn");

    AppendMenuW(hEdit.Get(), MF_SEPARATOR, 0, nullptr);
    AddODItem(hEdit.Get(), ID_MEMO_EDIT_DEL_END, L"뒤쪽 지우기\tAlt+Y");
    AddODItem(hEdit.Get(), ID_MEMO_EDIT_DEL_WORD_LEFT, L"앞 단어 지우기\tCtrl+Bksp");
    AddODItem(hEdit.Get(), ID_MEMO_EDIT_DEL_WORD_RIGHT, L"뒷 단어 지우기\tCtrl+Del");
    AddODItem(hEdit.Get(), ID_MEMO_EDIT_DEL_LINE, L"한줄 지우기\tCtrl+L");

    AppendMenuW(hEdit.Get(), MF_SEPARATOR, 0, nullptr);
    AddODItem(hEdit.Get(), ID_MEMO_EDIT_SELECTALL, L"모두 선택\tCtrl+A");

    AddODItem(hSearch.Get(), ID_MEMO_EDIT_FIND, L"찾기\tCtrl+F");
    AddODItem(hSearch.Get(), ID_MEMO_EDIT_FIND_NEXT, L"다음 찾기\tF3");
    AddODItem(hSearch.Get(), ID_MEMO_EDIT_FIND_PREV, L"이전 찾기\tShift+F3");
    AddODItem(hSearch.Get(), ID_MEMO_EDIT_REPLACE, L"바꾸기\tCtrl+H");
    AppendMenuW(hSearch.Get(), MF_SEPARATOR, 0, nullptr);
    AddODItem(hSearch.Get(), ID_MEMO_EDIT_GOTO, L"행 찾아가기\tCtrl+G");

    AddODItem(hFormat.Get(), ID_MEMO_FORMAT_TEXT_COLOR, L"글자색/선색 변경...");
    AddODItem(hFormat.Get(), ID_MEMO_FORMAT_BACK_COLOR, L"배경색 변경...");
    AppendMenuW(hFormat.Get(), MF_SEPARATOR, 0, nullptr);
    AddODItem(hFormat.Get(), ID_MEMO_FORMAT_FONT, L"폰트 변경...");
    AppendMenuW(hFormat.Get(), MF_SEPARATOR, 0, nullptr);
    AddODItem(hFormat.Get(), ID_MEMO_FORMAT_ENC_UTF8, L"인코딩: UTF-8로 변경");
    AddODItem(hFormat.Get(), ID_MEMO_FORMAT_ENC_CP949, L"인코딩: CP949(한국어)로 변경");
    AppendMenuW(hFormat.Get(), MF_SEPARATOR, 0, nullptr);
    AddODItem(hFormat.Get(), ID_MEMO_FORMAT_WRAP_WIDTH, L"자동 줄바꿈 열 너비 설정...");
    AddODItem(hFormat.Get(), ID_MEMO_ALIGN_LEFT, L"선택영역 왼쪽 정렬");
    AddODItem(hFormat.Get(), ID_MEMO_ALIGN_CENTER, L"선택영역 가운데 정렬");
    AddODItem(hFormat.Get(), ID_MEMO_ALIGN_RIGHT, L"선택영역 오른쪽 정렬");
    AppendMenuW(hFormat.Get(), MF_SEPARATOR, 0, nullptr);

    UniqueMenu hTheme(CreatePopupMenu());
    if (!hTheme.IsValid())
        return;
    AddODItem(hTheme.Get(), ID_MEMO_SYNTAX_THEME_CLASSIC, L"기본 라이트 (Default Light)");
    AddODItem(hTheme.Get(), ID_MEMO_SYNTAX_THEME_DEFAULT_DARK, L"기본 다크 (Default Dark)");
    AppendMenuW(hTheme.Get(), MF_SEPARATOR, 0, nullptr);
    AddODItem(hTheme.Get(), ID_MEMO_SYNTAX_THEME_ULTRAEDIT, L"울트라에디터 (UltraEdit)");
    AddODItem(hTheme.Get(), ID_MEMO_SYNTAX_THEME_VSCODE, L"비주얼 스튜디오 (VS Code)");
    AddODItem(hTheme.Get(), ID_MEMO_SYNTAX_THEME_GITHUB_DARK, L"깃허브 다크 (GitHub Dark)");
    AppendMenuW(hTheme.Get(), MF_SEPARATOR, 0, nullptr);
    AddODItem(hTheme.Get(), ID_MEMO_SYNTAX_THEME_MONOKAI, L"모노카이 다크 (Monokai)");
    AddODItem(hTheme.Get(), ID_MEMO_SYNTAX_THEME_DRACULA, L"드라큘라 다크 (Dracula)");
    AppendMenuW(hTheme.Get(), MF_SEPARATOR, 0, nullptr);
    AddODItem(hTheme.Get(), ID_MEMO_SYNTAX_THEME_SOLAR_LIGHT, L"솔라라이즈드 라이트 (Solarized Light)");
    AddODItem(hTheme.Get(), ID_MEMO_SYNTAX_THEME_SOLAR_DARK, L"솔라라이즈드 다크 (Solarized Dark)");
    AddODPopup(hFormat.Get(), hTheme, L"구문 테마 선택...");

    AddODItem(hFormat.Get(), ID_MEMO_FORMAT_SYNTAX_TINTIN, L"TinTin++ 구문 강조 켜기/끄기");

    UniqueMenu hSyntaxLang(CreatePopupMenu());
    if (!hSyntaxLang.IsValid())
        return;
    AddODItem(hSyntaxLang.Get(), ID_MEMO_FORMAT_SYNTAX_LANG_TINTIN, L"TinTin 구문강조");
    AddODItem(hSyntaxLang.Get(), ID_MEMO_FORMAT_SYNTAX_LANG_CPP, L"C, C++ 구문강조");
    AddODItem(hSyntaxLang.Get(), ID_MEMO_FORMAT_SYNTAX_LANG_CSHARP, L"C# 구문강조");
    AddODPopup(hFormat.Get(), hSyntaxLang, L"구문 강조 선택...");
    AddODItem(hFormat.Get(), ID_MEMO_VIEW_FORMATMARKS, L"조판 부호 보기 켜기");

    AddODItem(hMode.Get(), ID_MEMO_DRAW_TOGGLE, L"그리기 모드 켜기\tAlt+D");
    AddODItem(hMode.Get(), ID_MEMO_REPEAT_SYMBOL, L"마지막 기호 반복\tCtrl+R");
    AddODItem(hMode.Get(), ID_MEMO_AUTOSAVE_TOGGLE, L"자동저장 켜기");
    AddODItem(hMode.Get(), ID_MEMO_VIEW_LINENUMBER, L"행 번호 보이기/숨기기");
    AddODItem(hMode.Get(), ID_MENU_VIEW_SYMBOLS, L"특수 기호(&S)\tF4");

    AddODPopup(hMenuBar.Get(), hFile, L"파일");
    AddODPopup(hMenuBar.Get(), hEdit, L"편집");
    AddODPopup(hMenuBar.Get(), hSearch, L"찾기");
    AddODPopup(hMenuBar.Get(), hFormat, L"서식");
    AddODPopup(hMenuBar.Get(), hMode, L"특수");

    if (!ReplaceWindowMenu(hwnd, hMenuBar.Release()))
        return;

    UpdateMemoMenuState(hwnd);
    MemoRebuildRecentMenu(hwnd);
}

static void MemoAutoSave()
{
    if (!g_memo.hwnd || !g_memo.hwndEdit || !g_memo.autoSave || !g_memo.dirty)
        return;

    // 1. 프로그램 옆에 autosave 폴더를 만듭니다.
    std::wstring saveDir = MakeAbsolutePath(GetModuleDirectory(), L"autosave");
    CreateDirectoryW(saveDir.c_str(), nullptr);

    // 2. 현재 파일 이름 알아내기 (새 문서면 Unsaved)
    std::wstring fileName = L"Unsaved";
    if (!g_memo.currentPath.empty()) {
        size_t slash = g_memo.currentPath.find_last_of(L"\\/");
        if (slash != std::wstring::npos) fileName = g_memo.currentPath.substr(slash + 1);
        else fileName = g_memo.currentPath;
    }

    // 3. 파일 이름 뒤에 _autosave.txt 를 붙여서 덮어쓰기 저장!
    std::wstring autoSavePath = saveDir + L"\\" + fileName + L"_autosave.txt";

    std::wstring text = GetWindowTextString(g_memo.hwndEdit);
    std::string utf8 = WideToUtf8(text);

    WriteUtf8BomTextFile(autoSavePath, utf8);
}

static LRESULT CALLBACK AutoSaveListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AutoSaveListState* state = (AutoSaveListState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE: SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lParam)->lpCreateParams); return DefWindowProcW(hwnd, msg, wParam, lParam);
    case WM_CREATE: {
        state = (AutoSaveListState*)((CREATESTRUCTW*)lParam)->lpCreateParams;
        ApplyPopupTitleBarTheme(hwnd);
        HFONT hF = GetPopupUIFont(hwnd);

        CreateWindowExW(0, L"STATIC", L"복구할 자동저장 파일을 선택하세요 (최신순):", WS_CHILD | WS_VISIBLE, 15, 15, 300, 20, hwnd, 0, 0, 0);
        HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | WS_VSCROLL, 15, 40, 380, 200, hwnd, (HMENU)101, 0, 0);
        CreateWindowExW(0, L"BUTTON", L"불러오기", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 235, 250, 80, 28, hwnd, (HMENU)IDOK, 0, 0);
        CreateWindowExW(0, L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 320, 250, 75, 28, hwnd, (HMENU)IDCANCEL, 0, 0);
        EnumChildWindows(hwnd, [](HWND c, LPARAM lp)->BOOL {SendMessageW(c, WM_SETFONT, lp, TRUE); return TRUE; }, (LPARAM)hF);

        for (size_t i = 0; i < state->files.size(); ++i) {
            int idx = (int)SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)state->files[i].display.c_str());
            SendMessageW(hList, LB_SETITEMDATA, idx, i);
        }
        if (!state->files.empty()) SendMessageW(hList, LB_SETCURSEL, 0, 0);
        SetFocus(hList);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || (LOWORD(wParam) == 101 && HIWORD(wParam) == LBN_DBLCLK)) {
            HWND hList = GetDlgItem(hwnd, 101);
            int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR && state) {
                int idx = (int)SendMessageW(hList, LB_GETITEMDATA, sel, 0);
                *state->outPath = state->files[idx].path;
                state->accepted = true;
                DestroyWindow(hwnd);
            }
            return 0;
        }
        else if (LOWORD(wParam) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
        break;
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)wParam, TRANSPARENT); return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool PromptMemoLoadAutoSave(HWND owner, std::wstring& outPath) 
{
    std::wstring saveDir = MakeAbsolutePath(GetModuleDirectory(), L"autosave");
    std::wstring searchPath = saveDir + L"\\*.txt";

    std::vector<AutoSaveFile> files;
    WIN32_FIND_DATAW fd = {};
    UniqueFindHandle hFind(FindFirstFileW(searchPath.c_str(), &fd));
    if (hFind.IsValid()) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                AutoSaveFile af;
                af.path = saveDir + L"\\" + fd.cFileName;
                af.ft = fd.ftLastWriteTime;

                SYSTEMTIME st = {};
                SYSTEMTIME lt = {};
                FileTimeToSystemTime(&fd.ftLastWriteTime, &st);
                SystemTimeToTzSpecificLocalTime(nullptr, &st, &lt);
                wchar_t buf[256];
                // 파일명과 저장된 시간을 예쁘게 표시
                wsprintfW(buf, L"[%04d-%02d-%02d %02d:%02d:%02d] %s", lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond, fd.cFileName);
                af.display = buf;
                files.push_back(af);
            }
        } while (FindNextFileW(hFind.Get(), &fd));
    }

    if (files.empty()) {
        ShowCenteredMessageBox(owner, L"자동 저장된 파일이 존재하지 않습니다.", L"알림", MB_OK | MB_ICONINFORMATION);
        return false;
    }

    // ★ 최신 파일이 위로 오게 정렬 (내림차순)
    std::sort(files.begin(), files.end(), [](const AutoSaveFile& a, const AutoSaveFile& b) {
        return CompareFileTime(&a.ft, &b.ft) > 0;
        });

    static bool reg = false;
    if (!reg) { WNDCLASSW wc = {}; wc.lpfnWndProc = AutoSaveListProc; wc.lpszClassName = L"TTAUtoSaveList"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassW(&wc); reg = true; }

    AutoSaveListState st = { &outPath, false, files };
    RECT rc; GetWindowRect(owner, &rc);
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"TTAUtoSaveList", L"자동 저장 불러오기", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, rc.left + 50, rc.top + 50, 430, 330, owner, 0, 0, &st);
    EnableWindow(owner, FALSE); MSG msg; while (IsWindow(hDlg) && GetMessageW(&msg, 0, 0, 0)) { if (!IsDialogMessageW(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    EnableWindow(owner, TRUE); SetActiveWindow(owner); return st.accepted;
}

static void MemoInsertText(HWND hEdit, const std::wstring& s)
{
    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)s.c_str());
}

static void MemoDeleteSelectionOrChar(HWND hEdit)
{
    CHARRANGE cr = {};
    SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
    if (cr.cpMin != cr.cpMax)
    {
        SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)L"");
        return;
    }
    SendMessageW(hEdit, EM_SETSEL, cr.cpMin, cr.cpMin + 1);
    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)L"");
}

static std::wstring MemoGetLineText(HWND hEdit, int line)
{
    LONG start = (LONG)SendMessageW(hEdit, EM_LINEINDEX, line, 0);
    if (start < 0)
        return L"";

    int len = (int)SendMessageW(hEdit, EM_LINELENGTH, start, 0);
    if (len <= 0)
        return L"";

    std::wstring out(len, L'\0');

    TEXTRANGEW tr = {};
    tr.chrg.cpMin = start;
    tr.chrg.cpMax = start + len;
    tr.lpstrText = &out[0];

    SendMessageW(hEdit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
    return out;
}

static int MemoCharCellWidth(wchar_t ch)
{
    if (ch == 0 || ch == L'\0')
        return 1;

    int w = KTinCharWidth(ch, true);

    if (w < 1) w = 1;
    if (w > 2) w = 2;
    return w;
}

static int MemoTextIndexToVisualCol(const std::wstring& text, int textIndex)
{
    if (textIndex <= 0)
        return 0;

    if (textIndex > (int)text.size())
        textIndex = (int)text.size();

    int vcol = 0;
    for (int i = 0; i < textIndex; ++i)
        vcol += MemoCharCellWidth(text[i]);

    return vcol;
}

static int MemoVisualColToTextIndex(const std::wstring& text, int visualCol, int* actualVisualCol)
{
    int vcol = 0;
    for (int i = 0; i < (int)text.size(); ++i) {
        if (vcol >= visualCol) {
            if (actualVisualCol) *actualVisualCol = vcol;
            return i;
        }
        int cw = MemoCharCellWidth(text[i]);
        // 만약 찾는 위치가 문자의 중간(2칸 문자의 2번째 칸)이라면 해당 문자의 시작 인덱스를 반환
        if (vcol + cw > visualCol) {
            if (actualVisualCol) *actualVisualCol = vcol;
            return i;
        }
        vcol += cw;
    }
    if (actualVisualCol) *actualVisualCol = vcol;
    return (int)text.size();
}

static bool MemoGetCaretGrid(HWND hEdit, int& line, int& col)
{
    DWORD sel = (DWORD)SendMessageW(hEdit, EM_GETSEL, 0, 0);
    int cp = LOWORD(sel);

    line = (int)SendMessageW(hEdit, EM_LINEFROMCHAR, cp, 0);
    LONG lineStart = (LONG)SendMessageW(hEdit, EM_LINEINDEX, line, 0);
    if (lineStart < 0)
    {
        line = 0;
        col = 0;
        return false;
    }

    int textIndex = cp - lineStart;
    std::wstring text = MemoGetLineText(hEdit, line);
    col = MemoTextIndexToVisualCol(text, textIndex);
    return true;
}

static void MemoEnsurePosition(HWND hEdit, int line, int col)
{
    while (true)
    {
        int lineCount = (int)SendMessageW(hEdit, EM_GETLINECOUNT, 0, 0);
        if (line < lineCount)
            break;

        SendMessageW(hEdit, EM_SETSEL, -1, -1);
        SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)L"\r\n");
    }

    std::wstring text = MemoGetLineText(hEdit, line);
    int visualLen = MemoTextIndexToVisualCol(text, (int)text.size());

    if (visualLen >= col)
        return;

    LONG start = (LONG)SendMessageW(hEdit, EM_LINEINDEX, line, 0);
    int textLen = (int)text.size();

    SendMessageW(hEdit, EM_SETSEL, start + textLen, start + textLen);

    std::wstring fill(col - visualLen, L' ');
    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)fill.c_str());
}

static void MemoSetCharAt(HWND hEdit, int line, int col, wchar_t ch)
{
    MemoEnsurePosition(hEdit, line, col);

    // ★ 그릴 때도 폰트의 크기와 이름을 동적으로 꽉 채움! (하드코딩 X)
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_FACE | CFM_CHARSET | CFM_SIZE | CFM_BOLD;
    cf.crTextColor = g_memo.textColor;
    cf.bCharSet = HANGEUL_CHARSET;

    int ptSize = GetFontPointSizeFromLogFont(g_memo.font);
    if (ptSize < 8) ptSize = 12;
    cf.yHeight = ptSize * 20; // 설정된 폰트 크기 완벽 복사

    lstrcpynW(cf.szFaceName, g_memo.font.lfFaceName, LF_FACESIZE);
    cf.dwEffects = (g_memo.font.lfWeight >= FW_BOLD) ? CFE_BOLD : 0;

    std::wstring text = MemoGetLineText(hEdit, line);
    LONG start = (LONG)SendMessageW(hEdit, EM_LINEINDEX, line, 0);
    if (start < 0) return;

    int chWidth = MemoCharCellWidth(ch);
    int actualCol = 0;
    int idx = MemoVisualColToTextIndex(text, col, &actualCol);

    // 부족한 공백 채우기
    if (actualCol < col) {
        SendMessageW(hEdit, EM_SETSEL, start + (int)text.size(), start + (int)text.size());
        std::wstring fill(col - actualCol, L' ');
        SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)fill.c_str());
        text = MemoGetLineText(hEdit, line);
        idx = (int)text.size();
    }

    int cpStart = idx;
    int nextActualCol = 0;
    int cpEnd = MemoVisualColToTextIndex(text, col + chWidth, &nextActualCol);

    if (nextActualCol < col + chWidth && cpEnd < (int)text.size()) {
        cpEnd++;
    }

    // 영역 선택 및 강제 서식 주입
    SendMessageW(hEdit, EM_SETSEL, start + cpStart, start + cpEnd);
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    // 선 그리기 적용
    wchar_t out[3] = { 0 };
    out[0] = ch;
    out[1] = L'\0';

    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)out);
}

static bool MemoIsLineBrush(wchar_t ch)
{
    return (ch >= 0x2500 && ch <= 0x257F);
}

static int MemoNormalizeLineCol(int col)
{
    if (col <= 0)
        return 0;

    return (col / 2) * 2;
}

static void MemoMoveCaret(HWND hEdit, int line, int col)
{
    if (MemoIsLineBrush(g_memo.selectedSymbol))
        col = MemoNormalizeLineCol(col);

    MemoEnsurePosition(hEdit, line, col);

    std::wstring text = MemoGetLineText(hEdit, line);
    LONG start = (LONG)SendMessageW(hEdit, EM_LINEINDEX, line, 0);
    if (start < 0)
        return;

    int actualCol = 0;
    int idx = MemoVisualColToTextIndex(text, col, &actualCol);

    SendMessageW(hEdit, EM_SETSEL, start + idx, start + idx);
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
    UpdateMemoStatus();
}

static int BoxMaskFromChar(wchar_t ch)
{
    for (int i = 0; i < 4; ++i)
    {
        const LineSet& s = g_lineSets[i];
        if (ch == s.v) return BOX_U | BOX_D;
        if (ch == s.h) return BOX_L | BOX_R;
        if (ch == s.tl) return BOX_R | BOX_D;
        if (ch == s.tr) return BOX_L | BOX_D;
        if (ch == s.bl) return BOX_U | BOX_R;
        if (ch == s.br) return BOX_U | BOX_L;
        if (ch == s.ml) return BOX_U | BOX_R | BOX_D;
        if (ch == s.mr) return BOX_U | BOX_L | BOX_D;
        if (ch == s.mt) return BOX_L | BOX_R | BOX_D;
        if (ch == s.mb) return BOX_U | BOX_L | BOX_R;
        if (ch == s.c) return BOX_U | BOX_R | BOX_D | BOX_L;
    }
    return 0;
}

static wchar_t BoxCharFromMask(int mask)
{
    const LineSet& s = g_lineSets[g_currentLineSetIdx];
    switch (mask)
    {
    case BOX_U | BOX_D: return s.v;
    case BOX_L | BOX_R: return s.h;
    case BOX_R | BOX_D: return s.tl;
    case BOX_L | BOX_D: return s.tr;
    case BOX_U | BOX_R: return s.bl;
    case BOX_U | BOX_L: return s.br;
    case BOX_U | BOX_R | BOX_D: return s.ml;
    case BOX_U | BOX_L | BOX_D: return s.mr;
    case BOX_L | BOX_R | BOX_D: return s.mt;
    case BOX_U | BOX_L | BOX_R: return s.mb;
    case BOX_U | BOX_R | BOX_D | BOX_L: return s.c;
    default: return 0;
    }
}

static wchar_t MemoGetCharAt(HWND hEdit, int line, int col)
{
    if (line < 0 || col < 0) return L' ';

    int lineCount = (int)SendMessageW(hEdit, EM_GETLINECOUNT, 0, 0);
    if (line >= lineCount) return L' ';

    std::wstring text = MemoGetLineText(hEdit, line);
    if (text.empty()) return L' ';

    int actualCol = 0;
    int idx = MemoVisualColToTextIndex(text, col, &actualCol);

    if (idx < 0 || idx >= (int)text.size()) return L' ';
    if (actualCol != col) return L' ';

    wchar_t ch = text[idx];
    if (ch == L' ') return L' ';

    return ch;
}


static int MemoConnectedMaskFromNeighbors(HWND hEdit, int line, int col, int hstep)
{
    if (line < 0 || col < 0)
        return 0;

    col = MemoNormalizeLineCol(col);

    int mask = 0;

    wchar_t leftCh = MemoGetCharAt(hEdit, line, col - hstep);
    wchar_t rightCh = MemoGetCharAt(hEdit, line, col + hstep);
    wchar_t upCh = MemoGetCharAt(hEdit, line - 1, col);
    wchar_t downCh = MemoGetCharAt(hEdit, line + 1, col);

    int leftMask = BoxMaskFromChar(leftCh);
    int rightMask = BoxMaskFromChar(rightCh);
    int upMask = BoxMaskFromChar(upCh);
    int downMask = BoxMaskFromChar(downCh);

    if (leftMask & BOX_R) mask |= BOX_L;
    if (rightMask & BOX_L) mask |= BOX_R;
    if (upMask & BOX_D) mask |= BOX_U;
    if (downMask & BOX_U) mask |= BOX_D;

    return mask;
}

static void MemoDrawStep(HWND hEdit, int dx, int dy)
{
    int line = 0, col = 0;
    MemoGetCaretGrid(hEdit, line, col);

    const int hstep = 2;
    col = MemoNormalizeLineCol(col);

    int nline = line + dy;
    int ncol = col + (dx * hstep);

    if (nline < 0) nline = 0;
    if (ncol < 0) ncol = 0;

    int curAdd = 0, nextAdd = 0;
    if (dx > 0) { curAdd |= BOX_R; nextAdd |= BOX_L; }
    if (dx < 0) { curAdd |= BOX_L; nextAdd |= BOX_R; }
    if (dy > 0) { curAdd |= BOX_D; nextAdd |= BOX_U; }
    if (dy < 0) { curAdd |= BOX_U; nextAdd |= BOX_D; }

    int curMask = MemoConnectedMaskFromNeighbors(hEdit, line, col, hstep);
    int nextMask = MemoConnectedMaskFromNeighbors(hEdit, nline, ncol, hstep);

    curMask |= curAdd;
    nextMask |= nextAdd;

    wchar_t outCur = BoxCharFromMask(curMask);
    wchar_t outNext = BoxCharFromMask(nextMask);

    // ★ 이동 방향에 맞게 정확한 기본선 부여 (상하 이동 시 세로선 적용)
    if (!outCur) outCur = (dx != 0) ? g_lineSets[g_currentLineSetIdx].h : g_lineSets[g_currentLineSetIdx].v;
    if (!outNext) outNext = (dx != 0) ? g_lineSets[g_currentLineSetIdx].h : g_lineSets[g_currentLineSetIdx].v;

    SendMessageW(hEdit, WM_SETREDRAW, FALSE, 0);

    MemoSetCharAt(hEdit, line, col, outCur);
    MemoSetCharAt(hEdit, nline, ncol, outNext);

    MemoMoveCaret(hEdit, nline, ncol);

    SendMessageW(hEdit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hEdit, nullptr, TRUE);

    MarkMemoDirty(true);
    UpdateMemoStatus();
}

static int MemoBrushStepWidth(wchar_t ch)
{
    return MemoCharCellWidth(ch);
}
static void MemoBrushStep(HWND hEdit, int dx, int dy)
{
    int line = 0, col = 0;
    MemoGetCaretGrid(hEdit, line, col);

    wchar_t brush = g_memo.selectedSymbol;
    if (brush == 0 || brush == L'\0')
        brush = L'■';

    int step = (dx != 0) ? MemoBrushStepWidth(brush) : 0;

    int nline = line + dy;
    int ncol = col + (dx * step);

    if (nline < 0) nline = 0;
    if (ncol < 0) ncol = 0;

    SendMessageW(hEdit, WM_SETREDRAW, FALSE, 0);

    MemoSetCharAt(hEdit, line, col, brush);

    if (step == 2 && dx != 0)
    {
        if (dx > 0)
            MemoSetCharAt(hEdit, line, col + 1, L' ');
        else if (col - 1 >= 0)
            MemoSetCharAt(hEdit, line, col - 1, L' ');
    }

    MemoEnsurePosition(hEdit, nline, ncol);
    MemoMoveCaret(hEdit, nline, ncol);

    SendMessageW(hEdit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hEdit, nullptr, TRUE);

    MarkMemoDirty(true);
    UpdateMemoStatus();
}

static void LayoutMemoChildren(HWND hwnd) 
{
    RECT rc; GetClientRect(hwnd, &rc);
    int statusH = 24; // 상태바 높이
    if (g_memo.hwndStatus) SendMessageW(g_memo.hwndStatus, WM_SIZE, 0, 0);

    // 행 번호 너비 설정 (켜져있으면 45px, 꺼져있으면 0px)
    int gutterW = g_memo.showLineNumbers ? 45 : 0;

    if (g_memo.hwndLineNum) {
        MoveWindow(g_memo.hwndLineNum, 0, 0, gutterW, rc.bottom - statusH, TRUE);
        ShowWindow(g_memo.hwndLineNum, g_memo.showLineNumbers ? SW_SHOW : SW_HIDE);
    }

    if (g_memo.hwndEdit) {
        MoveWindow(g_memo.hwndEdit, gutterW, 0, rc.right - gutterW, rc.bottom - statusH, TRUE);
    }
}

static void ApplyMemoLineSpacing()
{
    if (!g_memo.hwndEdit || !g_memo.hFont) return;

    ScopedWindowDC dc(g_memo.hwndEdit);
    if (!dc) return;

    HDC hdc = dc.Get();
    ScopedSelectObject fontSel(hdc, g_memo.hFont);

    TEXTMETRICW tm = {};
    GetTextMetricsW(hdc, &tm);
    int logPixelsY = GetDeviceCaps(hdc, LOGPIXELSY);

    // 폰트의 순수 높이(픽셀)를 RichEdit가 사용하는 Twips(1/1440인치) 단위로 정확히 변환
    int exactTwips = MulDiv(tm.tmHeight, 1440, logPixelsY);

    PARAFORMAT2 pf = {};
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_LINESPACING;
    pf.bLineSpacingRule = 4; // 4 = 정확한 줄 간격 (Exact)
    pf.dyLineSpacing = exactTwips;

    // 현재 사용자의 커서(선택 영역) 위치 백업
    CHARRANGE cr;
    SendMessageW(g_memo.hwndEdit, EM_EXGETSEL, 0, (LPARAM)&cr);

    // 문서 전체를 선택하고 여백 없는 행간 적용
    SendMessageW(g_memo.hwndEdit, EM_SETSEL, 0, -1);
    SendMessageW(g_memo.hwndEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);

    // 커서 위치 원래대로 복구
    SendMessageW(g_memo.hwndEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
}

static void ApplyMemoWrapWidth() 
{
    if (!g_memo.hwndEdit) return;
    if (g_memo.wrapCols <= 0) {
        // 0이면 기본 상태(창 크기에 맞춰 자동 줄바꿈)
        SendMessageW(g_memo.hwndEdit, EM_SETTARGETDEVICE, 0, 0);
    }
    else {
        // 지정된 글자 수(칸)만큼의 픽셀을 계산하여 해당 너비에 도달하면 무조건 줄바꿈
        ScopedWindowDC dc(g_memo.hwndEdit);
        if (!dc) return;

        HDC hdc = dc.Get();
        ScopedSelectObject fontSel(hdc, g_memo.hFont);
        SIZE sz{};
        GetTextExtentPoint32W(hdc, L"W", 1, &sz);

        int px = (g_memo.wrapCols * sz.cx) + 8; // 여백 8px 추가
        int twips = MulDiv(px, 1440, GetDeviceCaps(hdc, LOGPIXELSX));
        SendMessageW(g_memo.hwndEdit, EM_SETTARGETDEVICE, (WPARAM)hdc, twips);
    }
}

static bool MemoPerformFind(bool down, bool silent = false) 
{
    if (g_memoFind.query.empty() || !g_memo.hwndEdit) return false;

    // 1. 현재 커서(블럭) 위치 가져오기
    CHARRANGE cr;
    SendMessageW(g_memo.hwndEdit, EM_EXGETSEL, 0, (LPARAM)&cr);

    // 2. 윈도우 찾기 버그를 피하기 위해, 전체 텍스트를 메모리로 싹 다 가져옵니다.
    GETTEXTLENGTHEX gtl = { GTL_DEFAULT, 1200 };
    int totalLen = (int)SendMessageW(g_memo.hwndEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    if (totalLen <= 0) return false;

    std::wstring text(totalLen + 1, L'\0');
    TEXTRANGEW tr;
    tr.chrg.cpMin = 0;
    tr.chrg.cpMax = totalLen;
    tr.lpstrText = &text[0];
    int copied = (int)SendMessageW(g_memo.hwndEdit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
    text.resize(copied); // 실제 가져온 텍스트 길이만큼 깎아냄

    // 3. 대/소문자 구분이 꺼져 있으면 모두 소문자로 변환해서 비교 준비
    std::wstring query = g_memoFind.query;
    if (!g_memoFind.matchCase) {
        std::transform(text.begin(), text.end(), text.begin(), ::towlower);
        std::transform(query.begin(), query.end(), query.begin(), ::towlower);
    }

    size_t pos = std::wstring::npos;

    // 4. 완벽한 앞/뒤 찾기 로직
    if (down) {
        // [다음 찾기 (F3)] : 현재 선택된 단어의 끝(cpMax)부터 앞으로 쭉 찾음
        size_t start = (cr.cpMax >= 0 && cr.cpMax < (LONG)text.length()) ? cr.cpMax : text.length();
        pos = text.find(query, start);
    }
    else {
        // [이전 찾기 (Shift+F3)] : 현재 선택된 단어의 시작점(cpMin) 바로 앞(-1)부터 거꾸로 찾음
        if (cr.cpMin > 0) {
            size_t start = cr.cpMin - 1;
            if (start >= text.length()) start = text.length() - 1;
            pos = text.rfind(query, start); // rfind: 뒤로 훑어가는 C++ 최강의 함수!
        }
    }

    // 5. 찾았다면 화면 스크롤 & 블럭 씌우기
    if (pos != std::wstring::npos) {
        SendMessageW(g_memo.hwndEdit, EM_SETSEL, (LONG)pos, (LONG)(pos + query.length()));
        SendMessageW(g_memo.hwndEdit, EM_SCROLLCARET, 0, 0);
        return true;
    }
    else if (!silent) {
        MessageBoxW(g_memoFind.hwndDialog ? g_memoFind.hwndDialog : g_memo.hwnd,
            L"더 이상 찾을 내용이 없습니다.", L"메모장 찾기", MB_OK | MB_ICONINFORMATION);
    }
    return false;
}

static void MemoPerformReplace() 
{
    if (g_memoFind.query.empty() || !g_memo.hwndEdit) return;

    CHARRANGE cr;
    SendMessageW(g_memo.hwndEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
    if (cr.cpMin != cr.cpMax) {
        std::wstring selText(cr.cpMax - cr.cpMin, L'\0');
        TEXTRANGEW tr = { cr, &selText[0] };
        SendMessageW(g_memo.hwndEdit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);

        bool match = g_memoFind.matchCase ? (selText == g_memoFind.query)
            : (_wcsicmp(selText.c_str(), g_memoFind.query.c_str()) == 0);

        if (match) {
            SendMessageW(g_memo.hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)g_memoFind.replaceText.c_str());
        }
    }
    MemoPerformFind(true); // 바꾸고 나서 자동으로 다음 찾기
}

static void MemoPerformReplaceAll() 
{
    if (g_memoFind.query.empty() || !g_memo.hwndEdit) return;
    SendMessageW(g_memo.hwndEdit, WM_SETREDRAW, FALSE, 0);

    int count = 0;
    SendMessageW(g_memo.hwndEdit, EM_SETSEL, 0, 0); // 처음부터
    while (MemoPerformFind(true, true)) {
        SendMessageW(g_memo.hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)g_memoFind.replaceText.c_str());
        count++;
    }

    SendMessageW(g_memo.hwndEdit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_memo.hwndEdit, nullptr, TRUE);

    wchar_t buf[128];
    wsprintfW(buf, L"총 %d개 항목을 바꾸었습니다.", count);
    MessageBoxW(g_memoFind.hwndDialog ? g_memoFind.hwndDialog : g_memo.hwnd,
        buf, L"모두 바꾸기", MB_OK | MB_ICONINFORMATION);
}

static LRESULT CALLBACK MemoFindReplaceProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) 
{
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDCANCEL) {
            DestroyWindow(hwnd);
            // ★ 취소 버튼 누를 때 메모장으로 커서 복구
            if (g_memo.hwndEdit) SetFocus(g_memo.hwndEdit);
            return 0;
        }

        // 입력된 텍스트 갱신
        wchar_t buf[1024];
        GetWindowTextW(GetDlgItem(hwnd, IDC_MEMOFIND_QUERY), buf, 1024); g_memoFind.query = buf;
        GetWindowTextW(GetDlgItem(hwnd, IDC_MEMOFIND_REPLACE), buf, 1024); g_memoFind.replaceText = buf;
        g_memoFind.matchCase = (SendMessageW(GetDlgItem(hwnd, IDC_MEMOFIND_CASE), BM_GETCHECK, 0, 0) == BST_CHECKED);

        if (id == IDC_MEMOFIND_NEXT || id == IDOK) MemoPerformFind(true);
        else if (id == IDC_MEMOFIND_PREV) MemoPerformFind(false);
        else if (id == IDC_MEMOREPLACE_DO) MemoPerformReplace();
        else if (id == IDC_MEMOREPLACE_ALL) MemoPerformReplaceAll();
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);

    case WM_CLOSE:
        DestroyWindow(hwnd);
        // ★ 우측 상단 X 버튼 눌러서 닫을 때도 메모장으로 커서 복구
        if (g_memo.hwndEdit) SetFocus(g_memo.hwndEdit);
        return 0;

    case WM_DESTROY:
        g_memoFind.hwndDialog = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ShowMemoFindReplaceDialog(HWND owner, bool isReplace) 
{
    if (g_memoFind.hwndDialog && IsWindow(g_memoFind.hwndDialog)) {
        g_memoFind.isReplaceMode = isReplace;
        SetWindowTextW(g_memoFind.hwndDialog, isReplace ? L"바꾸기" : L"찾기");
        ShowWindow(GetDlgItem(g_memoFind.hwndDialog, IDC_MEMOFIND_REPLACE), isReplace ? SW_SHOW : SW_HIDE);
        ShowWindow(GetDlgItem(g_memoFind.hwndDialog, IDC_MEMOREPLACE_DO), isReplace ? SW_SHOW : SW_HIDE);
        ShowWindow(GetDlgItem(g_memoFind.hwndDialog, IDC_MEMOREPLACE_ALL), isReplace ? SW_SHOW : SW_HIDE);
        SetWindowPos(g_memoFind.hwndDialog, HWND_TOP, 0, 0, 420, isReplace ? 200 : 160, SWP_NOMOVE | SWP_SHOWWINDOW);
        SetFocus(GetDlgItem(g_memoFind.hwndDialog, IDC_MEMOFIND_QUERY));
        return;
    }

    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = {}; wc.lpfnWndProc = MemoFindReplaceProc; wc.hInstance = GetModuleHandle(0);
        wc.lpszClassName = L"TTMemoFindRepClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc); reg = true;
    }

    RECT rc; GetWindowRect(owner, &rc);
    int h = isReplace ? 200 : 160;
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"TTMemoFindRepClass", isReplace ? L"바꾸기" : L"찾기",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, rc.left + 50, rc.top + 50, 420, h, owner, nullptr, GetModuleHandle(0), nullptr);

    ApplyPopupTitleBarTheme(hDlg);
    HFONT hFont = GetPopupUIFont(hDlg);

    CreateWindowExW(0, L"STATIC", L"찾을 내용:", WS_CHILD | WS_VISIBLE, 15, 20, 70, 20, hDlg, nullptr, nullptr, nullptr);
    HWND hQ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_memoFind.query.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 90, 16, 200, 24, hDlg, (HMENU)IDC_MEMOFIND_QUERY, nullptr, nullptr);

    CreateWindowExW(0, L"STATIC", L"바꿀 내용:", WS_CHILD | (isReplace ? WS_VISIBLE : 0), 15, 55, 70, 20, hDlg, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_memoFind.replaceText.c_str(), WS_CHILD | (isReplace ? WS_VISIBLE : 0) | WS_TABSTOP | ES_AUTOHSCROLL, 90, 51, 200, 24, hDlg, (HMENU)IDC_MEMOFIND_REPLACE, nullptr, nullptr);

    HWND hChk = CreateWindowExW(0, L"BUTTON", L"대/소문자 구분", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 15, isReplace ? 90 : 55, 120, 20, hDlg, (HMENU)IDC_MEMOFIND_CASE, nullptr, nullptr);
    SendMessageW(hChk, BM_SETCHECK, g_memoFind.matchCase ? BST_CHECKED : BST_UNCHECKED, 0);

    CreateWindowExW(0, L"BUTTON", L"다음 찾기", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 305, 15, 85, 26, hDlg, (HMENU)IDC_MEMOFIND_NEXT, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"이전 찾기", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 305, 45, 85, 26, hDlg, (HMENU)IDC_MEMOFIND_PREV, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"바꾸기", WS_CHILD | (isReplace ? WS_VISIBLE : 0) | WS_TABSTOP, 305, 75, 85, 26, hDlg, (HMENU)IDC_MEMOREPLACE_DO, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"모두 바꾸기", WS_CHILD | (isReplace ? WS_VISIBLE : 0) | WS_TABSTOP, 305, 105, 85, 26, hDlg, (HMENU)IDC_MEMOREPLACE_ALL, nullptr, nullptr);

    EnumChildWindows(hDlg, [](HWND c, LPARAM lp) -> BOOL { SendMessageW(c, WM_SETFONT, lp, TRUE); return TRUE; }, (LPARAM)hFont);
    g_memoFind.hwndDialog = hDlg;
    g_memoFind.isReplaceMode = isReplace;
    SetFocus(hQ); SendMessageW(hQ, EM_SETSEL, 0, -1);
}

static LRESULT CALLBACK MemoGoToProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) 
{
    if (msg == WM_COMMAND) {
        if (LOWORD(wParam) == IDOK) {
            wchar_t buf[32]; GetWindowTextW(GetDlgItem(hwnd, 101), buf, 32);
            int targetLine = _wtoi(buf);
            if (targetLine < 1) targetLine = 1;

            int maxLines = (int)SendMessageW(g_memo.hwndEdit, EM_GETLINECOUNT, 0, 0);
            if (targetLine > maxLines) targetLine = maxLines;

            LONG pos = (LONG)SendMessageW(g_memo.hwndEdit, EM_LINEINDEX, targetLine - 1, 0);
            if (pos != -1) {
                SendMessageW(g_memo.hwndEdit, EM_SETSEL, pos, pos);
                SendMessageW(g_memo.hwndEdit, EM_SCROLLCARET, 0, 0);
            }
            DestroyWindow(hwnd); return 0;
        }
        else if (LOWORD(wParam) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
    }
    else if (msg == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    else if (msg == WM_CTLCOLORSTATIC) { SetBkMode((HDC)wParam, TRANSPARENT); return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE); }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}


static void ShowMemoGoToDialog(HWND owner) 
{
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = {}; wc.lpfnWndProc = MemoGoToProc; wc.hInstance = GetModuleHandle(0);
        wc.lpszClassName = L"TTMemoGoToClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc); reg = true;
    }
    RECT rc; GetWindowRect(owner, &rc);
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"TTMemoGoToClass", L"행 찾아가기",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, rc.left + 80, rc.top + 80, 260, 130, owner, nullptr, GetModuleHandle(0), nullptr);

    ApplyPopupTitleBarTheme(hDlg);
    HFONT hFont = GetPopupUIFont(hDlg);

    CreateWindowExW(0, L"STATIC", L"이동할 줄 번호:", WS_CHILD | WS_VISIBLE, 15, 20, 100, 20, hDlg, nullptr, nullptr, nullptr);
    HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 120, 16, 100, 24, hDlg, (HMENU)101, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"이동", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 60, 50, 70, 26, hDlg, (HMENU)IDOK, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 140, 50, 70, 26, hDlg, (HMENU)IDCANCEL, nullptr, nullptr);

    EnumChildWindows(hDlg, [](HWND c, LPARAM lp) -> BOOL { SendMessageW(c, WM_SETFONT, lp, TRUE); return TRUE; }, (LPARAM)hFont);

    // ★ 여기서부터가 새로 추가된 "대기(루프)" 및 "포커스 복구" 기능입니다!
    EnableWindow(owner, FALSE);
    SetFocus(hEdit);

    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN) {
            // 엔터키와 ESC키 인식
            if (msg.wParam == VK_RETURN) { SendMessageW(hDlg, WM_COMMAND, IDOK, 0); continue; }
            if (msg.wParam == VK_ESCAPE) { SendMessageW(hDlg, WM_COMMAND, IDCANCEL, 0); continue; }
        }
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    if (g_memo.hwndEdit) SetFocus(g_memo.hwndEdit); // ★ 창 닫히면 메모장으로 포커스 완벽 복구!
}

static LRESULT CALLBACK MemoColWidthProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) 
{
    MemoColWidthState* state = (MemoColWidthState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lParam)->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    case WM_CREATE: {
        state = (MemoColWidthState*)((CREATESTRUCTW*)lParam)->lpCreateParams;
        ApplyPopupTitleBarTheme(hwnd);
        HFONT hFont = GetPopupUIFont(hwnd);
        CreateWindowExW(0, L"STATIC", L"자동 줄바꿈 할 칸(열) 수 (0=창너비):", WS_CHILD | WS_VISIBLE, 15, 20, 250, 20, hwnd, nullptr, nullptr, nullptr);
        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 15, 45, 100, 24, hwnd, (HMENU)101, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"적용", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 130, 42, 60, 28, hwnd, (HMENU)IDOK, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 200, 42, 60, 28, hwnd, (HMENU)IDCANCEL, nullptr, nullptr);
        EnumChildWindows(hwnd, [](HWND c, LPARAM lp) -> BOOL { SendMessageW(c, WM_SETFONT, lp, TRUE); return TRUE; }, (LPARAM)hFont);

        wchar_t buf[32]; wsprintfW(buf, L"%d", *state->cols); SetWindowTextW(hEdit, buf);
        SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, 0, -1);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            wchar_t buf[32] = {}; GetWindowTextW(GetDlgItem(hwnd, 101), buf, 32);
            if (state) { *state->cols = _wtoi(buf); state->accepted = true; }
            DestroyWindow(hwnd); return 0;
        }
        else if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd); return 0;
        }
        break;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wParam, TRANSPARENT); return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool PromptMemoColumnWidth(HWND owner, int& cols) 
{
    static const wchar_t* kCls = L"TTMemoColWidth";
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = {}; wc.lpfnWndProc = MemoColWidthProc; wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kCls; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassW(&wc); reg = true;
    }
    MemoColWidthState state = { &cols, false };
    RECT rc; GetWindowRect(owner, &rc);
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kCls, L"열 너비 설정", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        rc.left + 50, rc.top + 50, 300, 130, owner, nullptr, GetModuleHandleW(nullptr), &state);
    EnableWindow(owner, FALSE);
    MSG msg; while (IsWindow(hDlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(owner, TRUE); SetActiveWindow(owner);
    if (g_memo.hwndEdit) SetFocus(g_memo.hwndEdit);
    return state.accepted;
}

static LRESULT CALLBACK MemoLineNumProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) 
{
    if (msg == WM_PAINT) {
        ScopedPaintDC paint(hwnd);
        HDC hdc = paint.Get();
        if (!hdc) return 0;
        
        // 1. 배경색 칠하기 (약간 연한 회색)
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));

        // 2. 폰트 설정 (회원님이 요청하신 GUI 기본 폰트)
        HFONT hFont = GetPopupUIFont(hwnd);
        HFONT hOld = (HFONT)SelectObject(hdc, hFont);
        SetTextColor(hdc, RGB(120, 120, 120)); // 숫자는 눈에 덜 띄게 회색으로
        SetBkMode(hdc, TRANSPARENT);

        // 3. 현재 메모장의 가시 영역 계산
        int firstLine = (int)SendMessageW(g_memo.hwndEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
        int lineCount = (int)SendMessageW(g_memo.hwndEdit, EM_GETLINECOUNT, 0, 0);
        
        // 4. 각 줄의 위치를 찾아서 숫자 그리기
        for (int i = firstLine; i < lineCount; ++i) {
            POINTL pt;
            // 해당 줄의 첫 번째 글자 위치를 가져옴
            int charIdx = (int)SendMessageW(g_memo.hwndEdit, EM_LINEINDEX, i, 0);
            SendMessageW(g_memo.hwndEdit, EM_POSFROMCHAR, (WPARAM)&pt, charIdx);
            
            // 창을 벗어나면 그만 그림
            if (pt.y > rc.bottom) break;

            wchar_t buf[16];
            wsprintfW(buf, L"%d ", i + 1);
            RECT lineRc = {0, (int)pt.y, rc.right - 5, (int)pt.y + 20};
            DrawTextW(hdc, buf, -1, &lineRc, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        }

        SelectObject(hdc, hOld);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void LoadMemoUserKeywords()
{
    std::wstring path = MakeAbsolutePath(GetModuleDirectory(), L"tintin_user.txt");

    // 파일이 없으면 생성만 해둠
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        static const char defaults[] = "Member\r\nObserver\r\n";
        WriteBytesToFile(path, defaults, sizeof(defaults) - 1);
    }

    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
        return;

    // ★ 이미 한 번 읽었고 수정 시간이 안 바뀌었으면 재로드 생략
    if (s_memoUserKeywordsLoaded &&
        CompareFileTime(&s_memoUserKeywordsFt, &fad.ftLastWriteTime) == 0)
    {
        return;
    }

    std::vector<std::wstring> newKeywords;

    std::wifstream wifs(path.c_str());
    wifs.imbue(std::locale("korean"));

    std::wstring wline;
    while (std::getline(wifs, wline)) {
        std::wstring trimmed = Trim(wline);
        if (!trimmed.empty())
            newKeywords.push_back(trimmed);
    }

    g_memo.userKeywords.swap(newKeywords);
    s_memoUserKeywordsFt = fad.ftLastWriteTime;
    s_memoUserKeywordsLoaded = true;
}

[[maybe_unused]] static void MemoReplaceSelection(HWND hEdit, const std::wstring& s)
{
    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)s.c_str());
}

static LONG MemoLineIndexFromLine(HWND hEdit, int line)
{
    return (LONG)SendMessageW(hEdit, EM_LINEINDEX, line, 0);
}

[[maybe_unused]] static int MemoGetLineLength(HWND hEdit, int line)
{
    LONG start = MemoLineIndexFromLine(hEdit, line);
    if (start < 0) return 0;
    return (int)SendMessageW(hEdit, EM_LINELENGTH, start, 0);
}

[[maybe_unused]] static void MemoSyncDrawPosition(HWND hEdit)
{
    int line = 0, col = 0;
    MemoGetCaretGrid(hEdit, line, col);

    if (MemoIsLineBrush(g_memo.selectedSymbol))
        col = MemoNormalizeLineCol(col);

    g_memo.drawLine = line;
    g_memo.drawCol = col;
    g_memo.drawPosValid = true;
}

[[maybe_unused]] static void MemoColumnInsertChar(HWND hEdit, int startLine, int endLine, int visualCol, wchar_t ch)
{
    SendMessageW(hEdit, WM_SETREDRAW, FALSE, 0);

    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = g_memo.textColor;

    for (int i = startLine; i <= endLine; ++i) {
        MemoEnsurePosition(hEdit, i, visualCol); // 줄이 짧으면 공백으로 채움
        std::wstring text = MemoGetLineText(hEdit, i);
        int actualCol = 0;
        int insertIdx = MemoVisualColToTextIndex(text, visualCol, &actualCol);

        LONG lineStart = (LONG)SendMessageW(hEdit, EM_LINEINDEX, i, 0);
        SendMessageW(hEdit, EM_SETSEL, lineStart + insertIdx, lineStart + insertIdx);
        SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

        wchar_t buf[2] = { ch, L'\0' };
        SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)buf);
    }

    // 삽입 후 다시 전체 행을 선택 상태로 만들어 연속 타이핑(스페이스바 연타)이 가능하게 함
    int newVisualCol = visualCol + MemoCharCellWidth(ch);
    LONG startLineIdx = (LONG)SendMessageW(hEdit, EM_LINEINDEX, startLine, 0);
    int startInsertIdx = MemoVisualColToTextIndex(MemoGetLineText(hEdit, startLine), newVisualCol, nullptr);
    LONG endLineIdx = (LONG)SendMessageW(hEdit, EM_LINEINDEX, endLine, 0);
    int endInsertIdx = MemoVisualColToTextIndex(MemoGetLineText(hEdit, endLine), newVisualCol, nullptr);

    SendMessageW(hEdit, EM_SETSEL, startLineIdx + startInsertIdx, endLineIdx + endInsertIdx);
    SendMessageW(hEdit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hEdit, nullptr, TRUE);
}

[[maybe_unused]] static void MemoColumnDeleteChar(HWND hEdit, int startLine, int endLine, int visualCol, bool isBackspace)
{
    if (isBackspace && visualCol == 0) return; // 0열에서 백스페이스 시 줄이 합쳐지는 것 방지

    SendMessageW(hEdit, WM_SETREDRAW, FALSE, 0);
    int deletedWidth = 1;

    for (int i = startLine; i <= endLine; ++i) {
        std::wstring text = MemoGetLineText(hEdit, i);
        int actualCol = 0;
        int textIdx = MemoVisualColToTextIndex(text, visualCol, &actualCol);
        LONG lineStart = (LONG)SendMessageW(hEdit, EM_LINEINDEX, i, 0);

        if (isBackspace) {
            if (textIdx > 0) {
                deletedWidth = MemoCharCellWidth(text[textIdx - 1]);
                SendMessageW(hEdit, EM_SETSEL, lineStart + textIdx - 1, lineStart + textIdx);
                SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)L"");
            }
        }
        else {
            if (textIdx < (int)text.size()) {
                SendMessageW(hEdit, EM_SETSEL, lineStart + textIdx, lineStart + textIdx + 1);
                SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)L"");
            }
        }
    }

    int newVisualCol = isBackspace ? (visualCol - deletedWidth) : visualCol;
    if (newVisualCol < 0) newVisualCol = 0;

    LONG startLineIdx = (LONG)SendMessageW(hEdit, EM_LINEINDEX, startLine, 0);
    int startInsertIdx = MemoVisualColToTextIndex(MemoGetLineText(hEdit, startLine), newVisualCol, nullptr);
    LONG endLineIdx = (LONG)SendMessageW(hEdit, EM_LINEINDEX, endLine, 0);
    int endInsertIdx = MemoVisualColToTextIndex(MemoGetLineText(hEdit, endLine), newVisualCol, nullptr);

    SendMessageW(hEdit, EM_SETSEL, startLineIdx + startInsertIdx, endLineIdx + endInsertIdx);
    SendMessageW(hEdit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hEdit, nullptr, TRUE);
}

static bool IsTintinCommandChar(wchar_t c) 
{
    return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || c == L'_';
}

static bool MemoRangeEquals(const std::wstring& text, int start, int end, const wchar_t* word, bool ignoreCase = false)
{
    if (!word || start < 0 || end < start)
        return false;

    const int len = end - start;
    int i = 0;
    for (; i < len && word[i] != 0; ++i)
    {
        wchar_t a = text[(size_t)start + (size_t)i];
        wchar_t b = word[i];
        if (ignoreCase)
        {
            a = (wchar_t)towlower(a);
            b = (wchar_t)towlower(b);
        }
        if (a != b)
            return false;
    }
    return i == len && word[i] == 0;
}

static bool IsCppKeywordRange(const std::wstring& text, int start, int end)
{
    static const wchar_t* kw[] = {
        L"int", L"char", L"void", L"float", L"double", L"bool", L"long", L"short",
        L"if", L"else", L"for", L"while", L"do", L"switch", L"case", L"default",
        L"return", L"break", L"continue", L"class", L"struct", L"enum", L"union",
        L"public", L"private", L"protected", L"static", L"const", L"new", L"delete",
        L"namespace", L"using", L"true", L"false", L"nullptr", L"sizeof", L"auto"
    };
    for (const wchar_t* word : kw)
    {
        if (MemoRangeEquals(text, start, end, word))
            return true;
    }
    return false;
}

static bool IsShellKeywordRange(const std::wstring& text, int start, int end)
{
    static const wchar_t* kw[] = {
        L"if", L"then", L"else", L"elif", L"fi", L"case", L"esac", L"for", L"while",
        L"until", L"do", L"done", L"in", L"echo", L"read", L"export", L"local",
        L"return", L"function", L"break", L"continue", L"exit"
    };
    for (const wchar_t* word : kw)
    {
        if (MemoRangeEquals(text, start, end, word))
            return true;
    }
    return false;
}

static void SetMemoThemeBaseColors(int themeIdx) 
{
    if (themeIdx == 1) { // 울트라에디터 (Light)
        g_memo.backColor = RGB(255, 255, 255); g_memo.textColor = RGB(0, 0, 0);
    }
    else if (themeIdx == 2) { // VS Code (Dark)
        g_memo.backColor = RGB(30, 30, 30);    g_memo.textColor = RGB(212, 212, 212);
    }
    else if (themeIdx == 3) { // 모노카이 (Dark)
        g_memo.backColor = RGB(39, 40, 34);    g_memo.textColor = RGB(248, 248, 242);
    }
    else if (themeIdx == 4) { // 드라큘라 (Dark)
        g_memo.backColor = RGB(40, 42, 54);    g_memo.textColor = RGB(248, 248, 242);
    }
    else if (themeIdx == 5) { // 기본 다크 (Dark)
        g_memo.backColor = RGB(15, 15, 15);    g_memo.textColor = RGB(230, 230, 230);
    }
    else if (themeIdx == 6) { // 솔라라이즈드 (Light)
        g_memo.backColor = RGB(253, 246, 227); g_memo.textColor = RGB(101, 123, 131);
    }
    else if (themeIdx == 7) { // 솔라라이즈드 (Dark)
        g_memo.backColor = RGB(0, 43, 54);     g_memo.textColor = RGB(131, 148, 150);
    }
    else if (themeIdx == 8) { // 깃허브 다크 (Dark)
        g_memo.backColor = RGB(13, 17, 23);    g_memo.textColor = RGB(201, 209, 217);
    }
    else { // 0: 기본 라이트
        g_memo.backColor = RGB(255, 255, 255); g_memo.textColor = RGB(0, 0, 0);
    }
}

// ==============================================
// 메인 윈도우 프로시저 및 서브클래스 프로시저// 
// ==============================================
struct AutoSaveIntervalState { int* sec; bool accepted; };
static LRESULT CALLBACK AutoSaveIntervalProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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

static bool PromptMemoAutoSaveInterval(HWND owner, int& sec) {
    static bool reg = false;
    if (!reg) { WNDCLASSW wc = {}; wc.lpfnWndProc = AutoSaveIntervalProc; wc.lpszClassName = L"TTAUtoSaveInt"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassW(&wc); reg = true; }
    AutoSaveIntervalState st = { &sec, false };
    RECT rc; GetWindowRect(owner, &rc);
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"TTAUtoSaveInt", L"자동저장 설정", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, rc.left + 50, rc.top + 50, 300, 130, owner, 0, 0, &st);
    EnableWindow(owner, FALSE); MSG msg; while (IsWindow(hDlg) && GetMessageW(&msg, 0, 0, 0)) { if (!IsDialogMessageW(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    EnableWindow(owner, TRUE); SetActiveWindow(owner); return st.accepted;
}

static LRESULT CALLBACK MemoWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (mis && mis->CtlType == ODT_MENU) { MeasureOwnerDrawMenuItem(hwnd, mis); return TRUE; }
        break;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis && dis->CtlType == ODT_MENU) { DrawOwnerDrawMenuItem(dis); return TRUE; }
        break;
    }


    case WM_CREATE:
    {
        g_memo.hwnd = hwnd;

        // 메인 창에 파일 끌어다 놓기 허용
        DragAcceptFiles(hwnd, TRUE);

        // ★ 메뉴를 만들기 전에 저장된 최근 파일 목록을 먼저 불러옵니다!
        LoadMemoRecentFiles();
        CreateMemoMenus(hwnd);

        g_memo.hwndEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL,
            0, 0, 100, 100, hwnd, (HMENU)(INT_PTR)ID_MEMO_EDIT, GetModuleHandleW(nullptr), nullptr);

        // ★ 에디트 창에도 파일 끌어다 놓기 허용 (안 그러면 RichEdit이 먹어버림)
        DragAcceptFiles(g_memo.hwndEdit, TRUE);

        // 이중 폰트 방지
        LRESULT lOpts = SendMessageW(g_memo.hwndEdit, EM_GETLANGOPTIONS, 0, 0);
        lOpts &= ~(IMF_AUTOFONT | IMF_AUTOKEYBOARD | IMF_DUALFONT | IMF_UIFONTS);
        SendMessageW(g_memo.hwndEdit, EM_SETLANGOPTIONS, 0, lOpts);

        g_memo.hwndStatus = CreateWindowExW(
            0, STATUSCLASSNAME, nullptr,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_MEMO_STATUS, GetModuleHandleW(nullptr), nullptr);

        SetWindowSubclass(g_memo.hwndEdit, MemoEditSubclassProc, 1, 0);

        SendMessageW(g_memo.hwndStatus, WM_SETFONT, (WPARAM)GetPopupUIFont(hwnd), TRUE);

        std::wstring path = GetSettingsPath();
        wchar_t buf[256];
        g_memo.textColor = StringToColor((GetPrivateProfileStringW(L"memo", L"text_color", L"0,0,0", buf, 256, path.c_str()), buf), RGB(0, 0, 0));
        g_memo.backColor = StringToColor((GetPrivateProfileStringW(L"memo", L"back_color", L"255,255,255", buf, 256, path.c_str()), buf), RGB(255, 255, 255));
        g_memo.wrapCols = GetPrivateProfileIntW(L"memo", L"wrap_cols", 0, path.c_str());

        s_lastMemoHighlightSig = 0;
        // ★ 테마 설정 불러오기
        g_memo.syntaxTheme = GetPrivateProfileIntW(L"memo", L"syntax_theme", 0, path.c_str());
        g_memo.useSyntax = (GetPrivateProfileIntW(L"memo", L"use_syntax", 1, path.c_str()) != 0);
        g_memo.showFormatMarks = (GetPrivateProfileIntW(L"memo", L"show_format_marks", 0, path.c_str()) != 0);
        SetMemoThemeBaseColors(g_memo.syntaxTheme);
        UpdateMemoMenuState(hwnd);

        // 1. 일단 구조체를 깨끗하게 비우고 시작합니다 (중요!)
        ZeroMemory(&g_memo.font, sizeof(g_memo.font));

        // 폰트 정보 불러오기 시도
        GetPrivateProfileStringW(L"memo", L"font_face", L"", buf, 256, path.c_str());

        if (g_app && g_app->useCustomMudFont) {
            ZeroMemory(&g_memo.font, sizeof(g_memo.font));
            g_memo.font.lfHeight = MakeLfHeightFromPointSize(hwnd, 12);
            g_memo.font.lfWeight = FW_NORMAL;
            g_memo.font.lfCharSet = HANGEUL_CHARSET;
            g_memo.font.lfQuality = GetCurrentFontQuality();
            g_memo.font.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
            lstrcpyW(g_memo.font.lfFaceName, L"Mud둥근모");
        }
        else if (g_app && g_app->inputStyle.font.lfFaceName[0] != L'\0') {
            // [사용자 개별 설정이 있는 경우]
            lstrcpynW(g_memo.font.lfFaceName, buf, LF_FACESIZE);

            wchar_t heightBuf[32];
            GetPrivateProfileStringW(L"memo", L"font_height", L"-16", heightBuf, 32, path.c_str());
            g_memo.font.lfHeight = _wtoi(heightBuf);
            g_memo.font.lfWeight = GetPrivateProfileIntW(L"memo", L"font_weight", FW_NORMAL, path.c_str());

            // 개별 설정이라도 한글과 품질 설정은 채워주는 것이 안전합니다.
            g_memo.font.lfCharSet = HANGEUL_CHARSET;
            g_memo.font.lfQuality = GetCurrentFontQuality(); // utils에 만든 함수 활용
        }
        else if (g_app && g_app->inputStyle.font.lfFaceName[0] != L'\0') {
            // [설정이 없으면 입력창 폰트를 그대로 복사]
            g_memo.font = g_app->inputStyle.font;
        }
        else {
            ZeroMemory(&g_memo.font, sizeof(g_memo.font));
            g_memo.font.lfHeight = MakeLfHeightFromPointSize(hwnd, 12);
            g_memo.font.lfWeight = FW_NORMAL;
            g_memo.font.lfCharSet = HANGEUL_CHARSET;
            g_memo.font.lfQuality = GetCurrentFontQuality();
            g_memo.font.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
            lstrcpyW(g_memo.font.lfFaceName, L"Mud둥근모");
        }

        SendMessageW(g_memo.hwndEdit, EM_SETLIMITTEXT, 0, 0);
        SendMessageW(g_memo.hwndEdit, EM_SETUNDOLIMIT, 10000, 0);
        SendMessageW(g_memo.hwndEdit, EM_SETTEXTMODE, TM_MULTILEVELUNDO | TM_MULTICODEPAGE, 0);

        SetWindowSubclass(g_memo.hwndEdit, MemoEditSubclassProc, 1, 0);

        // 행 번호 창 생성
        static bool regLine = false;
        if (!regLine) {
            WNDCLASSW wc = {}; wc.lpfnWndProc = MemoLineNumProc; wc.hInstance = GetModuleHandle(0);
            wc.lpszClassName = L"TTMemoLineNum"; wc.hCursor = LoadCursor(0, IDC_ARROW);
            RegisterClassW(&wc); regLine = true;
        }
        g_memo.hwndLineNum = CreateWindowExW(0, L"TTMemoLineNum", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, 0, 0, 0);

        // ★ 회원님 아이디어: 로딩 끝나갈 때 한 번 더 강제로 폰트를 입힙니다!
        ApplyMemoFontAndFormat();

        SetFocus(g_memo.hwndEdit);
        StartWinTimer(hwnd, ID_TIMER_MEMO_AUTOSAVE, 3000);

        UpdateMemoTitle();
        UpdateMemoStatus();
        return 0;
    }

    case WM_SIZE:
        LayoutMemoChildren(hwnd);
        return 0;

    case WM_TIMER:
        if (wParam == ID_TIMER_MEMO_AUTOSAVE) { MemoAutoSave(); return 0; }
        break;

    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
        if (HandleMemoShortcutKey(msg, wParam))
            return 0;
        break;

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case ID_MEMO_FILE_OPEN:   MemoDoOpenDialog(hwnd); return 0;
        case ID_MEMO_FILE_SAVE:   MemoDoSaveDialog(hwnd, false); return 0;
        case ID_MEMO_FILE_SAVEAS:
        { // ★ 변수 선언을 위해 중괄호 블록을 열어줍니다.
            int r = ShowCenteredMessageBox(hwnd,
                g_memo.encodingType == 0 ?
                L"UTF-8 (BOM없음) 인코딩으로 저장할까요?\n(아니오 클릭 시 CP949로 변경)" :
                L"CP949 (한국어) 인코딩으로 저장할까요?\n(아니오 클릭 시 UTF-8로 변경)",
                L"저장 인코딩 확인", MB_YESNOCANCEL | MB_ICONQUESTION);

            if (r == IDCANCEL) return 0; // 취소 누르면 저장 안 함

            if (r == IDNO) {
                // "아니오"를 누르면 인코딩을 서로 교체합니다.
                g_memo.encodingType = (g_memo.encodingType == 0 ? 1 : 0);
                UpdateMemoStatus(); // ★ 인코딩이 바뀌었으니 상태바의 텍스트도 즉시 갱신!
            }

            // 이제 실제 저장 다이얼로그를 띄웁니다.
            MemoDoSaveDialog(hwnd, true);
            return 0;
        } // ★ 중괄호 블록을 닫아줍니다.

        case ID_MEMO_FILE_EXIT:   DestroyWindow(hwnd); return 0;

        case ID_MEMO_EDIT_UNDO:      SendMessageW(g_memo.hwndEdit, EM_UNDO, 0, 0); return 0;
        case ID_MEMO_EDIT_REDO:      SendMessageW(g_memo.hwndEdit, EM_REDO, 0, 0); return 0;
        case ID_MEMO_EDIT_CUT:       SendMessageW(g_memo.hwndEdit, WM_CUT, 0, 0); return 0;
        case ID_MEMO_EDIT_COPY:      SendMessageW(g_memo.hwndEdit, WM_COPY, 0, 0); return 0;
        case ID_MEMO_EDIT_PASTE:     SendMessageW(g_memo.hwndEdit, WM_PASTE, 0, 0); return 0;
        case ID_MEMO_EDIT_DELETE:    MemoDeleteSelectionOrChar(g_memo.hwndEdit); return 0;
        case ID_MEMO_EDIT_SELECTALL: SendMessageW(g_memo.hwndEdit, EM_SETSEL, 0, -1); return 0;

        case ID_MEMO_EDIT_FIND: ShowMemoFindReplaceDialog(hwnd, false); return 0;
        case ID_MEMO_EDIT_REPLACE: ShowMemoFindReplaceDialog(hwnd, true); return 0;
        case ID_MEMO_EDIT_FIND_NEXT: MemoPerformFind(true); return 0;
        case ID_MEMO_EDIT_FIND_PREV: MemoPerformFind(false); return 0;
        case ID_MEMO_EDIT_GOTO: ShowMemoGoToDialog(hwnd); return 0;
        case ID_MENU_VIEW_SYMBOLS:
        {
            if (g_app)
                g_app->hwndTargetEdit = g_memo.hwndEdit ? g_memo.hwndEdit : GetFocus();

            ShowSymbolDialog(hwnd);

            if (g_app && g_app->hwndSymbol && IsWindow(g_app->hwndSymbol))
            {
                SetWindowPos(g_app->hwndSymbol, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                BringWindowToTop(g_app->hwndSymbol);
                SetForegroundWindow(g_app->hwndSymbol);
            }
            return 0;
        }
        case ID_MEMO_FORMAT_ENC_UTF8:
            g_memo.encodingType = 0;
            UpdateMemoStatus();
            MarkMemoDirty(true); // 인코딩 바뀌었으므로 저장 필요 표시
            return 0;

        case ID_MEMO_FORMAT_ENC_CP949:
            g_memo.encodingType = 1;
            UpdateMemoStatus();
            MarkMemoDirty(true);
            return 0;

            // ★ 새로 추가된 메뉴 버튼 동작 구현
        case ID_MEMO_EDIT_DOC_START:
            SendMessageW(g_memo.hwndEdit, EM_SETSEL, 0, 0);
            SendMessageW(g_memo.hwndEdit, EM_SCROLLCARET, 0, 0);
            return 0;

        case ID_MEMO_EDIT_DOC_END:
        {
            int len = GetWindowTextLengthW(g_memo.hwndEdit);
            SendMessageW(g_memo.hwndEdit, EM_SETSEL, len, len);
            SendMessageW(g_memo.hwndEdit, EM_SCROLLCARET, 0, 0);
            return 0;
        }

        case ID_MEMO_EDIT_SCR_START:
        {
            int firstVisible = (int)SendMessageW(g_memo.hwndEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
            int target = (int)SendMessageW(g_memo.hwndEdit, EM_LINEINDEX, firstVisible, 0);
            SendMessageW(g_memo.hwndEdit, EM_SETSEL, target, target);
            SendMessageW(g_memo.hwndEdit, EM_SCROLLCARET, 0, 0);
            return 0;
        }

        case ID_MEMO_EDIT_SCR_END:
        {
            int firstVisible = (int)SendMessageW(g_memo.hwndEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
            RECT rc; GetClientRect(g_memo.hwndEdit, &rc);
            ScopedWindowDC dc(g_memo.hwndEdit);
            if (!dc) return 0;

            HDC hdc = dc.Get();
            HFONT oldFont = (HFONT)SelectObject(hdc, g_memo.hFont);
            TEXTMETRICW tm{}; GetTextMetricsW(hdc, &tm);
            if (oldFont) SelectObject(hdc, oldFont);

            int visibleLines = tm.tmHeight > 0 ? RectHeight(rc) / tm.tmHeight : 1;
            int targetLine = firstVisible + visibleLines - 1;
            int maxLines = (int)SendMessageW(g_memo.hwndEdit, EM_GETLINECOUNT, 0, 0);
            if (targetLine >= maxLines) targetLine = maxLines - 1;

            int lineIdx = (int)SendMessageW(g_memo.hwndEdit, EM_LINEINDEX, targetLine, 0);
            int lineLen = (int)SendMessageW(g_memo.hwndEdit, EM_LINELENGTH, lineIdx, 0);

            SendMessageW(g_memo.hwndEdit, EM_SETSEL, lineIdx + lineLen, lineIdx + lineLen);
            SendMessageW(g_memo.hwndEdit, EM_SCROLLCARET, 0, 0);
            return 0;
        }

        case ID_MEMO_EDIT_DEL_END:
        {
            CHARRANGE cr; SendMessageW(g_memo.hwndEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            int line = (int)SendMessageW(g_memo.hwndEdit, EM_LINEFROMCHAR, cr.cpMin, 0);
            int lineIdx = (int)SendMessageW(g_memo.hwndEdit, EM_LINEINDEX, line, 0);
            int lineLen = (int)SendMessageW(g_memo.hwndEdit, EM_LINELENGTH, lineIdx, 0);
            SendMessageW(g_memo.hwndEdit, EM_SETSEL, cr.cpMin, lineIdx + lineLen);
            SendMessageW(g_memo.hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)L"");
            return 0;
        }

        // ★ 앞 단어 지우기 (Ctrl + Backspace 를 누른 것과 똑같은 효과)
        case ID_MEMO_EDIT_DEL_WORD_LEFT:
        {
            BYTE kbuf[256]; GetKeyboardState(kbuf);
            kbuf[VK_CONTROL] |= 0x80; SetKeyboardState(kbuf); // Ctrl 누른 척
            SendMessageW(g_memo.hwndEdit, WM_KEYDOWN, VK_BACK, 0); // Backspace 전송
            kbuf[VK_CONTROL] &= ~0x80; SetKeyboardState(kbuf); // Ctrl 떼기
            return 0;
        }

        // ★ 뒷 단어 지우기 (Ctrl + Delete 를 누른 것과 똑같은 효과)
        case ID_MEMO_EDIT_DEL_WORD_RIGHT:
        {
            BYTE kbuf[256]; GetKeyboardState(kbuf);
            kbuf[VK_CONTROL] |= 0x80; SetKeyboardState(kbuf); // Ctrl 누른 척
            SendMessageW(g_memo.hwndEdit, WM_KEYDOWN, VK_DELETE, 0); // Delete 전송
            kbuf[VK_CONTROL] &= ~0x80; SetKeyboardState(kbuf); // Ctrl 떼기
            return 0;
        }

        case ID_MEMO_EDIT_DEL_LINE:
        {
            CHARRANGE cr; SendMessageW(g_memo.hwndEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            int line = (int)SendMessageW(g_memo.hwndEdit, EM_LINEFROMCHAR, cr.cpMin, 0);
            int lineIdx = (int)SendMessageW(g_memo.hwndEdit, EM_LINEINDEX, line, 0);
            int lineLen = (int)SendMessageW(g_memo.hwndEdit, EM_LINELENGTH, lineIdx, 0);

            int targetEnd = lineIdx + lineLen;
            std::wstring text = GetWindowTextString(g_memo.hwndEdit);
            if (targetEnd < (int)text.size() && text[targetEnd] == L'\r') targetEnd++;
            if (targetEnd < (int)text.size() && text[targetEnd] == L'\n') targetEnd++;

            SendMessageW(g_memo.hwndEdit, EM_SETSEL, lineIdx, targetEnd);
            SendMessageW(g_memo.hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)L"");
            return 0;
        }

        case ID_MEMO_DRAW_TOGGLE:
            g_memo.drawMode = !g_memo.drawMode;
            UpdateMemoMenuState(hwnd);
            UpdateMemoStatus();
            return 0;

        case ID_MEMO_AUTOSAVE_TOGGLE:
        {
            if (g_memo.autoSave) {
                g_memo.autoSave = false; // 켜져 있으면 끕니다.
                KillWinTimer(hwnd, ID_TIMER_MEMO_AUTOSAVE);
            }
            else {
                int sec = g_memo.autoSaveIntervalSec;
                // 꺼져 있으면 시간을 묻고 켭니다.
                if (PromptMemoAutoSaveInterval(hwnd, sec)) {
                    g_memo.autoSaveIntervalSec = sec;
                    g_memo.autoSave = true;
                    StartWinTimer(hwnd, ID_TIMER_MEMO_AUTOSAVE, g_memo.autoSaveIntervalSec * 1000);
                }
            }
            UpdateMemoMenuState(hwnd);
            UpdateMemoStatus();
            return 0;
        };

        // ★ 새로 추가된 복구 파일 불러오기 명령
        case ID_MEMO_FILE_LOAD_AUTOSAVE:
        {
            std::wstring autoFile;
            // 1. 목록 팝업을 띄우고 사용자가 파일을 선택하면
            if (PromptMemoLoadAutoSave(hwnd, autoFile)) {
                // 2. 현재 창에 수정중인 내용이 있으면 경고 팝업을 띄움
                if (g_memo.dirty) {
                    int r = ShowCenteredMessageBox(hwnd,
                        L"현재 수정 중인 내용을 무시하고 복구 파일을 덮어씌우시겠습니까?\n(예=복구 파일로 덮어쓰기, 아니오=기존 내용 먼저 저장)",
                        L"경고", MB_YESNOCANCEL | MB_ICONWARNING);
                    if (r == IDCANCEL) return 0;
                    if (r == IDNO) { if (!MemoDoSaveDialog(hwnd, false)) return 0; }
                }
                // 3. 파일 열기
                MemoOpenFile(hwnd, autoFile);
            }
            return 0;
        }

        case ID_MEMO_FORMAT_TEXT_COLOR:
            if (ChooseColorOnly(hwnd, g_memo.textColor)) {
                CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf); cf.dwMask = CFM_COLOR; cf.crTextColor = g_memo.textColor;
                SendMessageW(g_memo.hwndEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
                std::wstring path = GetSettingsPath();
                WritePrivateProfileStringW(L"memo", L"text_color", ColorToString(g_memo.textColor).c_str(), path.c_str());

                s_lastMemoHighlightSig = 0;
                // ★ 글자색이 바뀌면 구문 강조도 새 기준색으로 덮어쓰기 위해 즉시 재적용
                if (g_memo.useSyntax) ApplyMemoSyntaxHighlight(g_memo.hwndEdit);
            }
            return 0;

        case ID_MEMO_FORMAT_BACK_COLOR:
            if (ChooseBackgroundColor(hwnd, g_memo.backColor)) {
                SendMessageW(g_memo.hwndEdit, EM_SETBKGNDCOLOR, 0, g_memo.backColor);
                InvalidateRect(g_memo.hwndEdit, nullptr, TRUE);
                std::wstring path = GetSettingsPath();
                WritePrivateProfileStringW(L"memo", L"back_color", ColorToString(g_memo.backColor).c_str(), path.c_str());
            }
            return 0;

        case ID_MEMO_FORMAT_FONT:
            if (ChooseFontOnly(hwnd, g_memo.font)) {
                ApplyMemoFontAndFormat();
                std::wstring path = GetSettingsPath();
                WritePrivateProfileStringW(L"memo", L"font_face", g_memo.font.lfFaceName, path.c_str());
                wchar_t numBuf[32]; wsprintfW(numBuf, L"%ld", g_memo.font.lfHeight); WritePrivateProfileStringW(L"memo", L"font_height", numBuf, path.c_str());
                wsprintfW(numBuf, L"%ld", g_memo.font.lfWeight); WritePrivateProfileStringW(L"memo", L"font_weight", numBuf, path.c_str());

                s_lastMemoHighlightSig = 0;
                // ★ 폰트가 바뀌면 서식이 뭉개지므로 즉시 구문 강조 재적용
                if (g_memo.useSyntax) ApplyMemoSyntaxHighlight(g_memo.hwndEdit);
            }
            return 0;

            // ★ 구문 테마 변경 클릭 시 동작 (9종 테마 배경색/글자색 완벽 동기화)
        case ID_MEMO_SYNTAX_THEME_CLASSIC:
        case ID_MEMO_SYNTAX_THEME_ULTRAEDIT:
        case ID_MEMO_SYNTAX_THEME_VSCODE:
        case ID_MEMO_SYNTAX_THEME_MONOKAI:
        case ID_MEMO_SYNTAX_THEME_DRACULA:
        case ID_MEMO_SYNTAX_THEME_DEFAULT_DARK:
        case ID_MEMO_SYNTAX_THEME_SOLAR_LIGHT:
        case ID_MEMO_SYNTAX_THEME_SOLAR_DARK:
        case ID_MEMO_SYNTAX_THEME_GITHUB_DARK:
        {
            int themeIdx = LOWORD(wParam) - ID_MEMO_SYNTAX_THEME_BASE;
            g_memo.syntaxTheme = themeIdx;
            s_lastMemoHighlightSig = 0;

            // ★ 만들어둔 함수로 테마 스킨(배경/글자색) 즉시 세팅
            SetMemoThemeBaseColors(themeIdx);

            // 포맷(배경색/기본글자색)을 메모장 전체에 즉시 주입
            ApplyMemoFontAndFormat();

            // 설정 저장
            std::wstring path = GetSettingsPath();
            wchar_t buf[32]; wsprintfW(buf, L"%d", g_memo.syntaxTheme);
            WritePrivateProfileStringW(L"memo", L"syntax_theme", buf, path.c_str());
            WritePrivateProfileStringW(L"memo", L"back_color", ColorToString(g_memo.backColor).c_str(), path.c_str());
            WritePrivateProfileStringW(L"memo", L"text_color", ColorToString(g_memo.textColor).c_str(), path.c_str());

            // ★ tin 파일 등 구문 강조가 켜져 있는 상태일 때만 명령어 색칠 엔진 가동
            // (일반 txt 파일은 배경과 기본 글씨색만 바뀐 채로 깔끔하게 멈춤)
            if (g_memo.useSyntax) {
                ApplyMemoSyntaxHighlight(g_memo.hwndEdit);
            }

            UpdateMemoStatus();
            return 0;
        }

        case ID_MEMO_FORMAT_WRAP_WIDTH:
            if (PromptMemoColumnWidth(hwnd, g_memo.wrapCols)) {
                ApplyMemoWrapWidth();
                std::wstring path = GetSettingsPath();
                wchar_t numBuf[32]; wsprintfW(numBuf, L"%d", g_memo.wrapCols);
                WritePrivateProfileStringW(L"memo", L"wrap_cols", numBuf, path.c_str());
            }
            return 0;

        case ID_MEMO_ALIGN_LEFT:
        case ID_MEMO_ALIGN_CENTER:
        case ID_MEMO_ALIGN_RIGHT:
        {
            PARAFORMAT2 pf = {};
            pf.cbSize = sizeof(pf);
            pf.dwMask = PFM_ALIGNMENT;
            if (LOWORD(wParam) == ID_MEMO_ALIGN_LEFT) pf.wAlignment = PFA_LEFT;
            if (LOWORD(wParam) == ID_MEMO_ALIGN_CENTER) pf.wAlignment = PFA_CENTER;
            if (LOWORD(wParam) == ID_MEMO_ALIGN_RIGHT) pf.wAlignment = PFA_RIGHT;
            SendMessageW(g_memo.hwndEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
            return 0;
        }

        case ID_MEMO_VIEW_LINENUMBER:
        {
            g_memo.showLineNumbers = !g_memo.showLineNumbers;
            LayoutMemoChildren(hwnd);
            UpdateMemoStatus();
            return 0;
        }

        case ID_MEMO_VIEW_FORMATMARKS:
        {
            g_memo.showFormatMarks = !g_memo.showFormatMarks;

            WritePrivateProfileStringW(
                L"memo",
                L"show_format_marks",
                g_memo.showFormatMarks ? L"1" : L"0",
                GetSettingsPath().c_str());

            UpdateMemoMenuState(hwnd);
            InvalidateRect(g_memo.hwndEdit, nullptr, TRUE);
            UpdateWindow(g_memo.hwndEdit);
            return 0;
        }

        case ID_MEMO_REPEAT_SYMBOL:
        {
            // 기억해둔 마지막 기호가 있으면 즉시 입력
            if (!g_memo.lastSymbol.empty() && g_memo.hwndEdit) {
                MemoInsertText(g_memo.hwndEdit, g_memo.lastSymbol);
                MarkMemoDirty(true);
                UpdateMemoStatus();
            }
            else {
                // 아직 고른 적이 없으면 F4 창을 띄워줌
                ShowSymbolDialog(hwnd);
            }
            return 0;
        }

        case ID_MEMO_FORMAT_SYNTAX_TINTIN:
        {
            g_memo.useSyntax = !g_memo.useSyntax;
            s_lastMemoHighlightSig = 0;

            std::wstring path = GetSettingsPath();
            WritePrivateProfileStringW(
                L"memo",
                L"use_syntax",
                g_memo.useSyntax ? L"1" : L"0",
                path.c_str());

            if (g_memo.useSyntax) {
                LoadMemoUserKeywords();
                ApplyMemoSyntaxHighlight(g_memo.hwndEdit);
            }
            else {
                ApplyMemoFontAndFormat();
            }

            UpdateMemoMenuState(hwnd);
            UpdateMemoStatus();
            return 0;
        }

        case ID_MEMO_FORMAT_SYNTAX_LANG_TINTIN:
        {
            g_memo.syntaxLang = 1;
            s_lastMemoHighlightSig = 0;

            if (g_memo.useSyntax) {
                LoadMemoUserKeywords();
                ApplyMemoSyntaxHighlight(g_memo.hwndEdit);
            }
            UpdateMemoMenuState(hwnd);
            return 0;
        }

        case ID_MEMO_FORMAT_SYNTAX_LANG_CPP:
        {
            g_memo.syntaxLang = 2;
            s_lastMemoHighlightSig = 0;

            if (g_memo.useSyntax) {
                LoadMemoUserKeywords();
                ApplyMemoSyntaxHighlight(g_memo.hwndEdit);
            }
            UpdateMemoMenuState(hwnd);
            return 0;
        }

        case ID_MEMO_FORMAT_SYNTAX_LANG_CSHARP:
        {
            g_memo.syntaxLang = 3;
            s_lastMemoHighlightSig = 0;

            if (g_memo.useSyntax) {
                LoadMemoUserKeywords();
                ApplyMemoSyntaxHighlight(g_memo.hwndEdit);
            }
            UpdateMemoMenuState(hwnd);
            return 0;
        }

        case ID_MEMO_EXIT_TO_TINTIN:
            // 메모장은 그대로 두고 메인 창에 포커스만 줍니다.
            SetForegroundWindow(g_app->hwndMain);
            if (g_app->hwndEdit[g_app->activeEditIndex]) {
                SetFocus(g_app->hwndEdit[g_app->activeEditIndex]);
            }
            return 0;

        default:
            if (LOWORD(wParam) >= ID_MEMO_RECENT_BASE && LOWORD(wParam) < ID_MEMO_RECENT_BASE + 5) {
                int idx = LOWORD(wParam) - ID_MEMO_RECENT_BASE;
                if (idx >= 0 && idx < (int)g_memo.recentFiles.size()) MemoOpenFile(hwnd, g_memo.recentFiles[idx]);
                return 0;
            }
            break;
        }
        break;
    }

    case WM_DROPFILES:
    {
        HDROP hDrop = (HDROP)wParam;
        wchar_t filePath[MAX_PATH] = { 0 };

        // 여러 파일을 드롭했더라도 첫 번째 파일 1개만 엽니다.
        if (DragQueryFileW(hDrop, 0, filePath, MAX_PATH) > 0)
        {
            // 현재 작업 중인 내용이 변경되었다면 저장할지 먼저 물어보는 안전장치
            if (g_memo.dirty) {
                int r = ShowCenteredMessageBox(hwnd, L"저장되지 않은 변경사항이 있습니다. 저장할까요?", L"메모장", MB_ICONQUESTION | MB_YESNOCANCEL);
                if (r == IDCANCEL) {
                    DragFinish(hDrop);
                    return 0; // 취소하면 파일 안 열고 돌아감
                }
                if (r == IDYES) {
                    if (!MemoDoSaveDialog(hwnd, false)) {
                        DragFinish(hDrop);
                        return 0; // 저장 실패/취소 시 파일 안 열기
                    }
                }
            }

            // 안전하게 기존 내용을 처리했으므로, 드롭된 파일 열기!
            MemoOpenFile(hwnd, filePath);
        }

        DragFinish(hDrop); // 메모리 해제 필수
        return 0;
    }

    case WM_CLOSE:
    {
        if (g_memo.dirty) {
            // ★ 기존 MessageBoxW 대신 새로 만든 ShowCenteredMessageBox 사용
            int r = ShowCenteredMessageBox(hwnd, L"저장되지 않은 변경사항이 있습니다. 저장할까요?", L"메모장", MB_ICONQUESTION | MB_YESNOCANCEL);
            if (r == IDCANCEL) return 0;
            if (r == IDYES) { if (!MemoDoSaveDialog(hwnd, false)) return 0; }
        }

        HWND hOwner = GetWindow(hwnd, GW_OWNER);
        if (hOwner && IsWindow(hOwner)) {
            SetActiveWindow(hOwner);
            if (IsIconic(hOwner)) ShowWindow(hOwner, SW_RESTORE);
        }
        DestroyWindow(hwnd);
        return 0;
    }

    case WM_DESTROY:
    {
        KillWinTimer(hwnd, ID_TIMER_MEMO_AUTOSAVE);
        SaveMemoWindowSettings();
        if (g_memo.hwndEdit) RemovePropW(g_memo.hwndEdit, L"MemoOldProc");

        HWND hParent = GetWindow(hwnd, GW_OWNER);
        ResetGdiObjectRef(g_memo.hFont);
        g_memo = MemoState{}; // 상태 완전 초기화
        if (hParent) SetFocus(hParent);

        return 0;
    }

    } // switch 종료

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK LineSelectPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        ApplyPopupTitleBarTheme(hwnd);

        // ★ 시스템 폰트가 아닌 메모장(입력창)에 적용된 실제 폰트를 사용
        HFONT hFont = g_app && g_app->hFontInput ? g_app->hFontInput : GetPopupUIFont(hwnd);

        HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | WS_VSCROLL,
            15, 15, 270, 130, hwnd, (HMENU)ID_LINE_LIST, GetModuleHandle(0), nullptr);

        for (int i = 0; i < 4; ++i) {
            SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)g_lineSets[i].display);
        }
        SendMessageW(hList, WM_SETFONT, (WPARAM)hFont, TRUE);

        // ★ 행 높이를 늘려 쾌적하게 렌더링
        SendMessageW(hList, LB_SETITEMHEIGHT, 0, 28);
        SendMessageW(hList, LB_SETCURSEL, (WPARAM)g_currentLineSetIdx, 0);
        SetFocus(hList);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_LINE_LIST && HIWORD(wParam) == LBN_DBLCLK) {
            SendMessageW(hwnd, WM_COMMAND, IDOK, 0);
        }
        else if (LOWORD(wParam) == IDOK) {
            int sel = (int)SendMessageW(GetDlgItem(hwnd, ID_LINE_LIST), LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) g_currentLineSetIdx = sel;
            DestroyWindow(hwnd);
        }
        else if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd);
        }
        break;
    case WM_CLOSE: DestroyWindow(hwnd); break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

[[maybe_unused]] static bool ShowLineSelectDialog(HWND owner) {
    static const wchar_t* kCls = L"LineSelectPopup";
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = {}; wc.lpfnWndProc = LineSelectPopupProc; wc.hInstance = GetModuleHandle(0);
        wc.lpszClassName = kCls; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc); reg = true;
    }

    RECT rc = {}; if (owner) GetWindowRect(owner, &rc);

    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kCls, L"선 모양 선택 (Enter/Esc)",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        rc.left + 50, rc.top + 50, 320, 200, owner, nullptr, GetModuleHandle(0), nullptr);

    if (!hDlg) return false;

    EnableWindow(owner, FALSE);
    MSG msg; while (IsWindow(hDlg) && GetMessageW(&msg, 0, 0, 0)) {
        if (msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_RETURN) { SendMessageW(hDlg, WM_COMMAND, IDOK, 0); continue; }
            else if (msg.wParam == VK_ESCAPE) { SendMessageW(hDlg, WM_COMMAND, IDCANCEL, 0); continue; }
        }
        if (!IsDialogMessageW(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(owner, TRUE); SetActiveWindow(owner);
    if (g_memo.hwndEdit) SetFocus(g_memo.hwndEdit); // 창 닫힌 후 메모장으로 완벽히 포커스 복구
    return true;
}

static void DrawMemoFormatMarks(HWND hwnd)
{
    if (!g_memo.showFormatMarks)
        return;

    ScopedWindowDC dc(hwnd);
    if (!dc)
        return;

    HDC hdc = dc.Get();
    HFONT oldFont = nullptr;
    if (g_memo.hFont)
        oldFont = (HFONT)SelectObject(hdc, g_memo.hFont);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(160, 160, 160));

    int firstLine = (int)SendMessageW(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0);
    int lineCount = (int)SendMessageW(hwnd, EM_GETLINECOUNT, 0, 0);

    RECT rcClient{};
    GetClientRect(hwnd, &rcClient);

    for (int line = firstLine; line < lineCount; ++line)
    {
        LONG lineStart = (LONG)SendMessageW(hwnd, EM_LINEINDEX, line, 0);
        if (lineStart < 0)
            break;

        int len = (int)SendMessageW(hwnd, EM_LINELENGTH, lineStart, 0);

        std::wstring text;
        if (len > 0)
        {
            text.resize(len, L'\0');
            TEXTRANGEW tr{};
            tr.chrg.cpMin = lineStart;
            tr.chrg.cpMax = lineStart + len;
            tr.lpstrText = &text[0];
            SendMessageW(hwnd, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
        }

        POINTL ptEnd{};
        SendMessageW(hwnd, EM_POSFROMCHAR, (WPARAM)&ptEnd, lineStart + len);
        if (ptEnd.y > rcClient.bottom)
            break;

        for (int i = 0; i < (int)text.size(); ++i)
        {
            wchar_t ch = text[i];

            if (ch != L' ' && ch != L'\t')
                continue;

            POINTL pt{};
            SendMessageW(hwnd, EM_POSFROMCHAR, (WPARAM)&pt, lineStart + i);

            POINTL ptNext{};
            SendMessageW(hwnd, EM_POSFROMCHAR, (WPARAM)&ptNext, lineStart + i + 1);

            int cellWidth = ptNext.x - pt.x;
            if (cellWidth <= 0)
                cellWidth = 6;

            int step = max(3, cellWidth / 3);

            // ------------------------
            // SPACE → 점으로 채우기
            // ------------------------
            if (ch == L' ')
            {
                for (int x = pt.x; x < pt.x + cellWidth; x += step)
                {
                    TextOutW(hdc, x, pt.y, L"·", 1);
                }
            }

            // ------------------------
            // TAB → 화살표 + 점
            // ------------------------
            else if (ch == L'\t')
            {
                // 시작 화살표
                TextOutW(hdc, pt.x, pt.y, L"→", 1);

                // 나머지 영역 점으로 채움
                for (int x = pt.x + step; x < pt.x + cellWidth; x += step)
                {
                    TextOutW(hdc, x, pt.y, L"·", 1);
                }
            }
        }

        TextOutW(hdc, ptEnd.x, ptEnd.y, L"¶", 1);
    }

    if (oldFont)
        SelectObject(hdc, oldFont);
}


// ==============================================
// 메모장 단축키 처리
// 메뉴에 "\tCtrl+..." 라고 표시하는 것만으로는 RichEdit 포커스 상태에서
// 실제 단축키가 동작하지 않는다. RichEdit가 키를 먼저 먹기 때문에 여기서
// 메모장 메뉴 명령으로 직접 변환한다.
// ==============================================
static bool HandleMemoShortcutKey(UINT msg, WPARAM wParam)
{
    if (!g_memo.hwnd || !IsWindow(g_memo.hwnd))
        return false;

    bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
    bool alt   = (GetKeyState(VK_MENU)    & 0x8000) != 0;

    auto DoCmd = [](int id) -> bool {
        SendMessageW(g_memo.hwnd, WM_COMMAND, id, 0);
        return true;
    };

    // Alt 계열은 WM_SYSKEYDOWN으로 들어온다.
    if (msg == WM_SYSKEYDOWN && alt && !ctrl)
    {
        switch ((int)wParam)
        {
        case 'G': return DoCmd(ID_MEMO_EXIT_TO_TINTIN);
        case 'D': return DoCmd(ID_MEMO_DRAW_TOGGLE);
        case 'Y': return DoCmd(ID_MEMO_EDIT_DEL_END);
        }
    }

    if (msg != WM_KEYDOWN)
        return false;

    // 단독 기능키
    if (!ctrl && !alt && !shift)
    {
        switch ((int)wParam)
        {
        case VK_F4:     return DoCmd(ID_MENU_VIEW_SYMBOLS);
        case VK_F3:     return DoCmd(ID_MEMO_EDIT_FIND_NEXT);
        case VK_ESCAPE: return DoCmd(ID_MEMO_FILE_EXIT);
        case VK_DELETE: return DoCmd(ID_MEMO_EDIT_DELETE);
        }
    }

    if (!ctrl && !alt && shift)
    {
        if (wParam == VK_F3) return DoCmd(ID_MEMO_EDIT_FIND_PREV);
    }

    // Ctrl 조합
    if (ctrl && !alt)
    {
        if (!shift)
        {
            switch ((int)wParam)
            {
            case 'O': return DoCmd(ID_MEMO_FILE_OPEN);
            case 'S': return DoCmd(ID_MEMO_FILE_SAVE);
            case 'Z': return DoCmd(ID_MEMO_EDIT_UNDO);
            case 'Y': return DoCmd(ID_MEMO_EDIT_REDO);
            case 'X': return DoCmd(ID_MEMO_EDIT_CUT);
            case 'C': return DoCmd(ID_MEMO_EDIT_COPY);
            case 'V': return DoCmd(ID_MEMO_EDIT_PASTE);
            case 'A': return DoCmd(ID_MEMO_EDIT_SELECTALL);
            case 'F': return DoCmd(ID_MEMO_EDIT_FIND);
            case 'H': return DoCmd(ID_MEMO_EDIT_REPLACE);
            case 'G': return DoCmd(ID_MEMO_EDIT_GOTO);
            case 'R': return DoCmd(ID_MEMO_REPEAT_SYMBOL);
            case 'L': return DoCmd(ID_MEMO_EDIT_DEL_LINE);
            case VK_HOME:   return DoCmd(ID_MEMO_EDIT_DOC_START);
            case VK_END:    return DoCmd(ID_MEMO_EDIT_DOC_END);
            case VK_PRIOR:  return DoCmd(ID_MEMO_EDIT_SCR_START);
            case VK_NEXT:   return DoCmd(ID_MEMO_EDIT_SCR_END);
            case VK_BACK:   return DoCmd(ID_MEMO_EDIT_DEL_WORD_LEFT);
            case VK_DELETE: return DoCmd(ID_MEMO_EDIT_DEL_WORD_RIGHT);
            }
        }
        else
        {
            if (wParam == 'S') return DoCmd(ID_MEMO_FILE_SAVEAS);
        }
    }

    return false;
}

static LRESULT CALLBACK MemoEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    (void)uIdSubclass;
    (void)dwRefData;

    if (HandleMemoShortcutKey(msg, wParam))
        return 0;

    // 그리기 모드에서는 RichEdit가 방향키를 먼저 처리하기 전에 가로채야 합니다.
    // 이전 안전판에서는 DefSubclassProc()를 먼저 호출해서 커서만 이동하고
    // MemoDrawStep()/MemoBrushStep()가 실행되지 않았습니다.
    if (msg == WM_KEYDOWN && g_memo.drawMode)
    {
        int dx = 0;
        int dy = 0;

        switch (wParam)
        {
        case VK_LEFT:  dx = -1; break;
        case VK_RIGHT: dx =  1; break;
        case VK_UP:    dy = -1; break;
        case VK_DOWN:  dy =  1; break;
        default:
            break;
        }

        if (dx != 0 || dy != 0)
        {
            if (MemoIsLineBrush(g_memo.selectedSymbol))
                MemoDrawStep(hwnd, dx, dy);
            else
                MemoBrushStep(hwnd, dx, dy);

            return 0;
        }
    }

    // 메모장 편집창에 포커스가 있을 때도 Alt+D로 그리기 모드를 켜고 끌 수 있게 합니다.
    if (msg == WM_SYSKEYDOWN && wParam == L'D')
    {
        g_memo.drawMode = !g_memo.drawMode;
        if (g_memo.hwnd)
            UpdateMemoMenuState(g_memo.hwnd);
        UpdateMemoStatus();
        return 0;
    }

    LRESULT lr = DefSubclassProc(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_PAINT:
        DrawMemoFormatMarks(hwnd);
        return lr;

    case WM_VSCROLL:
    case WM_HSCROLL:
    case WM_MOUSEWHEEL:
    case WM_SIZE:
    case WM_SETTEXT:
        if (g_memo.showFormatMarks)
            InvalidateRect(hwnd, nullptr, FALSE);
        return lr;
    }

    return lr;
}

static bool MemoOpenFile(HWND hwnd, const std::wstring& path)
{
    std::ifstream ifs(path.c_str(), std::ios::binary);
    if (!ifs) return false;

    // 1. 파일 데이터 통째로 읽기
    std::string bytes((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    // ★ 로딩 중 플래그 ON
    g_memo.loadingFile = true;

    if (bytes.empty()) {
        DWORD oldEventMask = (DWORD)SendMessageW(g_memo.hwndEdit, EM_GETEVENTMASK, 0, 0);
        SendMessageW(g_memo.hwndEdit, WM_SETREDRAW, FALSE, 0);
        SendMessageW(g_memo.hwndEdit, EM_SETEVENTMASK, 0, 0);

        SetWindowTextW(g_memo.hwndEdit, L"");

        SendMessageW(g_memo.hwndEdit, EM_SETEVENTMASK, 0, oldEventMask);
        SendMessageW(g_memo.hwndEdit, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(g_memo.hwndEdit, nullptr, TRUE);

        g_memo.currentPath = path;
        g_memo.encodingType = 0; // 새 파일은 기본 UTF-8
        MarkMemoDirty(false);
        UpdateMemoTitle();
        UpdateMemoStatus();

        g_memo.loadingFile = false;
        return true;
    }

    std::wstring text;

    // 2. [인코딩 감지] 우선 BOM이 있는지 확인 (UTF-8 BOM: EF BB BF)
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF &&
        (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF)
    {
        text = MultiByteToWide(bytes.substr(3), CP_UTF8);
        g_memo.encodingType = 0; // UTF-8
    }
    else
    {
        // 3. [인코딩 감지] BOM이 없다면 UTF-8인지 엄격하게 테스트
        int testLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            bytes.data(), (int)bytes.size(), nullptr, 0);

        if (testLen > 0) {
            text.resize(testLen);
            MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(),
                &text[0], testLen);
            g_memo.encodingType = 0; // UTF-8
        }
        else {
            text = MultiByteToWide(bytes, 949);
            g_memo.encodingType = 1; // CP949
        }
    }

    // ★ 에디터 전체 갱신 묶기
    DWORD oldEventMask = (DWORD)SendMessageW(g_memo.hwndEdit, EM_GETEVENTMASK, 0, 0);
    SendMessageW(g_memo.hwndEdit, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_memo.hwndEdit, EM_SETEVENTMASK, 0, 0);

    s_lastMemoHighlightSig = 0;

    // 4. 에디터에 텍스트 채우기
    SetWindowTextW(g_memo.hwndEdit, text.c_str());
    g_memo.currentPath = path;
    MarkMemoDirty(false);
    MemoPushRecentFile(path);

    // 5. [구문 강조] 확장자별 다중 언어 자동 감지
    int detectedLang = GetSyntaxLanguageFromPath(path);

    // 확장자로 감지되면 자동으로 그 언어 선택
    if (detectedLang > 0)
        g_memo.syntaxLang = detectedLang;

    UpdateMemoMenuState(hwnd);

    // 구문강조가 켜져 있고, 언어가 잡혀 있으면 자동 적용
    if (g_memo.useSyntax && g_memo.syntaxLang > 0) {
        LoadMemoUserKeywords();
        ApplyMemoSyntaxHighlight(g_memo.hwndEdit);
    }

    // 파일 내용 삽입은 끝났으므로 이제 로딩 플래그 해제
    g_memo.loadingFile = false;

    if (g_memo.useSyntax && g_memo.syntaxLang > 0) {
        LoadMemoUserKeywords();
        ApplyMemoSyntaxHighlight(g_memo.hwndEdit);
    }

    SendMessageW(g_memo.hwndEdit, EM_SETEVENTMASK, 0, oldEventMask);
    SendMessageW(g_memo.hwndEdit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_memo.hwndEdit, nullptr, TRUE);

    // 6. UI 갱신
    UpdateMemoTitle();
    UpdateMemoStatus();

    return true;
}

static bool MemoSaveFile(HWND hwnd, const std::wstring& path)
{
    (void)hwnd;

    std::wstring text = GetWindowTextString(g_memo.hwndEdit);
    bool saved = false;

    if (g_memo.encodingType == 0) { // UTF-8
        saved = WriteUtf8BomTextFile(path, WideToMultiByte(text, CP_UTF8));
    }
    else { // CP949 (EUC-KR)
        std::string ansi = WideToMultiByte(text, 949);
        saved = WriteStringToFile(path, ansi);
    }

    if (!saved)
        return false;

    g_memo.currentPath = path;
    MarkMemoDirty(false);
    MemoPushRecentFile(path);
    UpdateMemoTitle();
    UpdateMemoStatus();
    return true;
}

static void UpdateMemoTitle()
{
    if (!g_memo.hwnd) return;

    std::wstring title = L"메모장";
    if (!g_memo.currentPath.empty())
    {
        title += L" - ";
        title += g_memo.currentPath;
    }
    else
    {
        title += L" - 새 문서";
    }

    if (g_memo.dirty)
        title += L" *";

    SetWindowTextW(g_memo.hwnd, title.c_str());
}

void UpdateMemoStatus()
{
    // 이미 상단에서 hwndStatus 라는 이름을 사용 중입니다.
    if (!g_memo.hwndStatus || !g_memo.hwndEdit) return;

    int line = 0, col = 0;
    MemoGetCaretGrid(g_memo.hwndEdit, line, col);

    int parts[6] = { 100, 160, 420, 520, 620, -1 };
    SendMessageW(g_memo.hwndStatus, SB_SETPARTS, 6, (LPARAM)parts);

    wchar_t buf1[64], buf2[32], buf3[256], buf4[64], buf5[64], encBuf[32];

    wsprintfW(encBuf, L"\t%s", g_memo.encodingType == 0 ? L"UTF-8" : L"CP949");
    wsprintfW(buf1, L"\t%d 줄\t%d 칸", line + 1, col + 1);
    wsprintfW(buf2, L"\t%s", g_memo.insertMode ? L"삽입" : L"수정");
    wsprintfW(buf3, L"\t그리기: %s  |  열모드: %s  |  자동저장: %s",
        g_memo.drawMode ? L"ON" : L"OFF",
        g_memo.columnMode ? L"ON" : L"OFF",
        g_memo.autoSave ? L"ON" : L"OFF");
    wsprintfW(buf4, L"\t현재 선: %c", g_memo.selectedSymbol ? g_memo.selectedSymbol : L'-');

    if (g_memo.lastSymbol.empty()) {
        wsprintfW(buf5, L"\t기호: 없음");
    }
    else {
        wsprintfW(buf5, L"\t기호: %s", g_memo.lastSymbol.c_str());
    }

    SendMessageW(g_memo.hwndStatus, SB_SETTEXTW, 0, (LPARAM)buf1);
    SendMessageW(g_memo.hwndStatus, SB_SETTEXTW, 1, (LPARAM)buf2);
    SendMessageW(g_memo.hwndStatus, SB_SETTEXTW, 2, (LPARAM)buf3);
    SendMessageW(g_memo.hwndStatus, SB_SETTEXTW, 3, (LPARAM)encBuf);
    SendMessageW(g_memo.hwndStatus, SB_SETTEXTW, 4, (LPARAM)buf4);
    SendMessageW(g_memo.hwndStatus, SB_SETTEXTW, 5, (LPARAM)buf5);

    // ★ 에러 해결: hwndStatusBar를 hwndStatus로 변경
    if (g_memo.hwndStatus) {
        // 마지막 인자를 TRUE로 주어 배경을 싹 지우고 다시 그리게 합니다.
        InvalidateRect(g_memo.hwndStatus, NULL, TRUE);
        // 즉시 반영되도록 UpdateWindow 호출 (선택 사항)
        UpdateWindow(g_memo.hwndStatus);
    }
}

static void MarkMemoDirty(bool dirty)
{
    g_memo.dirty = dirty;
    UpdateMemoTitle();
}

static void MemoRebuildRecentMenu(HWND hwnd)
{
    HMENU hMenuBar = GetMenu(hwnd);
    if (!hMenuBar) return;
    HMENU hFile = GetSubMenu(hMenuBar, 0);
    if (!hFile) return;

    // ★ 최근 파일 문자열을 안전하게 보관할 정적 배열
    static std::wstring s_recentItems[5];

    for (int i = 0; i < 5; ++i) DeleteMenu(hFile, ID_MEMO_RECENT_BASE + i, MF_BYCOMMAND);
    DeleteMenu(hFile, ID_MEMO_RECENT_BASE + 10, MF_BYCOMMAND);

    if (!g_memo.recentFiles.empty()) {
        AppendMenuW(hFile, MF_SEPARATOR, ID_MEMO_RECENT_BASE + 10, nullptr);
        for (int i = 0; i < (int)g_memo.recentFiles.size() && i < 5; ++i) {
            // 	 를 넣으면 Windows 메뉴가 왼쪽/오른쪽 단축키 영역처럼 나누어 그리므로
            // "1" 다음에 파일명이 멀리 떨어져 보인다. 최근 파일은 일반 문자열로 붙여서 표시한다.
            s_recentItems[i] = L"&" + std::to_wstring(i + 1) + L". " + g_memo.recentFiles[i];
            AppendMenuW(hFile, MF_STRING, ID_MEMO_RECENT_BASE + i, s_recentItems[i].c_str());
        }
    }
    DrawMenuBar(hwnd);
}

static unsigned long long HashMemoTextFast(const std::wstring& text)
{
    // FNV-1a 64bit
    unsigned long long h = 1469598103934665603ULL;
    for (wchar_t ch : text)
    {
        h ^= (unsigned long long)(unsigned short)ch;
        h *= 1099511628211ULL;
    }
    return h;
}

static unsigned long long BuildMemoHighlightSignature(const std::wstring& text)
{
    unsigned long long h = HashMemoTextFast(text);

    // 구문강조 결과에 영향을 주는 상태도 함께 반영
    h ^= (unsigned long long)g_memo.syntaxLang + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= (unsigned long long)g_memo.useSyntax + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= (unsigned long long)g_memo.textColor + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= (unsigned long long)g_memo.font.lfHeight + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= (unsigned long long)g_memo.font.lfWeight + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);

    for (int i = 0; g_memo.font.lfFaceName[i] != 0; ++i)
    {
        h ^= (unsigned long long)(unsigned short)g_memo.font.lfFaceName[i];
        h *= 1099511628211ULL;
    }

    return h;
}

static void ApplyMemoFontAndFormat()
{
    if (!g_memo.hwndEdit) return;

    // 일반 설정의 "폰트 부드럽게 표시" 값을 읽어옵니다.
    g_memo.font.lfQuality = GetCurrentFontQuality();
    g_memo.font.lfCharSet = HANGEUL_CHARSET; // 한글 깨짐 방지
    g_memo.font.lfPitchAndFamily = FIXED_PITCH | FF_MODERN; // 고정폭 강제

    ResetGdiObjectRef(g_memo.hFont);
    g_memo.hFont = CreateFontIndirectW(&g_memo.font);

    SendMessageW(g_memo.hwndEdit, WM_SETFONT, (WPARAM)g_memo.hFont, TRUE);
    SendMessageW(g_memo.hwndEdit, EM_SETBKGNDCOLOR, 0, g_memo.backColor);

    // 서식 강제 주입
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_CHARSET;
    cf.crTextColor = g_memo.textColor;
    cf.bCharSet = HANGEUL_CHARSET;
    lstrcpynW(cf.szFaceName, g_memo.font.lfFaceName, LF_FACESIZE);

    int ptSize = GetFontPointSizeFromLogFont(g_memo.font);
    if (ptSize < 8) ptSize = 12;
    cf.yHeight = ptSize * 20;
    cf.dwEffects = (g_memo.font.lfWeight >= FW_BOLD) ? CFE_BOLD : 0;

    SendMessageW(g_memo.hwndEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    SendMessageW(g_memo.hwndEdit, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);

    ApplyMemoWrapWidth();
    ApplyMemoLineSpacing();
    InvalidateRect(g_memo.hwndEdit, nullptr, TRUE);
}

static void ApplyMemoSyntaxHighlight(HWND hwndEdit)
{
    if (!hwndEdit) return;
    if (!g_memo.useSyntax) return;
    if (g_memo.loadingFile) return; // MemoOpenFile 바깥에서 중간 호출 방지

    if (g_memo.hwnd)
    {
        ShowMemoBusyPopup(g_memo.hwnd, L"구문 강조 중...");
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    GETTEXTLENGTHEX gtl = { GTL_DEFAULT, 1200 };
    int len = (int)SendMessageW(hwndEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    if (len <= 0) return;

    std::wstring text(len + 1, L'\0');
    GETTEXTEX gt = { (DWORD)((len + 1) * sizeof(wchar_t)), GT_DEFAULT, 1200, nullptr, nullptr };
    SendMessageW(hwndEdit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)&text[0]);
    text.resize(len);

    // ★ 같은 문서/같은 서식 상태면 전체 재강조 생략
    unsigned long long sig = BuildMemoHighlightSignature(text);
    if (sig == s_lastMemoHighlightSig)
        return;

    CHARRANGE oldSel{};
    SendMessageW(hwndEdit, EM_EXGETSEL, 0, (LPARAM)&oldSel);
    DWORD oldEventMask = (DWORD)SendMessageW(hwndEdit, EM_GETEVENTMASK, 0, 0);
    SendMessageW(hwndEdit, WM_SETREDRAW, FALSE, 0);
    SendMessageW(hwndEdit, EM_SETEVENTMASK, 0, 0);

    CHARFORMAT2W cfBase{};
    cfBase.cbSize = sizeof(cfBase);
    cfBase.dwMask = CFM_COLOR | CFM_BOLD;
    cfBase.crTextColor = g_memo.textColor;
    cfBase.dwEffects = 0;
    SendMessageW(hwndEdit, EM_SETSEL, 0, -1);
    SendMessageW(hwndEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfBase);

    auto ApplyColorRange = [&](LONG start, LONG end, COLORREF color) {
        if (start < 0 || end <= start) return;
        CHARRANGE cr{ start, end };
        SendMessageW(hwndEdit, EM_EXSETSEL, 0, (LPARAM)&cr);

        CHARFORMAT2W cf{};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR | CFM_BOLD;
        cf.crTextColor = color;
        cf.dwEffects = 0; // 굵게 무조건 OFF
        SendMessageW(hwndEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        };

    // ★ 9종 테마별 컬러 팔레트 (문자열 색상 clrString 추가!)
    COLORREF clrCommand, clrComment, clrVar, clrBrace, clrSeparator, clrArraySep, clrString;

    switch (g_memo.syntaxTheme) {
    case 1: // 울트라에디터 (Light)
        clrCommand = RGB(0, 0, 255); clrComment = RGB(0, 128, 0); clrVar = RGB(255, 0, 0);
        clrBrace = RGB(0, 0, 128); clrSeparator = RGB(204, 0, 204); clrArraySep = RGB(128, 128, 0); clrString = RGB(170, 0, 170); break;
    case 2: // 비주얼 스튜디오 (VS Code - Dark)
        clrCommand = RGB(86, 156, 214); clrComment = RGB(106, 153, 85); clrVar = RGB(156, 220, 254);
        clrBrace = RGB(255, 215, 0); clrSeparator = RGB(197, 134, 192); clrArraySep = RGB(215, 186, 125); clrString = RGB(206, 145, 120); break;
    case 3: // 모노카이 (Monokai - Dark)
        clrCommand = RGB(249, 38, 114); clrComment = RGB(117, 113, 94); clrVar = RGB(253, 151, 31);
        clrBrace = RGB(248, 248, 242); clrSeparator = RGB(102, 217, 239); clrArraySep = RGB(166, 226, 46); clrString = RGB(230, 219, 116); break;
    case 4: // 드라큘라 (Dracula - Dark)
        clrCommand = RGB(255, 121, 198); clrComment = RGB(98, 114, 164); clrVar = RGB(80, 250, 123);
        clrBrace = RGB(248, 248, 242); clrSeparator = RGB(139, 233, 253); clrArraySep = RGB(255, 184, 108); clrString = RGB(241, 250, 140); break;
    case 5: // 기본 다크 (Default Dark)
        clrCommand = RGB(86, 156, 214); clrComment = RGB(87, 166, 74); clrVar = RGB(255, 100, 100);
        clrBrace = RGB(255, 215, 0); clrSeparator = RGB(200, 200, 200); clrArraySep = RGB(255, 128, 0); clrString = RGB(214, 157, 133); break;
    case 6: // 솔라라이즈드 라이트
        clrCommand = RGB(38, 139, 210); clrComment = RGB(147, 161, 161); clrVar = RGB(220, 50, 47);
        clrBrace = RGB(181, 137, 0); clrSeparator = RGB(101, 123, 131); clrArraySep = RGB(203, 75, 22); clrString = RGB(42, 161, 152); break;
    case 7: // 솔라라이즈드 다크
        clrCommand = RGB(38, 139, 210); clrComment = RGB(88, 110, 117); clrVar = RGB(220, 50, 47);
        clrBrace = RGB(181, 137, 0); clrSeparator = RGB(131, 148, 150); clrArraySep = RGB(203, 75, 22); clrString = RGB(42, 161, 152); break;
    case 8: // 깃허브 다크
        clrCommand = RGB(121, 192, 255); clrComment = RGB(139, 148, 158); clrVar = RGB(255, 123, 114);
        clrBrace = RGB(210, 168, 255); clrSeparator = RGB(201, 209, 217); clrArraySep = RGB(255, 166, 87); clrString = RGB(165, 214, 255); break;
    default: // 0: 기본 라이트
        clrCommand = RGB(0, 0, 205); clrComment = RGB(34, 139, 34); clrVar = RGB(220, 20, 60);
        clrBrace = RGB(139, 0, 139); clrSeparator = RGB(255, 140, 0); clrArraySep = RGB(0, 128, 128); clrString = RGB(163, 21, 21); break;
    }

    int lang = g_memo.syntaxLang; // 1: TinTin, 2: C/C++, 3: Shell
    int i = 0;

    while (i < len)
    {
        wchar_t ch = text[i];

        // [C/C++] 블록 주석 /* ... */
        if (lang == 2 && ch == L'/' && i + 1 < len && text[i + 1] == L'*') {
            int start = i;
            i += 2;
            while (i < len && !(text[i - 1] == L'*' && text[i] == L'/')) i++;
            if (i < len) i++;
            ApplyColorRange(start, i, clrComment);
            continue;
        }

        // [TinTin, C/C++] 한줄 주석 (//) 또는 배열 기호
        if ((lang == 1 || lang == 2) && ch == L'/' && i + 1 < len && text[i + 1] == L'/') {
            int start = i;
            if (lang == 1) { // TinTin은 배열 구분자로 사용
                ApplyColorRange(start, start + 2, clrArraySep);
                i += 2;
            }
            else { // C/C++은 한 줄 주석
                while (i < len && text[i] != L'\n' && text[i] != L'\r') i++;
                ApplyColorRange(start, i, clrComment);
            }
            continue;
        }

        // [Shell] 한줄 주석 (#)
        if (lang == 3 && ch == L'#') {
            int start = i;
            while (i < len && text[i] != L'\n' && text[i] != L'\r') i++;
            ApplyColorRange(start, i, clrComment);
            continue;
        }

        // [공통] 문자열 "..." 또는 '...' 처리
        if ((lang == 2 || lang == 3) && (ch == L'"' || ch == L'\'')) {
            wchar_t quote = ch;
            int start = i;
            i++;
            while (i < len && text[i] != quote && text[i] != L'\n' && text[i] != L'\r') {
                if (text[i] == L'\\' && i + 1 < len) i += 2; // 이스케이프 문자 무시
                else i++;
            }
            if (i < len && text[i] == quote) i++;
            ApplyColorRange(start, i, clrString);
            continue;
        }

        // [TinTin, C/C++] 전처리기 및 명령어 (#명령)
        if ((lang == 1 || lang == 2) && ch == L'#') {
            int start = i;
            int j = i + 1;
            while (j < len && IsTintinCommandChar(text[j])) j++;
            if (j > i + 1) {
                // TinTin의 #nop 처리
                if (lang == 1 && MemoRangeEquals(text, i + 1, j, L"nop", true)) {
                    int depth = 0;
                    while (j < len) {
                        if (text[j] == L'{') depth++;
                        else if (text[j] == L'}') { if (depth == 0) break; depth--; }
                        else if (depth <= 0 && (text[j] == L';' || text[j] == L'\n' || text[j] == L'\r')) break;
                        j++;
                    }
                    ApplyColorRange(start, j, clrComment);
                    i = j;
                    continue;
                }
                else {
                    ApplyColorRange(start, j, clrCommand);
                    i = j;
                    continue;
                }
            }
        }

        // [공통] 괄호 및 세퍼레이터
        if (ch == L'{' || ch == L'}') {
            ApplyColorRange(i, i + 1, clrBrace); i++; continue;
        }
        if (ch == L';' || (lang != 1 && (ch == L'(' || ch == L')' || ch == L'[' || ch == L']'))) {
            ApplyColorRange(i, i + 1, clrSeparator); i++; continue;
        }

        // [공통] 변수 매칭 ($var, %1)
        if (ch == L'$' || (lang == 1 && ch == L'%')) {
            int start = i;
            int j = i + 1;
            if (ch == L'%') {
                while (j < len && iswdigit(text[j])) j++;
            }
            else {
                if (lang == 3 && j < len && text[j] == L'{') { // Shell ${var}
                    while (j < len && text[j] != L'}') j++;
                    if (j < len) j++;
                }
                else {
                    while (j < len && (iswalnum(text[j]) || text[j] == L'_')) j++;
                }
            }
            if (j > start + 1) {
                ApplyColorRange(start, j, clrVar);
                i = j;
                continue;
            }
        }

        // [공통] 키워드 매칭 (TinTin: else/elseif, C/C++/Shell: 기본 키워드)
        if (IsTintinCommandChar(ch)) {
            int start = i;
            while (i < len && (IsTintinCommandChar(text[i]) || iswdigit(text[i]))) i++;

            if (lang == 1) {
                if (MemoRangeEquals(text, start, i, L"else", true) ||
                    MemoRangeEquals(text, start, i, L"elseif", true)) {
                    ApplyColorRange(start, i, clrCommand);
                }
            }
            else if (lang == 2) {
                if (IsCppKeywordRange(text, start, i)) ApplyColorRange(start, i, clrCommand);
            }
            else if (lang == 3) {
                if (IsShellKeywordRange(text, start, i)) ApplyColorRange(start, i, clrCommand);
            }
            continue;
        }

        i++;
    }

    s_lastMemoHighlightSig = sig;

    SendMessageW(hwndEdit, EM_SETSEL, oldSel.cpMin, oldSel.cpMax);
    SendMessageW(hwndEdit, EM_SETEVENTMASK, 0, oldEventMask);
    SendMessageW(hwndEdit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hwndEdit, NULL, FALSE);

    HideMemoBusyPopup();
}

// ==============================================
// 외부에서 호출되는 진입점
// ==============================================
void OpenMemoWindow(HWND owner)
{
    (void)owner;

    EnsureRichEditLoaded();
    LoadMemoWindowSettings();

    if (g_memo.hwnd && IsWindow(g_memo.hwnd))
    {
        SetWindowPos(g_memo.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(g_memo.hwnd);
        SetFocus(g_memo.hwndEdit);
        return;
    }

    static bool clsRegistered = false;
    if (!clsRegistered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = MemoWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"TTMemoWindowClass";
        wc.hCursor = LoadCursor(nullptr, IDC_IBEAM);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        clsRegistered = true;
    }

    int finalX = (g_memo.x == -1) ? CW_USEDEFAULT : g_memo.x;
    int finalY = (g_memo.y == -1) ? CW_USEDEFAULT : g_memo.y;

    // 0 대신 WS_EX_TOPMOST를 상황에 따라 쓸 수 있으나, 
    // 여기서는 기본적으로 부모 창과의 관계를 위해 CreateWindowExW의 스타일을 점검합니다.
    HWND hwnd = CreateWindowExW(
        0,
        L"TTMemoWindowClass",
        L"메모장",
        WS_OVERLAPPEDWINDOW, // WS_VISIBLE을 여기서 빼고 아래서 ShowWindow로 호출하는 것이 안정적입니다.
        finalX, finalY, g_memo.w, g_memo.h,
        nullptr,
        nullptr,
        GetModuleHandleW(0),
        nullptr);

    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd); // 새 창을 맨 앞으로
    }
}
