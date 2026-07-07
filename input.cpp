#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "input.h"
#include "shortcut_bar.h"

#include "resource.h"
#include "settings.h"
#include "shortcut_bar.h"
#include <commctrl.h>

// ==============================================
// 입력창 레이아웃 및 메트릭스
// ==============================================
void LayoutInputEdits()
{
    if (!g_app || !g_app->hwndInput)
        return;

    RECT rc{};
    GetClientRect(g_app->hwndInput, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0)
        return;

    const int leftPad = 8;
    const int rightPad = 8;
    const int topPad = INPUT_SEPARATOR_HEIGHT + 2;
    const int rowGap = 1;

    int editX = leftPad;
    int editW = width - leftPad - rightPad;
    if (editW < 40) editW = 40;

    int rowHeight = g_app->inputLineHeight;
    if (rowHeight < 18) rowHeight = 18;

    for (int i = 0; i < INPUT_ROWS; ++i)
    {
        if (g_app->hwndEdit[i])
        {
            int y = topPad + i * (rowHeight + rowGap);
            MoveWindow(g_app->hwndEdit[i], editX, y, editW, rowHeight, TRUE);
        }
    }
}

void RecalcInputMetrics()
{
    if (!g_app || !g_app->hwndInput)
        return;

    HDC hdc = GetDC(g_app->hwndInput);
    if (!hdc) return;

    HFONT oldFont = nullptr;
    if (g_app->hFontInput)
        oldFont = (HFONT)SelectObject(hdc, g_app->hFontInput);

    TEXTMETRICW tm = {};
    GetTextMetricsW(hdc, &tm);

    if (oldFont)
        SelectObject(hdc, oldFont);
    ReleaseDC(g_app->hwndInput, hdc);

    g_app->inputLineHeight = tm.tmHeight + tm.tmExternalLeading;
    if (g_app->inputLineHeight < 17)
        g_app->inputLineHeight = 17;

    g_app->inputTextOffsetX = 0;
    g_app->inputTextOffsetY = 0;

    g_app->inputAreaHeight = INPUT_SEPARATOR_HEIGHT +
        (g_app->inputLineHeight * INPUT_ROWS);
}

int GetInputAreaHeight()
{
    if (!g_app) return 90;

    const int topPadding = INPUT_SEPARATOR_HEIGHT + 2;
    const int bottomPadding = 2;
    const int rowGap = 1;

    int rowHeight = g_app->inputLineHeight;
    if (rowHeight < 18) rowHeight = 18;

    return topPadding + bottomPadding +
           (rowHeight * INPUT_ROWS) + (rowGap * (INPUT_ROWS - 1));
}

// ==============================================
// 입력 기록 보기 관리
// ==============================================
void ApplyInputView()
{
    if (!g_app) return;

    g_app->internalEditUpdate = true;

    for (int i = 0; i < INPUT_ROWS; ++i)
    {
        std::wstring text;
        int idx = g_app->displayedHistoryIndex[i];
        if (idx >= 0 && idx < (int)g_app->history.size())
            text = g_app->history[idx];

        if (g_app->hwndEdit[i])
            SetWindowTextW(g_app->hwndEdit[i], text.c_str());
    }

    g_app->internalEditUpdate = false;
}

void SetInputViewLatest()
{
    if (!g_app) return;

    for (int i = 0; i < INPUT_ROWS; ++i)
        g_app->displayedHistoryIndex[i] = -1;

    int n = (int)g_app->history.size();
    if (n <= 0)
    {
        ApplyInputView();
        FocusInputRow(0);
        return;
    }

    if (n == 1)
    {
        g_app->displayedHistoryIndex[0] = 0;
        ApplyInputView();
        FocusInputRow(1);
        return;
    }

    g_app->displayedHistoryIndex[0] = n - 2;
    g_app->displayedHistoryIndex[1] = n - 1;
    g_app->displayedHistoryIndex[2] = -1;

    ApplyInputView();
    FocusInputRow(2);
}

bool ShiftInputViewOlder()
{
    if (!g_app) return false;

    int a = g_app->displayedHistoryIndex[0];
    if (a <= 0) return false;

    int b = g_app->displayedHistoryIndex[1];
    int c = g_app->displayedHistoryIndex[2];

    g_app->displayedHistoryIndex[0] = a - 1;
    g_app->displayedHistoryIndex[1] = a;
    g_app->displayedHistoryIndex[2] = (b >= 0) ? b : c;

    ApplyInputView();
    return true;
}

bool ShiftInputViewNewer()
{
    if (!g_app) return false;

    int n = (int)g_app->history.size();
    if (n <= 0) return false;

    int a = g_app->displayedHistoryIndex[0];
    int b = g_app->displayedHistoryIndex[1];
    int c = g_app->displayedHistoryIndex[2];

    if (c >= 0)
    {
        if (c < n - 1)
        {
            g_app->displayedHistoryIndex[0] = b;
            g_app->displayedHistoryIndex[1] = c;
            g_app->displayedHistoryIndex[2] = c + 1;
            ApplyInputView();
            return true;
        }
        if (c == n - 1)
        {
            g_app->displayedHistoryIndex[0] = b;
            g_app->displayedHistoryIndex[1] = c;
            g_app->displayedHistoryIndex[2] = -1;
            ApplyInputView();
            return true;
        }
    }
    return false;
}

// ==============================================
// 포커스 및 캐럿 관리
// ==============================================
void FocusInputRow(int row)
{
    if (!g_app) return;
    row = ClampInt(row, 0, INPUT_ROWS - 1);

    g_app->activeEditIndex = row;

    if (g_app->hwndEdit[row])
    {
        SetFocus(g_app->hwndEdit[row]);
        SendMessageW(g_app->hwndEdit[row], EM_SETSEL, -1, -1);
        EnsureVisibleEditCaret(g_app->hwndEdit[row]);
    }
}

void RecreateInputCaret(HWND hwnd)
{
    if (!g_app || !g_app->hFontInput) return;

    HDC hdc = GetDC(hwnd);
    HFONT oldFont = (HFONT)SelectObject(hdc, g_app->hFontInput);

    TEXTMETRICW tm = {};
    GetTextMetricsW(hdc, &tm);

    SelectObject(hdc, oldFont);
    ReleaseDC(hwnd, hdc);

    int caretHeight = tm.tmHeight + tm.tmExternalLeading;
    if (caretHeight < 16) caretHeight = 16;

    if (g_app)
    {
        int maxCaret = g_app->inputLineHeight - 2;
        if (maxCaret >= 16 && caretHeight > maxCaret)
            caretHeight = maxCaret;
    }

    DestroyCaret();
    CreateCaret(hwnd, nullptr, 2, caretHeight);
    ShowCaret(hwnd);
}

int GetInputRowY(int row)
{
    return INPUT_SEPARATOR_HEIGHT + row * g_app->inputLineHeight;
}

std::wstring GetInputRowText(int row)
{
    if (!g_app || row < 0 || row >= INPUT_ROWS || !g_app->hwndEdit[row])
        return L"";
    return GetEditTextW(g_app->hwndEdit[row]);
}

// ==============================================
// 입력 컨테이너 프로시저
// ==============================================
LRESULT CALLBACK InputContainerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        COLORREF bg = g_app ? g_app->inputStyle.backColor : RGB(0, 0, 0);
        HBRUSH hBrush = CreateSolidBrush(bg);
        FillRect(hdc, &rc, hBrush);
        DeleteObject(hBrush);
        return 1;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        HWND hEdit = (HWND)lParam;
        COLORREF back = g_app ? g_app->inputStyle.backColor : RGB(20, 20, 20);
        COLORREF text = g_app ? g_app->inputStyle.textColor : RGB(230, 230, 230);

        SetTextColor(hdc, text);
        SetBkColor(hdc, back);
        SetBkMode(hdc, OPAQUE);

        return (INT_PTR)(g_app && g_app->hbrInputEdit ? g_app->hbrInputEdit : (HBRUSH)(COLOR_WINDOW + 1));
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        HBRUSH hbrBack = CreateSolidBrush(g_app ? g_app->inputStyle.backColor : RGB(20, 20, 20));
        FillRect(hdc, &rc, hbrBack);
        DeleteObject(hbrBack);

        // 단축키 바가 숨겨져 있을 때 구분선
        if (!g_app || !g_app->shortcutBarVisible)
        {
            RECT sep = rc;
            sep.bottom = sep.top + INPUT_SEPARATOR_HEIGHT;
            FillRect(hdc, &sep, GetSysColorBrush(COLOR_BTNFACE));
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}


void ExecuteShortcutButton(int idx)
{
    if (!g_app || idx < 0 || idx >= SHORTCUT_BUTTON_COUNT) return;

    if (g_app->shortcutIsToggle[idx]) {
        g_app->shortcutActive[idx] = !g_app->shortcutActive[idx]; // 상태 반전
        if (g_app->shortcutActive[idx]) {
            SendRawCommandToMud(g_app->shortcutCommands[idx]); // ON 명령
        }
        else {
            SendRawCommandToMud(g_app->shortcutOffCommands[idx]); // OFF 명령
        }
        // 버튼 모양 즉시 갱신
        SendMessageW(g_app->hwndShortcutButtons[idx], BM_SETCHECK, g_app->shortcutActive[idx] ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    else {
        if (!g_app->shortcutCommands[idx].empty())
            SendRawCommandToMud(g_app->shortcutCommands[idx]);
    }
    if (g_app->hwndEdit[g_app->activeEditIndex]) SetFocus(g_app->hwndEdit[g_app->activeEditIndex]);
}

