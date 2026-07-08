#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct MemoState
{
    HWND hwnd = nullptr;
    HWND hwndEdit = nullptr;
    HWND hwndStatus = nullptr;
    HWND hwndLineNum = nullptr;

    bool drawMode = false;
    bool columnMode = false;
    bool autoSave = true;
    int autoSaveIntervalSec = 30;
    bool dirty = false;

    std::wstring currentPath;
    std::wstring autoSavePath;
    std::vector<std::wstring> recentFiles;

    wchar_t selectedSymbol = L'─';
    std::wstring lastSymbol;

    int caretLine = 0;
    int caretCol = 0;
    int drawLine = 0;
    int drawCol = 0;
    bool drawPosValid = false;

    COLORREF textColor = RGB(0, 0, 0);
    COLORREF backColor = RGB(255, 255, 255);

    LOGFONTW font = {};
    HFONT hFont = nullptr;

    bool insertMode = true;
    int wrapCols = 0;
    int encodingType = 0;
    bool showLineNumbers = false;

    bool useSyntax = false;
    int syntaxTheme = 0;
    int syntaxLang = 0;
    std::vector<std::wstring> userKeywords;

    // 창 위치/크기 기억
    int x = -1, y = -1, w = 900, h = 700;
    bool loadingFile = false;

    bool showFormatMarks = false;
};


enum
{
    BOX_U = 1,
    BOX_R = 2,
    BOX_D = 4,
    BOX_L = 8
};

struct LineSet {
    wchar_t h, v, tl, tr, bl, br, ml, mr, mt, mb, c;
    const wchar_t* display;
};

// 외부에서 접근할 전역 변수 및 함수 선언
extern MemoState g_memo;
extern MemoFindState g_memoFind;
extern int g_currentLineSetIdx;

extern const int g_lineSetsCount;

// 주요 함수 선언
void OpenMemoWindow(HWND owner);

#ifndef KTIN_MEMO_LOCAL_IMPL
bool MemoOpenFile(HWND hwnd, const std::wstring& path);
bool MemoSaveFile(HWND hwnd, const std::wstring& path);

void UpdateMemoTitle();
void UpdateMemoStatus();
void MarkMemoDirty(bool dirty);

void ApplyMemoFontAndFormat();
void ApplyMemoSyntaxHighlight(HWND hwndEdit);
void SetMemoThemeBaseColors(int themeIdx);

LRESULT CALLBACK MemoWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK MemoEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif
