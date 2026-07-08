#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "input.h"
#include "shortcut_bar.h"
#include "win_util.h"

#include "resource.h"
#include "settings.h"
#include <commctrl.h>

namespace
{
    constexpr size_t kMaxInputHistory = 1000;

    void TrimInputHistory()
    {
        if (!g_app)
            return;

        if (g_app->history.size() <= kMaxInputHistory)
            return;

        const size_t removeCount = g_app->history.size() - kMaxInputHistory;
        g_app->history.erase(g_app->history.begin(), g_app->history.begin() + removeCount);
        g_app->historyBrowseIndex = -1;

        for (int i = 0; i < INPUT_ROWS; ++i)
        {
            if (g_app->displayedHistoryIndex[i] >= 0)
            {
                g_app->displayedHistoryIndex[i] -= static_cast<int>(removeCount);
                if (g_app->displayedHistoryIndex[i] < 0)
                    g_app->displayedHistoryIndex[i] = -1;
            }
        }
    }
}

// ==============================================
// 입력창 레이아웃 및 메트릭스
// ==============================================
void LayoutInputEdits()
{
    if (!g_app || !g_app->hwndInput)
        return;

    RECT rc{};
    GetClientRect(g_app->hwndInput, &rc);
    int width = RectWidth(rc);
    int height = RectHeight(rc);
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

    ScopedWindowDC dc(g_app->hwndInput);
    if (!dc) return;

    HDC hdc = dc.Get();
    HFONT oldFont = nullptr;
    if (g_app->hFontInput)
        oldFont = (HFONT)SelectObject(hdc, g_app->hFontInput);

    TEXTMETRICW tm = {};
    GetTextMetricsW(hdc, &tm);

    if (oldFont)
        SelectObject(hdc, oldFont);

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

    ScopedWindowDC dc(hwnd);
    if (!dc) return;

    HDC hdc = dc.Get();
    HFONT oldFont = (HFONT)SelectObject(hdc, g_app->hFontInput);

    TEXTMETRICW tm = {};
    GetTextMetricsW(hdc, &tm);

    SelectObject(hdc, oldFont);

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
        FillRect(hdc, &rc, GetInputContainerBrush());
        return 1;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        HWND hEdit = (HWND)lParam;
        (void)hEdit;
        COLORREF back = g_app ? g_app->inputStyle.backColor : RGB(20, 20, 20);
        COLORREF text = g_app ? g_app->inputStyle.textColor : RGB(230, 230, 230);

        SetTextColor(hdc, text);
        SetBkColor(hdc, back);
        SetBkMode(hdc, OPAQUE);

        return (INT_PTR)GetInputEditBrush();
    }

    case WM_PAINT:
    {
        ScopedPaintDC paint(hwnd);
        HDC hdc = paint.Get();
        if (!hdc) return 0;
        RECT rc;
        GetClientRect(hwnd, &rc);

        FillRect(hdc, &rc, GetInputContainerBrush());

        // 단축키 바가 숨겨져 있을 때 구분선
        if (!g_app || !g_app->shortcutBarVisible)
        {
            RECT sep = rc;
            sep.bottom = sep.top + INPUT_SEPARATOR_HEIGHT;
            FillRect(hdc, &sep, GetSysColorBrush(COLOR_BTNFACE));
        }

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

// ==============================================
// 입력창 편집 서브클래스 프로시저
// ==============================================
LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    int index = -1;
    for (int i = 0; i < INPUT_ROWS; ++i)
    {
        if (g_app->hwndEdit[i] == hwnd)
        {
            index = i;
            break;
        }
    }

    if (index < 0)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_GETDLGCODE:
    {
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS | DLGC_WANTMESSAGE;
    }

    case WM_SETFOCUS:
    {
        g_app->activeEditIndex = index;
        g_app->historyBrowseIndex = -1;
        LRESULT lr = CallWindowProcW(g_app->oldEditProc[index], hwnd, msg, wParam, lParam);
        EnsureVisibleEditCaret(hwnd);
        InvalidateRect(g_app->hwndInput, nullptr, FALSE);
        return lr;
    }

    case WM_KILLFOCUS:
    {
        LRESULT lr = CallWindowProcW(g_app->oldEditProc[index], hwnd, msg, wParam, lParam);
        HideCaret(hwnd);
        InvalidateRect(g_app->hwndInput, nullptr, FALSE);
        return lr;
    }

    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE:
    {
        LRESULT lr = CallWindowProcW(g_app->oldEditProc[index], hwnd, msg, wParam, lParam);
        EnsureVisibleEditCaret(hwnd);
        if (GetFocus() == hwnd)
        {
            ShowCaret(hwnd);
            SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
        }
        return lr;
    }

    case WM_SYSKEYDOWN:
    {
        if (HandleGlobalMenuShortcut(hwnd, msg, wParam))
            return 0;

        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

        if (HandleFunctionKey((int)wParam))
            return 0;

        if (alt && !ctrl && !shift && wParam == VK_SPACE)
        {
            for (int i = 0; i < INPUT_ROWS; ++i) {
                if (g_app->hwndEdit[i]) SetWindowTextW(g_app->hwndEdit[i], L"");
            }

            if (g_app) {
                g_app->history.clear();                    // ← 추가
                g_app->displayedHistoryIndex[0] = -1;
                g_app->displayedHistoryIndex[1] = -1;
                g_app->displayedHistoryIndex[2] = -1;
                SetInputViewLatest();
            }

            FocusInputRow(0);
            return 0;
        }

        // 기존의 Alt + 숫자 단축키 로직 유지
        if (alt && !ctrl && !shift)
        {
            int shortcutIdx = -1;
            if (wParam >= '1' && wParam <= '9') shortcutIdx = (int)(wParam - '1');
            else if (wParam == '0') shortcutIdx = 9;
            else if (wParam >= VK_NUMPAD1 && wParam <= VK_NUMPAD9) shortcutIdx = (int)(wParam - VK_NUMPAD1);
            else if (wParam == VK_NUMPAD0) shortcutIdx = 9;

            if (shortcutIdx >= 0 && shortcutIdx < SHORTCUT_BUTTON_COUNT)
            {
                ExecuteShortcutButton(shortcutIdx);
                return 0;
            }
        }
        break;
    }

    case WM_SYSCHAR:
    {
        // Alt + Space 시 발생하는 시스템 비프음 및 메뉴 활성화 차단
        if (wParam == VK_SPACE) return 0;
        break;
    }

    case WM_KEYDOWN:
    {
        if (HandleGlobalMenuShortcut(hwnd, msg, wParam))
            return 0;

        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

        // F4 키가 눌렸을 때 (다른 조합 없이 순수하게 F4만 눌린 경우)
        if (wParam == VK_F4 && !ctrl && !alt && !shift)
        {
            // 메인 윈도우에 특수기호 메뉴 클릭 메시지를 강제로 보냅니다.
            SendMessageW(g_app->hwndMain, WM_COMMAND, ID_MENU_VIEW_SYMBOLS, 0);
            return 0; // 여기서 처리를 끝내서 에디트 컨트롤이나 매크로가 가로채지 못하게 함
        }

        if (HandleFunctionKey((int)wParam))
            return 0;

        if (ctrl && wParam == 'A')
        {
            SendMessageW(hwnd, EM_SETSEL, 0, -1);
            EnsureVisibleEditCaret(hwnd);
            return 0;
        }

        if (!ctrl && !shift && !alt && wParam == VK_PRIOR) {
            if (g_app && g_app->termBuffer) { TerminalBufferMetrics m = g_app->termBuffer->GetMetrics(); g_app->termBuffer->DoScroll(m.height / 2); InvalidateRect(g_app->hwndLog, nullptr, FALSE); }
            return 0;
        }
        if (!ctrl && !shift && !alt && wParam == VK_NEXT) {
            if (g_app && g_app->termBuffer) { TerminalBufferMetrics m = g_app->termBuffer->GetMetrics(); g_app->termBuffer->DoScroll(-(m.height / 2)); InvalidateRect(g_app->hwndLog, nullptr, FALSE); }
            return 0;
        }
        if (ctrl && !shift && !alt && wParam == VK_HOME) {
            if (g_app && g_app->termBuffer) { g_app->termBuffer->ScrollToHistoryTop(); InvalidateRect(g_app->hwndLog, nullptr, FALSE); }
            return 0;
        }
        if (ctrl && !shift && !alt && wParam == VK_END) {
            if (g_app && g_app->termBuffer) { g_app->termBuffer->ScrollToLive(); InvalidateRect(g_app->hwndLog, nullptr, FALSE); }
            return 0;
        }
        if (ctrl && shift && !alt && wParam == VK_HOME) {
            if (g_app && g_app->termBuffer) {
                TerminalBufferMetrics m = g_app->termBuffer->GetMetrics();
                int currentViewBottomY = m.historySize + m.height - 1 - m.scrollOffset;
                g_app->termBuffer->SetSelectionRange(0, 0, m.width - 1, currentViewBottomY);
                InvalidateRect(g_app->hwndLog, nullptr, FALSE);
            }
            return 0;
        }
        if (ctrl && shift && !alt && wParam == VK_END) {
            if (g_app && g_app->termBuffer) {
                TerminalBufferMetrics m = g_app->termBuffer->GetMetrics();
                int currentViewTopY = m.historySize - m.scrollOffset;
                int absoluteBottom = m.historySize + m.height - 1;
                g_app->termBuffer->SetSelectionRange(0, currentViewTopY, m.width - 1, absoluteBottom);
                InvalidateRect(g_app->hwndLog, nullptr, FALSE);
            }
            return 0;
        }

        // --- 화면 및 입력창 지우기 ---
        if (ctrl && !alt && !shift && wParam == VK_SPACE) {
            ClearLogWindow(false);
            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, -1, -1); EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }
        if (ctrl && shift && !alt && wParam == VK_SPACE) {
            // 1. 로그창 완전 초기화
            ClearLogWindow(true);

            // 2. 입력창 히스토리 완전 삭제 (이 부분이 빠져 있어서 문제가 발생)
            if (g_app) {
                g_app->history.clear();                    // ← 핵심: 히스토리 벡터 완전 삭제
                g_app->displayedHistoryIndex[0] = -1;
                g_app->displayedHistoryIndex[1] = -1;
                g_app->displayedHistoryIndex[2] = -1;
                SetInputViewLatest();                      // ← 입력창 화면도 최신 상태로 초기화
            }

            // 3. 입력창 포커스 복구
            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit);
                SendMessageW(hEdit, EM_SETSEL, -1, -1);
                EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }
        // ★ 숫자 키패드 매크로 가로채기 (NumLock이 켜져 있을 때만 작동)
        if (!ctrl && !shift && !alt && g_app && g_app->numpadMacroEnabled) {
            int npIdx = -1;
            if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9) npIdx = (int)(wParam - VK_NUMPAD0);
            else if (wParam == VK_DIVIDE) npIdx = 10;   // /
            else if (wParam == VK_MULTIPLY) npIdx = 11; // *
            else if (wParam == VK_SUBTRACT) npIdx = 12; // -
            else if (wParam == VK_ADD) npIdx = 13;      // +
            else if (wParam == VK_DECIMAL) npIdx = 14;  // .

            if (npIdx >= 0 && !g_app->numpadMacros[npIdx].empty()) {
                SendRawCommandToMud(g_app->numpadMacros[npIdx]);
                return 0; // 0을 반환해서 입력창에 글씨가 안 써지게 아예 먹어버림!
            }
        }


        // --- 키보드 세부 이동 로직 (원본 100% 복원) ---
        switch (wParam)
        {
        case VK_RETURN:
        {
            std::wstring line = GetInputRowText(index);

            if (Trim(line).empty())
            {
                // 콘솔에서 그냥 Enter 친 것과 동일하게 CR만 전송
                SendCommandToProcess(L"");
            }
            else
            {
                SendTextToMud(line);
                g_app->history.push_back(line);
                TrimInputHistory();
            }

            SetInputViewLatest();

            if (g_app && g_app->hwndEdit[g_app->activeEditIndex])
            {
                SetFocus(g_app->hwndEdit[g_app->activeEditIndex]);
                SendMessageW(g_app->hwndEdit[g_app->activeEditIndex], EM_SETSEL, -1, -1);
                EnsureVisibleEditCaret(g_app->hwndEdit[g_app->activeEditIndex]);
            }

            return 0;
        }
        case VK_BACK:
        {
            DWORD sel = (DWORD)SendMessageW(hwnd, EM_GETSEL, 0, 0);
            int start = LOWORD(sel);
            int end = HIWORD(sel);

            if (start == end && start == 0) // 커서가 맨 앞일 때
            {
                if (index > 0) {
                    if (g_app && g_app->limitBackspaceToCurrentRow) return 0; // 제한 옵션 시 무시

                    // 이전 줄로 이동 로직
                    std::wstring prev = GetEditTextW(g_app->hwndEdit[index - 1]);
                    int prevLen = (int)prev.size();
                    g_app->activeEditIndex = index - 1;
                    SetFocus(g_app->hwndEdit[index - 1]);
                    SendMessageW(g_app->hwndEdit[index - 1], EM_SETSEL, prevLen, prevLen);
                    return 0;
                }
                else {
                    // ★ 0번 인덱스(첫 줄) 맨 앞에서 백스페이스 누를 때 소리 방지
                    return 0;
                }
            }
            break;
        }

        case VK_HOME:
        {
            if (ctrl) break;
            int target = 0;
            if (shift) {
                DWORD sel = (DWORD)SendMessageW(hwnd, EM_GETSEL, 0, 0);
                SendMessageW(hwnd, EM_SETSEL, HIWORD(sel), target);
            }
            else {
                SendMessageW(hwnd, EM_SETSEL, target, target);
            }
            EnsureVisibleEditCaret(hwnd);
            return 0;
        }

        case VK_END:
        {
            if (ctrl) break;
            int len = GetWindowTextLengthW(hwnd);
            if (shift) {
                DWORD sel = (DWORD)SendMessageW(hwnd, EM_GETSEL, 0, 0);
                SendMessageW(hwnd, EM_SETSEL, LOWORD(sel), len);
            }
            else {
                SendMessageW(hwnd, EM_SETSEL, len, len);
            }
            EnsureVisibleEditCaret(hwnd);
            return 0;
        }

        case VK_LEFT:
        {
            if (!ctrl && !shift) {
                DWORD sel = (DWORD)SendMessageW(hwnd, EM_GETSEL, 0, 0);
                if (LOWORD(sel) == HIWORD(sel) && LOWORD(sel) == 0 && index > 0) {
                    std::wstring prev = GetEditTextW(g_app->hwndEdit[index - 1]);
                    int prevLen = (int)prev.size();
                    g_app->activeEditIndex = index - 1;
                    SetFocus(g_app->hwndEdit[index - 1]);
                    SendMessageW(g_app->hwndEdit[index - 1], EM_SETSEL, prevLen, prevLen);
                    EnsureVisibleEditCaret(g_app->hwndEdit[index - 1]);
                    return 0;
                }
            }
            break;
        }

        case VK_RIGHT:
        {
            if (!ctrl && !shift) {
                DWORD sel = (DWORD)SendMessageW(hwnd, EM_GETSEL, 0, 0);
                int len = GetWindowTextLengthW(hwnd);
                if (LOWORD(sel) == HIWORD(sel) && LOWORD(sel) == len && index < INPUT_ROWS - 1) {
                    g_app->activeEditIndex = index + 1;
                    SetFocus(g_app->hwndEdit[index + 1]);
                    SendMessageW(g_app->hwndEdit[index + 1], EM_SETSEL, 0, 0);
                    EnsureVisibleEditCaret(g_app->hwndEdit[index + 1]);
                    return 0;
                }
            }
            break;
        }

        case VK_UP:
            if (!ctrl && !shift) {
                if (index > 0) FocusInputRow(index - 1);
                else if (ShiftInputViewOlder()) FocusInputRow(0);
                return 0;
            }
            break;

        case VK_DOWN:
            if (!ctrl && !shift) {
                if (index < INPUT_ROWS - 1) FocusInputRow(index + 1);
                else if (ShiftInputViewNewer()) FocusInputRow(INPUT_ROWS - 1);
                return 0;
            }
            break;
        }
        break;
    }

    case WM_CHAR:
    case WM_IME_CHAR:
    {
        if (wParam == VK_RETURN || wParam == '\r' || wParam == '\n')
            return 0;

        if (wParam == VK_BACK)
        {
            DWORD sel = (DWORD)SendMessageW(hwnd, EM_GETSEL, 0, 0);
            if (LOWORD(sel) == 0 && HIWORD(sel) == 0)
                return 0;
        }

        if (wParam == 1) { // Ctrl+A
            SendMessageW(hwnd, EM_SETSEL, 0, -1);
            return 0;
        }
        break;
    }
    }

    LRESULT lr = CallWindowProcW(g_app->oldEditProc[index], hwnd, msg, wParam, lParam);

    if (msg == WM_KEYUP || msg == WM_CHAR || msg == WM_PASTE || msg == EM_REPLACESEL) {
        EnsureVisibleEditCaret(hwnd);
        if (GetFocus() == hwnd) ShowCaret(hwnd);
    }

    return lr;
}
