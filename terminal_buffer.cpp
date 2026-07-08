#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "resource.h"
#include "settings.h"
#include <commctrl.h>

// 함수 전방 선언 (Forward Declarations) 추가
void ClearLogWindow(bool clearAllBuffer);

bool IsBoxDrawingChar(wchar_t ch)
{
    return (ch >= 0x2500 && ch <= 0x257F);
}



int GetTerminalGlyphWidth(wchar_t ch, bool forceAmbiguousWide)
{
    if (ch == 0 || ch == L'\0')
        return 1;

    // 1) ASCII는 항상 1칸
    if (ch <= 0x007F)
        return 1;

    // 2) 한글·전각 공백·한자는 항상 2칸
    if ((ch >= 0x1100 && ch <= 0x11FF) ||
        (ch >= 0x3130 && ch <= 0x318F) ||
        (ch >= 0xAC00 && ch <= 0xD7A3) ||
        (ch == 0x3000) ||
        (ch >= 0x4E00 && ch <= 0x9FFF))
        return 2;

    // 3) 모호한 문자 판정
    bool isAmbiguous = false;

    // EUC-KR/CP949 기반 한국 MUD 지도에서 전각처럼 쓰이는 기호들
    if (ch >= 0x2500 && ch <= 0x257F) isAmbiguous = true; // Box Drawing ─ │ ┌ ┐
    if (ch >= 0x25A0 && ch <= 0x25FF) isAmbiguous = true; // Geometric Shapes ○ ● ◎
    if (ch >= 0x2190 && ch <= 0x21FF) isAmbiguous = true; // Arrows ← ↑ → ↓
    if (ch >= 0x2200 && ch <= 0x22FF) isAmbiguous = true; // Math Operators × ∧ ∨
    if (ch >= 0x2460 && ch <= 0x24FF) isAmbiguous = true; // Enclosed ①②③
    if (ch >= 0x2600 && ch <= 0x26FF) isAmbiguous = true; // Misc symbols ★☆
    if (ch >= 0x2700 && ch <= 0x27BF) isAmbiguous = true;
    if (ch >= 0x2E80 && ch <= 0xA4CF) isAmbiguous = true;
    if (ch >= 0xFE10 && ch <= 0xFE19) isAmbiguous = true;
    if (ch >= 0xFE30 && ch <= 0xFE6F) isAmbiguous = true;
    if (ch >= 0xFF01 && ch <= 0xFF60) isAmbiguous = true; // Fullwidth ASCII
    if (ch >= 0xFFE0 && ch <= 0xFFE6) isAmbiguous = true;

    if (isAmbiguous)
    {
        // 원본 구조 유지: 강제 넓게 처리 요청 또는 옵션값에 따라 2칸으로 처리합니다.
        // 한국어 MUD의 박스/도형/화살표 화면은 이 옵션을 켜는 쪽이 맞습니다.
        bool ambiguousWide = forceAmbiguousWide;

        if (!ambiguousWide)
            ambiguousWide = (g_app && g_app->ambiguousEastAsianWide);

        return ambiguousWide ? 2 : 1;
    }

    // 나머지는 기본 1칸
    return 1;
}

int GetTerminalGlyphWidth(wchar_t ch)
{
    return GetTerminalGlyphWidth(ch, false);
}

int GetCharWidthW(wchar_t ch, bool forceAmbiguousWide)
{
    return GetTerminalGlyphWidth(ch, forceAmbiguousWide);
}

int GetCharWidthW(wchar_t ch)
{
    return GetCharWidthW(ch, false);
}

bool NeedsExtraRightShiftForWideGlyph(wchar_t ch)
{
    if (ch >= 0x2500 && ch <= 0x257F) // Box Drawing
        return true;

    if (ch >= 0x25A0 && ch <= 0x25FF) // Geometric Shapes
        return true;

    if (ch >= 0x2600 && ch <= 0x26FF) // Misc Symbols
        return true;

    if (ch >= 0x2700 && ch <= 0x27BF) // Dingbats
        return true;

    if (ch >= 0x2190 && ch <= 0x21FF) // Arrows
        return true;

    if (ch >= 0x2200 && ch <= 0x22FF) // Math Operators
        return true;

    return false;
}
// ==============================================
// 내부 헬퍼 함수 (static)
// ==============================================
int GetDisplayCellWidth(wchar_t ch)
{
    return GetTerminalGlyphWidth(ch);
}

// ==============================================
// TerminalBuffer 클래스 구현
// ==============================================

TerminalBuffer::TerminalBuffer(int w, int h)
{
    Resize(w, h);
}

void TerminalBuffer::MoveCursorLeftVisual(int n)
{
    while (n-- > 0)
    {
        if (cursorX <= 0)
        {
            cursorX = 0;
            break;
        }

        cursorX--;

        if (GetCell(cursorX, cursorY).isWideTrailer && cursorX > 0)
            cursorX--;
    }
}

void TerminalBuffer::MoveCursorRightVisual(int n)
{
    while (n-- > 0)
    {
        if (cursorX >= width - 1)
        {
            cursorX = width - 1;
            break;
        }

        // 현재 칸이 와이드 본체면 trailer를 건너뜀
        if (cursorX + 1 < width && GetCell(cursorX + 1, cursorY).isWideTrailer)
            cursorX += 2;
        else
            cursorX += 1;

        if (cursorX >= width)
            cursorX = width - 1;
    }
}
void TerminalBuffer::ClearCellPairAware(int x, int y)
{
    if (x < 0 || x >= width || y < 0 || y >= height)
        return;

    TerminalCell& c = GetCell(x, y);

    // 현재 칸이 와이드 문자의 오른쪽(trailer)이면 왼쪽 본체까지 같이 제거
    if (c.isWideTrailer)
    {
        ClearSingleCell(x, y);
        if (x - 1 >= 0)
            ClearSingleCell(x - 1, y);
        return;
    }

    // 현재 칸이 와이드 본체이고, 오른쪽이 trailer이면 같이 제거
    if (x + 1 < width && GetCell(x + 1, y).isWideTrailer)
    {
        ClearSingleCell(x, y);
        ClearSingleCell(x + 1, y);
        return;
    }

    ClearSingleCell(x, y);
}

void TerminalBuffer::NormalizeCursorForWrite()
{
    if (cursorX < 0) cursorX = 0;
    if (cursorY < 0) cursorY = 0;
    if (cursorX >= width) cursorX = width - 1;
    if (cursorY >= height) cursorY = height - 1;

    // 커서가 와이드 문자의 오른쪽 칸(trailer)에 걸려 있으면
    // 항상 본체 시작 칸으로 되돌림
    if (GetCell(cursorX, cursorY).isWideTrailer && cursorX > 0)
        cursorX--;
}

void TerminalBuffer::Resize(int w, int h)
{
    std::lock_guard<std::recursive_mutex> lock(mtx);
    if (w == width && h == height && !cells.empty()) return;

    std::vector<TerminalCell> newCells(w * h, { L' ', defaultFg, defaultBg, false, false });

    int minW = (width < w) ? width : w;
    int minH = (height < h) ? height : h;

    if (!cells.empty())
    {
        for (int y = 0; y < minH; ++y)
            for (int x = 0; x < minW; ++x)
                newCells[y * w + x] = cells[y * width + x];
    }

    width = w;
    height = h;
    cells = std::move(newCells);

    if (cursorX >= width) cursorX = width - 1;
    if (cursorY >= height) cursorY = height - 1;
}


void TerminalBuffer::PutChar(wchar_t ch, COLORREF fg, COLORREF bg, bool bold)
{
    std::lock_guard<std::recursive_mutex> lock(mtx);

    if (ch == L'\r') { cursorX = 0; return; }
    if (ch == L'\n') {
        cursorY++;
        if (cursorY >= height) { ScrollUp(); cursorY = height - 1; }
        return;
    }
    if (ch == 0x08) { MoveCursorLeftVisual(1); return; }
    if (ch == 0x09) {
        int nextTab = (cursorX + 8) & ~7;
        while (cursorX < nextTab && cursorX < width - 1)
            MoveCursorRightVisual(1);
        return;
    }
    if (ch < 0x20) return;

    NormalizeCursorForWrite();

    int cw = GetCharWidthW(ch);
    if (cw < 1) cw = 1;
    if (cw > 2) cw = 2;

    if (cursorX + cw > width) {
        cursorX = 0;
        cursorY++;
        if (cursorY >= height) { ScrollUp(); cursorY = height - 1; }
        NormalizeCursorForWrite();
    }

    ClearCellPairAware(cursorX, cursorY);
    if (cw == 2 && cursorX + 1 < width)
        ClearCellPairAware(cursorX + 1, cursorY);

    TerminalCell& cell = GetCell(cursorX, cursorY);
    cell.ch = ch;
    cell.fg = fg;
    cell.bg = bg;
    cell.bold = bold;
    cell.isWideTrailer = false;

    if (cw == 2) {
        if (cursorX + 1 >= width) {
            cell.ch = L' ';
            cell.fg = defaultFg;
            cell.bg = defaultBg;
            cell.bold = false;
            cell.isWideTrailer = false;
            return;
        }
        TerminalCell& trailer = GetCell(cursorX + 1, cursorY);
        trailer.ch = L' ';
        trailer.fg = fg;
        trailer.bg = bg;
        trailer.bold = bold;
        trailer.isWideTrailer = true;
    }

    cursorX += cw;
    if (cursorX >= width) cursorX = width - 1;
}

void TerminalBuffer::ScrollUp()
{
    std::vector<TerminalCell> topRow(width);
    for (int x = 0; x < width; ++x)
        topRow[x] = GetCell(x, 0);

    history.push_back(std::move(topRow));

    if ((int)history.size() > maxHistory) {
        history.pop_front();
        if (hasSelection) {
            selStartY--; selEndY--;
            if (selStartY < 0 && selEndY < 0) hasSelection = false;
            else {
                if (selStartY < 0) selStartY = 0;
                if (selEndY < 0) selEndY = 0;
            }
        }
    }

    for (int y = 1; y < height; ++y)
        for (int x = 0; x < width; ++x)
            GetCell(x, y - 1) = GetCell(x, y);

    for (int x = 0; x < width; ++x) {
        TerminalCell& cell = GetCell(x, height - 1);
        cell.ch = L' ';
        cell.bg = defaultBg;
        cell.isWideTrailer = false;
    }

    if (scrollOffset > 0) {
        scrollOffset++;
        if (scrollOffset > (int)history.size())
            scrollOffset = (int)history.size();
    }
}

// ==============================================
// 외부 함수들
// ==============================================

void ClearLogWindow(bool clearAllBuffer)
{
    if (!g_app || !g_app->termBuffer) return;
    std::lock_guard<std::recursive_mutex> lock(g_app->termBuffer->mtx);

    if (clearAllBuffer) {
        g_app->termBuffer->history.clear();
    }
    else {
        for (int y = 0; y < g_app->termBuffer->height; ++y) {
            std::vector<TerminalCell> row(g_app->termBuffer->width);
            for (int x = 0; x < g_app->termBuffer->width; ++x) {
                row[x] = g_app->termBuffer->cells[y * g_app->termBuffer->width + x];
            }
            g_app->termBuffer->history.push_back(std::move(row));
            if ((int)g_app->termBuffer->history.size() > g_app->termBuffer->maxHistory)
                g_app->termBuffer->history.pop_front();
        }
    }

    for (auto& cell : g_app->termBuffer->cells) {
        cell.ch = L' ';
        cell.bg = g_app->termBuffer->defaultBg;
        cell.isWideTrailer = false;
    }

    g_app->termBuffer->cursorX = 0;
    g_app->termBuffer->cursorY = 0;
    g_app->termBuffer->scrollOffset = 0;
    g_app->termBuffer->ClearSelection();

    if (g_app->hwndLog)
        InvalidateRect(g_app->hwndLog, nullptr, TRUE);
}

void ResetHistoryBrowse()
{
    if (!g_app) return;
    g_app->historyBrowseIndex = -1;
}

// ConPTY 관련 함수
bool GetConPtyApi(PFN_CreatePseudoConsole* createFn, PFN_ResizePseudoConsole* resizeFn, PFN_ClosePseudoConsole* closeFn)
{
    if (!createFn || !resizeFn || !closeFn) return false;
    HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
    if (!hKernel) return false;

    union {
        FARPROC raw;
        PFN_CreatePseudoConsole typed;
    } createProc = { GetProcAddress(hKernel, "CreatePseudoConsole") };
    union {
        FARPROC raw;
        PFN_ResizePseudoConsole typed;
    } resizeProc = { GetProcAddress(hKernel, "ResizePseudoConsole") };
    union {
        FARPROC raw;
        PFN_ClosePseudoConsole typed;
    } closeProc = { GetProcAddress(hKernel, "ClosePseudoConsole") };

    *createFn = createProc.typed;
    *resizeFn = resizeProc.typed;
    *closeFn = closeProc.typed;

    return (*createFn != nullptr && *resizeFn != nullptr && *closeFn != nullptr);
}

COORD GetPseudoConsoleSizeFromLogWindow()
{
    COORD size = { 80, 32 };
    if (g_app && g_app->termBuffer) {
        size.X = (SHORT)g_app->termBuffer->width;
        size.Y = (SHORT)g_app->termBuffer->height;
    }
    return size;
}

bool ResizePseudoConsoleToLogWindow()
{
    if (!g_app || !g_app->proc.hPC) return false;

    PFN_CreatePseudoConsole createFn = nullptr;
    PFN_ResizePseudoConsole resizeFn = nullptr;
    PFN_ClosePseudoConsole closeFn = nullptr;

    if (!GetConPtyApi(&createFn, &resizeFn, &closeFn)) return false;

    COORD size = GetPseudoConsoleSizeFromLogWindow();
    HRESULT hr = resizeFn(g_app->proc.hPC, size);
    return SUCCEEDED(hr);
}

void ClosePseudoConsoleHandle(HPCON hpc)
{
    if (!hpc) return;

    PFN_CreatePseudoConsole createFn = nullptr;
    PFN_ResizePseudoConsole resizeFn = nullptr;
    PFN_ClosePseudoConsole closeFn = nullptr;

    if (!GetConPtyApi(&createFn, &resizeFn, &closeFn)) return;
    closeFn(hpc);
}

void TerminalBuffer::ClearSelection() { hasSelection = false; }
void TerminalBuffer::SetSelectionStart(int x, int y) { selStartX = x; selStartY = y; selEndX = x; selEndY = y; hasSelection = true; }
void TerminalBuffer::SetSelectionEnd(int x, int y) { selEndX = x; selEndY = y; }
bool TerminalBuffer::IsSelected(int x, int absY) {
    if (!hasSelection) return false;
    int sy1 = selStartY, sx1 = selStartX, sy2 = selEndY, sx2 = selEndX;
    if (sy1 > sy2 || (sy1 == sy2 && sx1 > sx2)) { std::swap(sy1, sy2); std::swap(sx1, sx2); }
    if (absY < sy1 || absY > sy2) return false;
    if (sy1 == sy2) return x >= sx1 && x <= sx2;
    if (absY == sy1) return x >= sx1;
    if (absY == sy2) return x <= sx2;
    return true;
}
std::wstring TerminalBuffer::GetSelectedText() {
    if (!hasSelection) return L"";
    int sy1 = selStartY, sx1 = selStartX, sy2 = selEndY, sx2 = selEndX;
    if (sy1 > sy2 || (sy1 == sy2 && sx1 > sx2)) { std::swap(sy1, sy2); std::swap(sx1, sx2); }
    std::wstring res;
    for (int y = sy1; y <= sy2; ++y) {
        if (y < 0 || y >= (int)history.size() + height) continue;
        int startX = (y == sy1) ? sx1 : 0;
        int endX = (y == sy2) ? sx2 : width - 1;
        std::wstring line;
        for (int x = startX; x <= endX; ++x) {
            TerminalCell c;
            if (y < (int)history.size()) c = history[y][x];
            else c = cells[(y - (int)history.size()) * width + x];
            if (!c.isWideTrailer) line += c.ch;
        }
        while (!line.empty() && line.back() == L' ') line.pop_back();
        res += line;
        if (y < sy2) res += L"\r\n";
    }
    return res;
}
std::wstring TerminalBuffer::GetWordAt(int x, int absY) {
    if (absY < 0 || absY >= (int)history.size() + height) return L"";
    std::wstring cleanLine;
    std::vector<int> colToCharIdx(width, -1);
    for (int i = 0; i < width; ++i) {
        TerminalCell c;
        if (absY < (int)history.size()) c = history[absY][i];
        else c = cells[(absY - (int)history.size()) * width + i];
        if (!c.isWideTrailer) { colToCharIdx[i] = (int)cleanLine.length(); cleanLine += c.ch; }
        else if (i > 0) colToCharIdx[i] = colToCharIdx[i - 1];
    }
    if (x < 0 || x >= width) return L"";
    int strIdx = colToCharIdx[x];
    if (strIdx == -1 || strIdx >= (int)cleanLine.length()) return L"";
    auto is_break = [](wchar_t ch) { return ch <= L' ' || wcschr(L",.:;!?\"'()[]{}<>=+*|", ch) != nullptr; };
    if (is_break(cleanLine[strIdx])) return L"";
    int start = strIdx, end = strIdx;
    while (start > 0 && !is_break(cleanLine[start - 1])) start--;
    while (end < (int)cleanLine.size() - 1 && !is_break(cleanLine[end + 1])) end++;
    return cleanLine.substr(start, end - start + 1);
}
std::wstring TerminalBuffer::GetCurrentScreenText() {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    std::wstring res;
    for (int y = 0; y < height; ++y) {
        std::wstring line;
        for (int x = 0; x < width; ++x) {
            if (!cells[y * width + x].isWideTrailer) line += cells[y * width + x].ch;
        }
        while (!line.empty() && line.back() == L' ') line.pop_back();
        res += line + L"\r\n";
    }
    return res;
}
std::wstring TerminalBuffer::GetHistoryText() {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    std::wstring res;
    for (size_t y = 0; y < history.size(); ++y) {
        std::wstring line;
        for (int x = 0; x < width; ++x) {
            if (!history[y][x].isWideTrailer) line += history[y][x].ch;
        }
        while (!line.empty() && line.back() == L' ') line.pop_back();
        res += line + L"\r\n";
    }
    return res;
}
TerminalCell& TerminalBuffer::GetCell(int x, int y) {
    static TerminalCell dummy;
    if (x < 0 || x >= width || y < 0 || y >= height) return dummy;
    return cells[y * width + x];
}
TerminalCell TerminalBuffer::GetViewCell(int x, int y) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    int absRow = (int)history.size() + y - scrollOffset;
    if (absRow >= 0 && absRow < (int)history.size()) {
        if (x >= 0 && x < (int)history[absRow].size()) return history[absRow][x];
    }
    else if (absRow >= (int)history.size()) {
        int liveY = absRow - (int)history.size();
        if (liveY >= 0 && liveY < height && x >= 0 && x < width) return cells[liveY * width + x];
    }
    return { L' ', defaultFg, defaultBg, false, false };
}
void TerminalBuffer::DoScroll(int lines) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    scrollOffset += lines;
    if (scrollOffset < 0) scrollOffset = 0;
    if (scrollOffset > (int)history.size()) scrollOffset = (int)history.size();
}
void TerminalBuffer::ClearSingleCell(int x, int y) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    TerminalCell& c = GetCell(x, y);
    c.ch = L' '; c.fg = defaultFg; c.bg = defaultBg; c.bold = false; c.isWideTrailer = false;
}
void TerminalBuffer::ClearLineRangePairAware(int y, int x1, int x2) {
    if (y < 0 || y >= height) return;
    if (x1 > x2) std::swap(x1, x2);
    if (x1 < 0) x1 = 0;
    if (x2 >= width) x2 = width - 1;
    for (int x = x1; x <= x2; ++x) ClearCellPairAware(x, y);
}
void TerminalBuffer::HandleCommand(char cmd, const std::string& params) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    std::vector<int> args; int val = 0; bool have = false;
    for (char c : params) {
        if (c >= '0' && c <= '9') { val = val * 10 + (c - '0'); have = true; }
        else if (c == ';') { args.push_back(have ? val : 0); val = 0; have = false; }
    }
    args.push_back(have ? val : 0);
    auto ArgOr = [&](size_t idx, int defVal) -> int { return (idx >= args.size() || args[idx] <= 0) ? defVal : args[idx]; };
    if (cmd == 'J') {
        int mode = args.empty() ? 0 : args[0];
        if (mode == 0) { ClearLineRangePairAware(cursorY, cursorX, width - 1); for (int y = cursorY + 1; y < height; ++y) ClearLineRangePairAware(y, 0, width - 1); }
        else if (mode == 1) { for (int y = 0; y < cursorY; ++y) ClearLineRangePairAware(y, 0, width - 1); ClearLineRangePairAware(cursorY, 0, cursorX); }
        else if (mode == 2) { for (int y = 0; y < height; ++y) ClearLineRangePairAware(y, 0, width - 1); cursorX = 0; cursorY = 0; scrollOffset = 0; }
    }
    else if (cmd == 'K') {
        int mode = args.empty() ? 0 : args[0];
        if (mode == 0) ClearLineRangePairAware(cursorY, cursorX, width - 1);
        else if (mode == 1) ClearLineRangePairAware(cursorY, 0, cursorX);
        else if (mode == 2) ClearLineRangePairAware(cursorY, 0, width - 1);
    }
    else if (cmd == 'H' || cmd == 'f') {
        int r = ArgOr(0, 1) - 1, c = ArgOr(1, 1) - 1;
        if (r < 0) r = 0;
        if (c < 0) c = 0;
        if (r >= height) r = height - 1;
        if (c >= width) c = width - 1;
        cursorY = r; cursorX = c; NormalizeCursorForWrite();
    }
    else if (cmd == 'A') { cursorY -= ArgOr(0, 1); if (cursorY < 0) cursorY = 0; NormalizeCursorForWrite(); }
    else if (cmd == 'B') { cursorY += ArgOr(0, 1); if (cursorY >= height) cursorY = height - 1; NormalizeCursorForWrite(); }
    else if (cmd == 'C') { MoveCursorRightVisual(ArgOr(0, 1)); NormalizeCursorForWrite(); }
    else if (cmd == 'D') { MoveCursorLeftVisual(ArgOr(0, 1)); NormalizeCursorForWrite(); }
    else if (cmd == 'G' || cmd == '`') { int n = ArgOr(0, 1) - 1; if (n < 0) n = 0; if (n >= width) n = width - 1; cursorX = n; NormalizeCursorForWrite(); }
    else if (cmd == 'd') { int n = ArgOr(0, 1) - 1; if (n < 0) n = 0; if (n >= height) n = height - 1; cursorY = n; NormalizeCursorForWrite(); }
}
