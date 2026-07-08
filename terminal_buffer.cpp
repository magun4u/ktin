#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "resource.h"
#include "settings.h"
#include <commctrl.h>
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <cwchar>

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

int KTinCharWidth(wchar_t ch, bool forceAmbiguousWide)
{
    return GetTerminalGlyphWidth(ch, forceAmbiguousWide);
}

int KTinCharWidth(wchar_t ch)
{
    return KTinCharWidth(ch, false);
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

namespace
{
    struct TerminalTextRowCopy
    {
        std::vector<TerminalCell> cells;
        int startX = 0;
        int endX = -1;
        bool appendNewline = true;
    };

    void AppendTrimmedCellRange(std::wstring& out, const TerminalCell* row, int rowSize,
                                int startX, int endX, bool appendNewline)
    {
        if (startX > endX || rowSize <= 0 || !row)
        {
            if (appendNewline)
                out += L"\r\n";
            return;
        }

        if (startX < 0) startX = 0;
        if (endX >= rowSize) endX = rowSize - 1;
        if (startX > endX)
        {
            if (appendNewline)
                out += L"\r\n";
            return;
        }

        const size_t base = out.size();
        for (int x = startX; x <= endX; ++x)
        {
            const TerminalCell& c = row[x];
            if (!c.isWideTrailer)
                out.push_back(c.ch);
        }
        while (out.size() > base && out.back() == L' ')
            out.pop_back();
        if (appendNewline)
            out += L"\r\n";
    }

    void AppendTrimmedCopiedRow(std::wstring& out, const TerminalTextRowCopy& row)
    {
        AppendTrimmedCellRange(out,
            row.cells.empty() ? nullptr : row.cells.data(),
            static_cast<int>(row.cells.size()),
            row.startX,
            row.endX,
            row.appendNewline);
    }

    bool IsTerminalWordBreak(wchar_t ch)
    {
        return ch <= L' ' || wcschr(L",.:;!?\"'()[]{}<>=+*|", ch) != nullptr;
    }

    int RowCharIndexAtColumn(const TerminalRowTextSnapshot& row, int x)
    {
        if (!row.valid || x < 0 || x >= static_cast<int>(row.charByColumn.size()))
            return -1;
        const int idx = row.charByColumn[static_cast<size_t>(x)];
        if (idx < 0 || idx >= static_cast<int>(row.text.size()))
            return -1;
        return idx;
    }

    bool RowHasWordAtColumn(const TerminalRowTextSnapshot& row, int x)
    {
        const int idx = RowCharIndexAtColumn(row, x);
        return idx >= 0 && !IsTerminalWordBreak(row.text[static_cast<size_t>(idx)]);
    }

    std::wstring RowWordAtColumn(const TerminalRowTextSnapshot& row, int x)
    {
        const int idx = RowCharIndexAtColumn(row, x);
        if (idx < 0)
            return L"";
        if (IsTerminalWordBreak(row.text[static_cast<size_t>(idx)]))
            return L"";

        int start = idx;
        int end = idx;
        while (start > 0 && !IsTerminalWordBreak(row.text[static_cast<size_t>(start - 1)]))
            --start;
        while (end < static_cast<int>(row.text.size()) - 1 && !IsTerminalWordBreak(row.text[static_cast<size_t>(end + 1)]))
            ++end;

        return row.text.substr(static_cast<size_t>(start), static_cast<size_t>(end - start + 1));
    }
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
    std::lock_guard<std::mutex> lock(mtx);
    MoveCursorLeftVisualUnlocked(n);
}

void TerminalBuffer::MoveCursorLeftVisualUnlocked(int n)
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
    std::lock_guard<std::mutex> lock(mtx);
    MoveCursorRightVisualUnlocked(n);
}

void TerminalBuffer::MoveCursorRightVisualUnlocked(int n)
{
    while (n-- > 0)
    {
        if (cursorX >= width - 1)
        {
            cursorX = width - 1;
            break;
        }

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
    std::lock_guard<std::mutex> lock(mtx);
    ClearCellPairAwareUnlocked(x, y);
}

void TerminalBuffer::ClearCellPairAwareUnlocked(int x, int y)
{
    if (x < 0 || x >= width || y < 0 || y >= height)
        return;

    TerminalCell& c = GetCell(x, y);

    if (c.isWideTrailer)
    {
        ClearSingleCellUnlocked(x, y);
        if (x - 1 >= 0)
            ClearSingleCellUnlocked(x - 1, y);
        return;
    }

    if (x + 1 < width && GetCell(x + 1, y).isWideTrailer)
    {
        ClearSingleCellUnlocked(x, y);
        ClearSingleCellUnlocked(x + 1, y);
        return;
    }

    ClearSingleCellUnlocked(x, y);
}

void TerminalBuffer::NormalizeCursorForWrite()
{
    std::lock_guard<std::mutex> lock(mtx);
    NormalizeCursorForWriteUnlocked();
}

void TerminalBuffer::NormalizeCursorForWriteUnlocked()
{
    if (cursorX < 0) cursorX = 0;
    if (cursorY < 0) cursorY = 0;
    if (cursorX >= width) cursorX = width - 1;
    if (cursorY >= height) cursorY = height - 1;

    if (GetCell(cursorX, cursorY).isWideTrailer && cursorX > 0)
        cursorX--;
}

void TerminalBuffer::Resize(int w, int h)
{
    std::lock_guard<std::mutex> lock(mtx);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w == width && h == height && !cells.empty()) return;

    const int oldWidth = width;
    const int oldHeight = height;
    const int oldOffset = rowOffset;
    std::vector<TerminalCell> oldCells = std::move(cells);

    std::vector<TerminalCell> newCells(static_cast<size_t>(w) * static_cast<size_t>(h),
        { L' ', defaultFg, defaultBg, false, false });

    int minW = (oldWidth < w) ? oldWidth : w;
    int minH = (oldHeight < h) ? oldHeight : h;

    if (!oldCells.empty() && oldWidth > 0 && oldHeight > 0)
    {
        for (int y = 0; y < minH; ++y)
        {
            const int oldPhysicalY = (oldOffset + y) % oldHeight;
            const size_t oldBase = static_cast<size_t>(oldPhysicalY) * static_cast<size_t>(oldWidth);
            const size_t newBase = static_cast<size_t>(y) * static_cast<size_t>(w);
            for (int x = 0; x < minW; ++x)
            {
                const size_t oldIdx = oldBase + static_cast<size_t>(x);
                if (oldIdx < oldCells.size())
                    newCells[newBase + static_cast<size_t>(x)] = oldCells[oldIdx];
            }
        }
    }

    width = w;
    height = h;
    rowOffset = 0;
    cells = std::move(newCells);
    dirtyRows.assign(static_cast<size_t>(height), 1);
    allRowsDirty = true;

    if (cursorX >= width) cursorX = width - 1;
    if (cursorY >= height) cursorY = height - 1;
    pendingWrap = false;
}


void TerminalBuffer::PutChar(wchar_t ch, COLORREF fg, COLORREF bg, bool bold)
{
    std::lock_guard<std::mutex> lock(mtx);
    PutCharUnlocked(ch, fg, bg, bold);
}

void TerminalBuffer::AppendText(std::wstring_view text, COLORREF fg, COLORREF bg, bool bold)
{
    if (text.empty())
        return;

    std::lock_guard<std::mutex> lock(mtx);
    for (wchar_t ch : text)
        PutCharUnlocked(ch, fg, bg, bold);
}

void TerminalBuffer::PutCharUnlocked(wchar_t ch, COLORREF fg, COLORREF bg, bool bold)
{
    if (ch == L'\r') { cursorX = 0; pendingWrap = false; return; }
    if (ch == L'\n') {
        cursorY++;
        if (cursorY >= height) { ScrollUpUnlocked(); cursorY = height - 1; }
        pendingWrap = false;
        return;
    }
    if (ch == 0x08) { pendingWrap = false; MoveCursorLeftVisualUnlocked(1); return; }
    if (ch == 0x09) {
        pendingWrap = false;
        int nextTab = (cursorX + 8) & ~7;
        while (cursorX < nextTab && cursorX < width - 1)
            MoveCursorRightVisualUnlocked(1);
        return;
    }
    if (ch < 0x20) return;

    NormalizeCursorForWriteUnlocked();

    if (pendingWrap) {
        cursorX = 0;
        cursorY++;
        if (cursorY >= height) { ScrollUpUnlocked(); cursorY = height - 1; }
        pendingWrap = false;
        NormalizeCursorForWriteUnlocked();
    }

    int cw = KTinCharWidth(ch);
    if (cw < 1) cw = 1;
    if (cw > 2) cw = 2;

    if (cursorX + cw > width) {
        cursorX = 0;
        cursorY++;
        if (cursorY >= height) { ScrollUpUnlocked(); cursorY = height - 1; }
        pendingWrap = false;
        NormalizeCursorForWriteUnlocked();
    }

    ClearCellPairAwareUnlocked(cursorX, cursorY);
    if (cw == 2 && cursorX + 1 < width)
        ClearCellPairAwareUnlocked(cursorX + 1, cursorY);

    TerminalCell& cell = GetCell(cursorX, cursorY);
    cell.ch = ch;
    cell.fg = fg;
    cell.bg = bg;
    cell.bold = bold;
    cell.isWideTrailer = false;
    MarkDirtyRowUnlocked(cursorY);

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
        MarkDirtyRowUnlocked(cursorY);
    }

    cursorX += cw;
    if (cursorX >= width) {
        cursorX = width - 1;
        pendingWrap = true;
    }
    else {
        pendingWrap = false;
    }
}

void TerminalBuffer::ScrollUp()
{
    std::lock_guard<std::mutex> lock(mtx);
    ScrollUpUnlocked();
}

void TerminalBuffer::ScrollUpUnlocked()
{
    if (width <= 0 || height <= 0)
        return;

    const bool wasLiveView = (scrollOffset == 0);
    const bool hadFullDirty = allRowsDirty;

    const size_t rowWidth = static_cast<size_t>(width);
    const size_t screenCells = rowWidth * static_cast<size_t>(height);
    const TerminalCell blank{ L' ', defaultFg, defaultBg, false, false };

    if (cells.size() < screenCells)
        cells.resize(screenCells, blank);
    if (rowOffset < 0 || rowOffset >= height)
        rowOffset = 0;

    const size_t topBase = RowBase(0);
    std::vector<TerminalCell> topRow(rowWidth, blank);
    if (topBase < cells.size())
    {
        const size_t available = std::min(rowWidth, cells.size() - topBase);
        std::copy_n(cells.begin() + topBase, available, topRow.begin());
    }
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

    rowOffset++;
    if (rowOffset >= height)
        rowOffset = 0;

    const size_t bottomBase = RowBase(height - 1);
    if (bottomBase < cells.size())
    {
        const size_t available = std::min(rowWidth, cells.size() - bottomBase);
        std::fill_n(cells.begin() + bottomBase, available, blank);
    }

    if (scrollOffset > 0) {
        scrollOffset++;
        if (scrollOffset > (int)history.size())
            scrollOffset = (int)history.size();
    }

    if (!wasLiveView)
        return;

    if (hadFullDirty || pendingLiveScrollRows >= height - 1)
    {
        MarkAllDirtyUnlocked();
        return;
    }

    if (static_cast<int>(dirtyRows.size()) != height)
        dirtyRows.assign(static_cast<size_t>(height), 0);

    for (int y = 1; y < height; ++y)
        dirtyRows[static_cast<size_t>(y - 1)] = dirtyRows[static_cast<size_t>(y)];
    dirtyRows[static_cast<size_t>(height - 1)] = 1;

    ++pendingLiveScrollRows;
    const int firstBottomDirty = std::max(0, height - pendingLiveScrollRows);
    for (int y = firstBottomDirty; y < height; ++y)
        dirtyRows[static_cast<size_t>(y)] = 1;
}

// ==============================================
// 외부 함수들
// ==============================================

void TerminalBuffer::ClearLog(bool clearAllBuffer)
{
    std::lock_guard<std::mutex> lock(mtx);

    if (clearAllBuffer)
    {
        history.clear();
    }
    else
    {
        const TerminalCell blank{ L' ', defaultFg, defaultBg, false, false };
        for (int y = 0; y < height; ++y)
        {
            const size_t base = RowBase(y);
            std::vector<TerminalCell> row;
            row.reserve(width > 0 ? static_cast<size_t>(width) : 0);
            for (int x = 0; x < width; ++x)
            {
                const size_t idx = base + static_cast<size_t>(x);
                row.push_back(idx < cells.size() ? cells[idx] : blank);
            }
            history.push_back(std::move(row));
            if (static_cast<int>(history.size()) > maxHistory)
                history.pop_front();
        }
    }

    const TerminalCell blank{ L' ', defaultFg, defaultBg, false, false };
    std::fill(cells.begin(), cells.end(), blank);
    rowOffset = 0;
    cursorX = 0;
    cursorY = 0;
    scrollOffset = 0;
    pendingWrap = false;
    hasSelection = false;
    MarkAllDirtyUnlocked();
}

void ClearLogWindow(bool clearAllBuffer)
{
    if (!g_app || !g_app->termBuffer) return;

    g_app->termBuffer->ClearLog(clearAllBuffer);

    if (g_app->hwndLog)
        InvalidateRect(g_app->hwndLog, nullptr, FALSE);
}

void ResetHistoryBrowse()
{
    if (!g_app) return;
    g_app->historyBrowseIndex = -1;
}

void TerminalBuffer::ResetHistoryBrowse()
{
    ::ResetHistoryBrowse();
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
        TerminalBufferMetrics metrics = g_app->termBuffer->GetMetrics();
        size.X = (SHORT)metrics.width;
        size.Y = (SHORT)metrics.height;
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

void TerminalBuffer::ClearSelection()
{
    std::lock_guard<std::mutex> lock(mtx);
    ClearSelectionUnlocked();
}

void TerminalBuffer::ClearSelectionUnlocked()
{
    if (hasSelection)
        MarkSelectionDirtyUnlocked(selStartX, selStartY, selEndX, selEndY);
    hasSelection = false;
}

void TerminalBuffer::SetSelectionStart(int x, int y)
{
    std::lock_guard<std::mutex> lock(mtx);
    if (hasSelection)
        MarkSelectionDirtyUnlocked(selStartX, selStartY, selEndX, selEndY);
    selStartX = x;
    selStartY = y;
    selEndX = x;
    selEndY = y;
    hasSelection = true;
    MarkSelectionDirtyUnlocked(selStartX, selStartY, selEndX, selEndY);
}

void TerminalBuffer::SetSelectionEnd(int x, int y)
{
    std::lock_guard<std::mutex> lock(mtx);
    if (hasSelection)
        MarkSelectionDirtyUnlocked(selStartX, selStartY, selEndX, selEndY);
    selEndX = x;
    selEndY = y;
    hasSelection = true;
    MarkSelectionDirtyUnlocked(selStartX, selStartY, selEndX, selEndY);
}

void TerminalBuffer::SetSelectionRange(int startX, int startY, int endX, int endY)
{
    std::lock_guard<std::mutex> lock(mtx);
    if (hasSelection)
        MarkSelectionDirtyUnlocked(selStartX, selStartY, selEndX, selEndY);
    selStartX = startX;
    selStartY = startY;
    selEndX = endX;
    selEndY = endY;
    hasSelection = true;
    MarkSelectionDirtyUnlocked(selStartX, selStartY, selEndX, selEndY);
}

bool TerminalBuffer::IsSelected(int x, int absY)
{
    std::lock_guard<std::mutex> lock(mtx);
    return IsSelectedUnlocked(x, absY);
}

bool TerminalBuffer::IsSelectedUnlocked(int x, int absY) const
{
    if (!hasSelection) return false;
    int sy1 = selStartY, sx1 = selStartX, sy2 = selEndY, sx2 = selEndX;
    if (sy1 > sy2 || (sy1 == sy2 && sx1 > sx2)) { std::swap(sy1, sy2); std::swap(sx1, sx2); }
    if (absY < sy1 || absY > sy2) return false;
    if (sy1 == sy2) return x >= sx1 && x <= sx2;
    if (absY == sy1) return x >= sx1;
    if (absY == sy2) return x <= sx2;
    return true;
}

std::wstring TerminalBuffer::GetSelectedText()
{
    std::vector<TerminalTextRowCopy> rows;
    size_t reserveChars = 0;

    {
        std::lock_guard<std::mutex> lock(mtx);
        if (!hasSelection || width <= 0 || height <= 0)
            return L"";

        int sy1 = selStartY, sx1 = selStartX, sy2 = selEndY, sx2 = selEndX;
        if (sy1 > sy2 || (sy1 == sy2 && sx1 > sx2))
        {
            std::swap(sy1, sy2);
            std::swap(sx1, sx2);
        }

        sx1 = ClampInt(sx1, 0, width - 1);
        sx2 = ClampInt(sx2, 0, width - 1);

        const int histRows = static_cast<int>(history.size());
        const int maxAbsRow = histRows + height;
        if (sy2 < 0 || sy1 >= maxAbsRow)
            return L"";

        sy1 = ClampInt(sy1, 0, maxAbsRow - 1);
        sy2 = ClampInt(sy2, 0, maxAbsRow - 1);
        const int selectedRows = sy2 - sy1 + 1;
        rows.reserve(selectedRows > 0 ? static_cast<size_t>(selectedRows) : 0);
        reserveChars = static_cast<size_t>(selectedRows) * static_cast<size_t>(width + 2);

        const TerminalCell blank{ L' ', defaultFg, defaultBg, false, false };
        for (int y = sy1; y <= sy2; ++y)
        {
            TerminalTextRowCopy rowCopy;
            rowCopy.startX = (y == sy1) ? sx1 : 0;
            rowCopy.endX = (y == sy2) ? sx2 : width - 1;
            rowCopy.appendNewline = (y < sy2);
            rowCopy.cells.assign(static_cast<size_t>(width), blank);

            if (y < histRows)
            {
                const auto& row = history[static_cast<size_t>(y)];
                const size_t count = std::min(static_cast<size_t>(width), row.size());
                if (count > 0)
                    std::copy_n(row.data(), count, rowCopy.cells.data());
            }
            else
            {
                const int liveY = y - histRows;
                if (liveY >= 0 && liveY < height)
                {
                    const size_t base = RowBase(liveY);
                    if (base < cells.size())
                    {
                        const size_t count = std::min(static_cast<size_t>(width), cells.size() - base);
                        if (count > 0)
                            std::copy_n(cells.data() + base, count, rowCopy.cells.data());
                    }
                }
            }
            rows.push_back(std::move(rowCopy));
        }
    }

    std::wstring res;
    res.reserve(reserveChars);
    for (const auto& row : rows)
        AppendTrimmedCopiedRow(res, row);
    return res;
}
std::wstring TerminalBuffer::GetWordAt(int x, int absY)
{
    return RowWordAtColumn(MakeRowTextSnapshot(absY, false), x);
}

bool TerminalBuffer::HasWordAt(int x, int absY)
{
    return RowHasWordAtColumn(MakeRowTextSnapshot(absY, false), x);
}
std::wstring TerminalBuffer::GetCurrentScreenText()
{
    std::vector<TerminalTextRowCopy> rows;
    size_t reserveChars = 0;

    {
        std::lock_guard<std::mutex> lock(mtx);
        if (width <= 0 || height <= 0)
            return L"";

        reserveChars = static_cast<size_t>(height) * static_cast<size_t>(width + 2);
        rows.reserve(static_cast<size_t>(height));
        const TerminalCell blank{ L' ', defaultFg, defaultBg, false, false };
        for (int y = 0; y < height; ++y)
        {
            TerminalTextRowCopy rowCopy;
            rowCopy.startX = 0;
            rowCopy.endX = width - 1;
            rowCopy.appendNewline = true;
            rowCopy.cells.assign(static_cast<size_t>(width), blank);

            const size_t base = RowBase(y);
            if (base < cells.size())
            {
                const size_t count = std::min(static_cast<size_t>(width), cells.size() - base);
                if (count > 0)
                    std::copy_n(cells.data() + base, count, rowCopy.cells.data());
            }
            rows.push_back(std::move(rowCopy));
        }
    }

    std::wstring res;
    res.reserve(reserveChars);
    for (const auto& row : rows)
        AppendTrimmedCopiedRow(res, row);
    return res;
}

std::wstring TerminalBuffer::GetHistoryText()
{
    std::vector<TerminalTextRowCopy> rows;
    size_t reserveChars = 0;

    {
        std::lock_guard<std::mutex> lock(mtx);
        const size_t avgWidth = width > 0 ? static_cast<size_t>(width + 2) : 2;
        reserveChars = static_cast<size_t>(history.size()) * avgWidth;
        rows.reserve(history.size());
        for (const auto& row : history)
        {
            TerminalTextRowCopy rowCopy;
            rowCopy.startX = 0;
            rowCopy.endX = static_cast<int>(row.size()) - 1;
            rowCopy.appendNewline = true;
            rowCopy.cells = row;
            rows.push_back(std::move(rowCopy));
        }
    }

    std::wstring res;
    res.reserve(reserveChars);
    for (const auto& row : rows)
        AppendTrimmedCopiedRow(res, row);
    return res;
}
size_t TerminalBuffer::RowBase(int y) const
{
    if (width <= 0 || height <= 0 || y < 0 || y >= height)
        return 0;
    int physicalY = rowOffset + y;
    if (physicalY >= height)
        physicalY -= height;
    return static_cast<size_t>(physicalY) * static_cast<size_t>(width);
}


void TerminalBuffer::MarkDirtyRow(int y)
{
    std::lock_guard<std::mutex> lock(mtx);
    MarkDirtyRowUnlocked(y);
}

void TerminalBuffer::MarkDirtyRowUnlocked(int y)
{
    if (y < 0 || y >= height)
        return;
    if (static_cast<int>(dirtyRows.size()) != height)
        dirtyRows.assign(static_cast<size_t>(height), 0);
    dirtyRows[static_cast<size_t>(y)] = 1;
}

void TerminalBuffer::MarkDirtyRange(int y1, int y2)
{
    std::lock_guard<std::mutex> lock(mtx);
    MarkDirtyRangeUnlocked(y1, y2);
}

void TerminalBuffer::MarkDirtyRangeUnlocked(int y1, int y2)
{
    if (height <= 0)
        return;
    if (y1 > y2)
        std::swap(y1, y2);
    y1 = ClampInt(y1, 0, height - 1);
    y2 = ClampInt(y2, 0, height - 1);
    for (int y = y1; y <= y2; ++y)
        MarkDirtyRowUnlocked(y);
}

void TerminalBuffer::MarkAbsRowRangeDirtyUnlocked(int absY1, int absY2)
{
    if (height <= 0)
        return;
    if (absY1 > absY2)
        std::swap(absY1, absY2);

    const int histRows = static_cast<int>(history.size());
    const int viewTop = histRows - scrollOffset;
    const int viewBottom = viewTop + height - 1;
    if (absY2 < viewTop || absY1 > viewBottom)
        return;

    const int firstRow = std::max(absY1, viewTop) - viewTop;
    const int lastRow = std::min(absY2, viewBottom) - viewTop;
    MarkDirtyRangeUnlocked(firstRow, lastRow);
}

void TerminalBuffer::MarkSelectionDirtyUnlocked(int startX, int startY, int endX, int endY)
{
    (void)startX;
    (void)endX;
    MarkAbsRowRangeDirtyUnlocked(startY, endY);
}

void TerminalBuffer::MarkAllDirty()
{
    std::lock_guard<std::mutex> lock(mtx);
    MarkAllDirtyUnlocked();
}

void TerminalBuffer::MarkAllDirtyUnlocked()
{
    pendingLiveScrollRows = 0;
    if (height <= 0)
        return;
    dirtyRows.assign(static_cast<size_t>(height), 1);
    allRowsDirty = true;
}

TerminalDirtyState TerminalBuffer::TakeDirtyState()
{
    std::lock_guard<std::mutex> lock(mtx);
    TerminalDirtyState state;
    if (height <= 0)
        return state;

    if (allRowsDirty || static_cast<int>(dirtyRows.size()) != height)
    {
        state.fullDirty = true;
        state.rows.reserve(static_cast<size_t>(height));
        for (int y = 0; y < height; ++y)
            state.rows.push_back(y);
    }
    else
    {
        state.liveScrollRows = ClampInt(pendingLiveScrollRows, 0, height - 1);
        for (int y = 0; y < height; ++y)
            if (dirtyRows[static_cast<size_t>(y)])
                state.rows.push_back(y);
    }

    dirtyRows.assign(static_cast<size_t>(height), 0);
    allRowsDirty = false;
    pendingLiveScrollRows = 0;
    return state;
}

std::vector<int> TerminalBuffer::TakeDirtyRows()
{
    return TakeDirtyState().rows;
}

TerminalCell& TerminalBuffer::GetCell(int x, int y) {
    thread_local TerminalCell dummy;
    if (x < 0 || x >= width || y < 0 || y >= height) {
        dummy = { L' ', defaultFg, defaultBg, false, false };
        return dummy;
    }
    const size_t idx = RowBase(y) + static_cast<size_t>(x);
    if (idx >= cells.size()) {
        dummy = { L' ', defaultFg, defaultBg, false, false };
        return dummy;
    }
    return cells[idx];
}

TerminalCell TerminalBuffer::GetViewCell(int x, int y) {
    std::lock_guard<std::mutex> lock(mtx);
    return GetViewCellUnlocked(x, y);
}

TerminalCell TerminalBuffer::GetViewCellUnlocked(int x, int y) const
{
    if (x < 0 || y < 0 || x >= width || y >= height)
        return { L' ', defaultFg, defaultBg, false, false };

    const int histSize = static_cast<int>(history.size());
    const int absRow = histSize + y - scrollOffset;
    if (absRow >= 0 && absRow < histSize) {
        const auto& row = history[absRow];
        if (x < static_cast<int>(row.size()))
            return row[x];
        return { L' ', defaultFg, defaultBg, false, false };
    }

    const int liveY = absRow - histSize;
    if (liveY >= 0 && liveY < height) {
        const size_t idx = RowBase(liveY) + static_cast<size_t>(x);
        if (idx < cells.size())
            return cells[idx];
    }
    return { L' ', defaultFg, defaultBg, false, false };
}

TerminalBufferMetrics TerminalBuffer::GetMetrics()
{
    std::lock_guard<std::mutex> lock(mtx);
    TerminalBufferMetrics metrics;
    metrics.width = width;
    metrics.height = height;
    metrics.historySize = static_cast<int>(history.size());
    metrics.scrollOffset = scrollOffset;
    return metrics;
}

bool TerminalBuffer::HasHistory()
{
    std::lock_guard<std::mutex> lock(mtx);
    return !history.empty();
}

TerminalFindSnapshot TerminalBuffer::MakeFindSnapshot()
{
    std::lock_guard<std::mutex> lock(mtx);

    TerminalFindSnapshot snap;
    snap.width = width;
    snap.height = height;
    snap.historySize = static_cast<int>(history.size());
    snap.scrollOffset = scrollOffset;
    snap.hasSelection = hasSelection;
    snap.selStartX = selStartX;
    snap.selStartY = selStartY;
    snap.selEndX = selEndX;
    snap.selEndY = selEndY;
    snap.defaultFg = defaultFg;
    snap.defaultBg = defaultBg;
    return snap;
}

TerminalRowTextSnapshot TerminalBuffer::MakeRowTextSnapshot(int absY, bool foldCase)
{
    std::lock_guard<std::mutex> lock(mtx);

    TerminalRowTextSnapshot snap;
    snap.width = width;
    snap.height = height;
    snap.historySize = static_cast<int>(history.size());
    snap.absY = absY;

    const int totalRows = snap.historySize + height;
    if (width <= 0 || height <= 0 || absY < 0 || absY >= totalRows)
        return snap;

    snap.valid = true;
    snap.text.reserve(static_cast<size_t>(width));
    snap.xByChar.reserve(static_cast<size_t>(width));
    snap.charByColumn.assign(static_cast<size_t>(width), -1);

    const TerminalCell blank{ L' ', defaultFg, defaultBg, false, false };
    const TerminalCell* row = nullptr;
    int rowSize = 0;
    if (absY < snap.historySize)
    {
        const auto& histRow = history[static_cast<size_t>(absY)];
        row = histRow.data();
        rowSize = static_cast<int>(histRow.size());
    }
    else
    {
        const int liveY = absY - snap.historySize;
        if (liveY >= 0 && liveY < height)
        {
            const size_t base = RowBase(liveY);
            if (base < cells.size())
            {
                row = cells.data() + base;
                rowSize = static_cast<int>(std::min<size_t>(static_cast<size_t>(width), cells.size() - base));
            }
        }
    }

    for (int x = 0; x < width; ++x)
    {
        const TerminalCell& c = (row && x < rowSize) ? row[x] : blank;
        if (c.isWideTrailer)
        {
            if (x > 0)
                snap.charByColumn[static_cast<size_t>(x)] = snap.charByColumn[static_cast<size_t>(x - 1)];
            continue;
        }

        const int charIndex = static_cast<int>(snap.text.size());
        snap.charByColumn[static_cast<size_t>(x)] = charIndex;

        wchar_t ch = c.ch;
        if (foldCase)
            ch = static_cast<wchar_t>(std::towlower(ch));
        snap.text.push_back(ch);
        snap.xByChar.push_back(x);
    }

    return snap;
}

void TerminalBuffer::SelectAndRevealRange(int startX, int startY, int endX, int endY)
{
    std::lock_guard<std::mutex> lock(mtx);

    if (hasSelection)
        MarkSelectionDirtyUnlocked(selStartX, selStartY, selEndX, selEndY);

    const int oldOffset = scrollOffset;
    selStartX = startX;
    selStartY = startY;
    selEndX = endX;
    selEndY = endY;
    hasSelection = true;

    const int viewTop = static_cast<int>(history.size()) - scrollOffset;
    const int viewBottom = viewTop + height - 1;
    if (startY < viewTop || startY > viewBottom)
    {
        scrollOffset = static_cast<int>(history.size()) - startY + (height / 2);
        if (scrollOffset < 0)
            scrollOffset = 0;
        if (scrollOffset > static_cast<int>(history.size()))
            scrollOffset = static_cast<int>(history.size());
    }

    if (scrollOffset != oldOffset)
        MarkAllDirtyUnlocked();
    else
        MarkSelectionDirtyUnlocked(selStartX, selStartY, selEndX, selEndY);
}

void TerminalBuffer::SetDefaultColors(COLORREF newBg, COLORREF newFg)
{
    std::lock_guard<std::mutex> lock(mtx);
    defaultBg = newBg;
    defaultFg = newFg;
}

void TerminalBuffer::RecolorExistingTheme(COLORREF oldBg, COLORREF oldFg, COLORREF newBg, COLORREF newFg, bool (*isKnownBack)(COLORREF))
{
    std::lock_guard<std::mutex> lock(mtx);

    const COLORREF prevDefaultBg = defaultBg;
    const COLORREF prevDefaultFg = defaultFg;
    defaultBg = newBg;
    defaultFg = newFg;

    if (oldBg == newBg && oldFg == newFg && prevDefaultBg == newBg && prevDefaultFg == newFg)
        return;

    auto recolorCell = [&](TerminalCell& c)
    {
        const bool bgCandidate =
            (c.bg == oldBg) ||
            (c.bg == prevDefaultBg) ||
            (isKnownBack && isKnownBack(c.bg));
        if (bgCandidate)
            c.bg = newBg;

        if (c.fg == oldFg || c.fg == prevDefaultFg)
            c.fg = newFg;
    };

    for (auto& c : cells)
        recolorCell(c);
    for (auto& row : history)
        for (auto& c : row)
            recolorCell(c);

    MarkAllDirtyUnlocked();
}

void TerminalBuffer::RecolorTheme(COLORREF oldBg, COLORREF oldFg, COLORREF newBg, COLORREF newFg, bool (*isKnownBack)(COLORREF))
{
    RecolorExistingTheme(oldBg, oldFg, newBg, newFg, isKnownBack);
}

TerminalRenderSnapshot TerminalBuffer::MakeRenderSnapshot(int firstRow, int lastRow)
{
    std::lock_guard<std::mutex> lock(mtx);

    TerminalRenderSnapshot snap;
    snap.width = width;
    snap.height = height;
    snap.scrollOffset = scrollOffset;
    snap.historySize = static_cast<int>(history.size());
    snap.hasSelection = hasSelection;
    snap.selStartX = selStartX;
    snap.selStartY = selStartY;
    snap.selEndX = selEndX;
    snap.selEndY = selEndY;
    snap.defaultFg = defaultFg;
    snap.defaultBg = defaultBg;

    if (width <= 0 || height <= 0)
        return snap;

    firstRow = ClampInt(firstRow, 0, height - 1);
    lastRow = ClampInt(lastRow, 0, height - 1);
    if (firstRow > lastRow)
        std::swap(firstRow, lastRow);

    snap.firstRow = firstRow;
    snap.lastRow = lastRow;
    const size_t rowCount = static_cast<size_t>(lastRow - firstRow + 1);
    const size_t rowWidth = static_cast<size_t>(width);
    const TerminalCell blank{ L' ', defaultFg, defaultBg, false, false };
    snap.cells.assign(rowCount * rowWidth, blank);

    const int histSize = static_cast<int>(history.size());
    for (int y = firstRow; y <= lastRow; ++y)
    {
        TerminalCell* dst = snap.cells.data() + static_cast<size_t>(y - firstRow) * rowWidth;
        const int absRow = histSize + y - scrollOffset;
        if (absRow >= 0 && absRow < histSize)
        {
            const auto& src = history[static_cast<size_t>(absRow)];
            const size_t count = std::min(rowWidth, src.size());
            if (count > 0)
                std::copy_n(src.data(), count, dst);
            continue;
        }

        const int liveY = absRow - histSize;
        if (liveY >= 0 && liveY < height)
        {
            const size_t base = RowBase(liveY);
            if (base < cells.size())
            {
                const size_t count = std::min(rowWidth, cells.size() - base);
                if (count > 0)
                    std::copy_n(cells.data() + base, count, dst);
            }
        }
    }

    return snap;
}

void TerminalBuffer::DoScroll(int lines) {
    std::lock_guard<std::mutex> lock(mtx);
    int oldOffset = scrollOffset;
    scrollOffset += lines;
    if (scrollOffset < 0) scrollOffset = 0;
    if (scrollOffset > (int)history.size()) scrollOffset = (int)history.size();
    if (scrollOffset != oldOffset)
        MarkAllDirtyUnlocked();
}

int TerminalBuffer::ViewRowToAbsRow(int row)
{
    std::lock_guard<std::mutex> lock(mtx);
    return static_cast<int>(history.size()) + row - scrollOffset;
}

void TerminalBuffer::SetScrollOffsetFromTrackPosition(int trackPos)
{
    std::lock_guard<std::mutex> lock(mtx);
    int oldOffset = scrollOffset;
    scrollOffset = static_cast<int>(history.size()) - trackPos;
    if (scrollOffset < 0) scrollOffset = 0;
    if (scrollOffset > static_cast<int>(history.size()))
        scrollOffset = static_cast<int>(history.size());
    if (scrollOffset != oldOffset)
        MarkAllDirtyUnlocked();
}

void TerminalBuffer::ScrollToHistoryTop()
{
    std::lock_guard<std::mutex> lock(mtx);
    const int oldOffset = scrollOffset;
    scrollOffset = static_cast<int>(history.size());
    if (scrollOffset != oldOffset)
        MarkAllDirtyUnlocked();
}

void TerminalBuffer::ScrollToLive()
{
    std::lock_guard<std::mutex> lock(mtx);
    const int oldOffset = scrollOffset;
    scrollOffset = 0;
    if (scrollOffset != oldOffset)
        MarkAllDirtyUnlocked();
}
void TerminalBuffer::ClearSingleCell(int x, int y)
{
    std::lock_guard<std::mutex> lock(mtx);
    ClearSingleCellUnlocked(x, y);
}

void TerminalBuffer::ClearSingleCellUnlocked(int x, int y)
{
    if (x < 0 || x >= width || y < 0 || y >= height)
        return;
    TerminalCell& c = GetCell(x, y);
    c.ch = L' ';
    c.fg = defaultFg;
    c.bg = defaultBg;
    c.bold = false;
    c.isWideTrailer = false;
    MarkDirtyRowUnlocked(y);
}

void TerminalBuffer::ClearLineRangePairAware(int y, int x1, int x2)
{
    std::lock_guard<std::mutex> lock(mtx);
    ClearLineRangePairAwareUnlocked(y, x1, x2);
}

void TerminalBuffer::ClearLineRangePairAwareUnlocked(int y, int x1, int x2)
{
    if (y < 0 || y >= height)
        return;
    if (x1 > x2)
        std::swap(x1, x2);
    if (x1 < 0)
        x1 = 0;
    if (x2 >= width)
        x2 = width - 1;
    for (int x = x1; x <= x2; ++x)
        ClearCellPairAwareUnlocked(x, y);
}
void TerminalBuffer::HandleCommand(char cmd, const std::string& params) {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<int> args; int val = 0; bool have = false;
    constexpr int kMaxAnsiArg = 1000000;
    for (char c : params) {
        if (c >= '0' && c <= '9') {
            if (val < kMaxAnsiArg) {
                val = val * 10 + (c - '0');
                if (val > kMaxAnsiArg) val = kMaxAnsiArg;
            }
            have = true;
        }
        else if (c == ';') { args.push_back(have ? val : 0); val = 0; have = false; }
    }
    args.push_back(have ? val : 0);
    pendingWrap = false;
    auto ArgOr = [&](size_t idx, int defVal) -> int { return (idx >= args.size() || args[idx] <= 0) ? defVal : args[idx]; };
    if (cmd == 'J') {
        int mode = args.empty() ? 0 : args[0];
        if (mode == 0) { ClearLineRangePairAwareUnlocked(cursorY, cursorX, width - 1); for (int y = cursorY + 1; y < height; ++y) ClearLineRangePairAwareUnlocked(y, 0, width - 1); }
        else if (mode == 1) { for (int y = 0; y < cursorY; ++y) ClearLineRangePairAwareUnlocked(y, 0, width - 1); ClearLineRangePairAwareUnlocked(cursorY, 0, cursorX); }
        else if (mode == 2) { for (int y = 0; y < height; ++y) ClearLineRangePairAwareUnlocked(y, 0, width - 1); cursorX = 0; cursorY = 0; scrollOffset = 0; }
    }
    else if (cmd == 'K') {
        int mode = args.empty() ? 0 : args[0];
        if (mode == 0) ClearLineRangePairAwareUnlocked(cursorY, cursorX, width - 1);
        else if (mode == 1) ClearLineRangePairAwareUnlocked(cursorY, 0, cursorX);
        else if (mode == 2) ClearLineRangePairAwareUnlocked(cursorY, 0, width - 1);
    }
    else if (cmd == 'H' || cmd == 'f') {
        int r = ArgOr(0, 1) - 1, c = ArgOr(1, 1) - 1;
        if (r < 0) r = 0;
        if (c < 0) c = 0;
        if (r >= height) r = height - 1;
        if (c >= width) c = width - 1;
        cursorY = r; cursorX = c; NormalizeCursorForWriteUnlocked();
    }
    else if (cmd == 'A') { cursorY -= ArgOr(0, 1); if (cursorY < 0) cursorY = 0; NormalizeCursorForWriteUnlocked(); }
    else if (cmd == 'B') { cursorY += ArgOr(0, 1); if (cursorY >= height) cursorY = height - 1; NormalizeCursorForWriteUnlocked(); }
    else if (cmd == 'C') { MoveCursorRightVisualUnlocked(ArgOr(0, 1)); NormalizeCursorForWriteUnlocked(); }
    else if (cmd == 'D') { MoveCursorLeftVisualUnlocked(ArgOr(0, 1)); NormalizeCursorForWriteUnlocked(); }
    else if (cmd == 'G' || cmd == '`') { int n = ArgOr(0, 1) - 1; if (n < 0) n = 0; if (n >= width) n = width - 1; cursorX = n; NormalizeCursorForWriteUnlocked(); }
    else if (cmd == 'd') { int n = ArgOr(0, 1) - 1; if (n < 0) n = 0; if (n >= height) n = height - 1; cursorY = n; NormalizeCursorForWriteUnlocked(); }
}
