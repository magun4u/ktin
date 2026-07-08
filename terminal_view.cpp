#include "main.h"

#include "highlight.h"
#include "resource.h"
#include "terminal_buffer.h"
#include "utils.h"

#include <windowsx.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>
#include "win_util.h"

static bool s_logDragging = false;
static POINT s_logMouseDownPt = { 0, 0 };
static POINT s_logLastMouseClientPt = { 0, 0 };
static const UINT_PTR ID_TIMER_LOG_DRAG_SCROLL = 30021;
static const UINT LOG_DRAG_SCROLL_INTERVAL_MS = 50;

static UniqueGdiObject s_logBackBitmap;
static int s_logBackWidth = 0;
static int s_logBackHeight = 0;

static bool EnsureLogBackBuffer(HDC hdc, int width, int height)
{
    if (!hdc || width <= 0 || height <= 0)
        return false;

    if (!s_logBackBitmap.IsValid() || s_logBackWidth != width || s_logBackHeight != height)
    {
        s_logBackBitmap.Reset(CreateCompatibleBitmap(hdc, width, height));
        if (!s_logBackBitmap.IsValid())
        {
            s_logBackWidth = 0;
            s_logBackHeight = 0;
            return false;
        }

        s_logBackWidth = width;
        s_logBackHeight = height;
    }

    return true;
}

static void ComputeTerminalOffsetFromLayout(HWND hwnd, const TerminalBufferMetrics& metrics, SIZE cell, int& offsetX, int& offsetY)
{
    offsetX = 0;
    offsetY = 0;
    if (!hwnd || !g_app)
        return;

    RECT rc{};
    GetClientRect(hwnd, &rc);

    const int clientW = RectWidth(rc);
    const int clientH = RectHeight(rc);
    const int gridW = metrics.width * cell.cx;
    const int gridH = metrics.height * cell.cy;

    int ml = max(0, g_app->termMarginLeft);
    int mr = max(0, g_app->termMarginRight);
    int mt = max(0, g_app->termMarginTop);
    int mb = max(0, g_app->termMarginBottom);

    if (ml + mr > clientW) { ml = 0; mr = 0; }
    if (mt + mb > clientH) { mt = 0; mb = 0; }

    const int areaW = max(0, clientW - ml - mr);
    const int areaH = max(0, clientH - mt - mb);

    if (g_app->termAlign == 0)
        offsetX = ml;
    else if (g_app->termAlign == 2)
        offsetX = max(ml, clientW - mr - gridW);
    else
        offsetX = ml + max(0, (areaW - gridW) / 2);

    if (gridH > areaH)
        offsetY = clientH - mb - gridH;
    else
        offsetY = mt + max(0, (areaH - gridH) / 2);
}

static bool GetTerminalRowInvalidateLayout(HWND hwnd, TerminalBufferMetrics& metrics, SIZE& cell, int& offsetX, int& offsetY)
{
    if (!hwnd || !IsWindow(hwnd) || !g_app || !g_app->termBuffer)
        return false;

    metrics = g_app->termBuffer->GetMetrics();
    if (metrics.width <= 0 || metrics.height <= 0)
        return false;

    cell = GetLogCellPixelSize(hwnd);
    if (cell.cx <= 0 || cell.cy <= 0)
        return false;

    ComputeTerminalOffsetFromLayout(hwnd, metrics, cell, offsetX, offsetY);
    return true;
}

static void InvalidateTerminalRowRange(HWND hwnd, int firstRow, int lastRow, BOOL erase)
{
    TerminalBufferMetrics metrics{};
    SIZE cell{};
    int offsetX = 0;
    int offsetY = 0;
    if (!GetTerminalRowInvalidateLayout(hwnd, metrics, cell, offsetX, offsetY))
        return;

    if (firstRow > lastRow)
        std::swap(firstRow, lastRow);
    firstRow = ClampInt(firstRow, 0, metrics.height - 1);
    lastRow = ClampInt(lastRow, 0, metrics.height - 1);

    RECT rangeRc{};
    rangeRc.left = offsetX;
    rangeRc.right = offsetX + metrics.width * cell.cx;
    rangeRc.top = offsetY + firstRow * cell.cy;
    rangeRc.bottom = offsetY + (lastRow + 1) * cell.cy;
    InvalidateRect(hwnd, &rangeRc, erase);
}

static void InvalidateTerminalRows(HWND hwnd, const std::vector<int>& rows, BOOL erase)
{
    if (rows.empty())
        return;

    TerminalBufferMetrics metrics{};
    SIZE cell{};
    int offsetX = 0;
    int offsetY = 0;
    if (!GetTerminalRowInvalidateLayout(hwnd, metrics, cell, offsetX, offsetY))
        return;

    int runStart = -1;
    int runEnd = -1;
    auto flush = [&]()
    {
        if (runStart >= 0)
        {
            RECT rangeRc{};
            rangeRc.left = offsetX;
            rangeRc.right = offsetX + metrics.width * cell.cx;
            rangeRc.top = offsetY + runStart * cell.cy;
            rangeRc.bottom = offsetY + (runEnd + 1) * cell.cy;
            InvalidateRect(hwnd, &rangeRc, erase);
        }
        runStart = -1;
        runEnd = -1;
    };

    for (int row : rows)
    {
        if (row < 0 || row >= metrics.height)
            continue;
        if (runStart < 0)
        {
            runStart = row;
            runEnd = row;
        }
        else if (row == runEnd + 1)
        {
            runEnd = row;
        }
        else
        {
            flush();
            runStart = row;
            runEnd = row;
        }
    }
    flush();
}

void InvalidateTerminalAllRows(HWND hwnd, BOOL erase)
{
    if (!hwnd || !IsWindow(hwnd) || !g_app || !g_app->termBuffer)
        return;
    TerminalBufferMetrics metrics = g_app->termBuffer->GetMetrics();
    if (metrics.height <= 0)
        return;
    InvalidateTerminalRowRange(hwnd, 0, metrics.height - 1, erase);
}

void InvalidateTerminalDirtyRows(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd) || !g_app || !g_app->termBuffer)
        return;

    TerminalDirtyState state = g_app->termBuffer->TakeDirtyState();
    if (state.rows.empty() && state.liveScrollRows <= 0)
        return;

    TerminalBufferMetrics metrics{};
    SIZE cell{};
    int offsetX = 0;
    int offsetY = 0;
    if (!GetTerminalRowInvalidateLayout(hwnd, metrics, cell, offsetX, offsetY))
        return;

    if (!state.fullDirty && state.liveScrollRows > 0)
    {
        RECT scrollRc{};
        scrollRc.left = offsetX;
        scrollRc.top = offsetY;
        scrollRc.right = offsetX + metrics.width * cell.cx;
        scrollRc.bottom = offsetY + metrics.height * cell.cy;

        const int deltaY = -state.liveScrollRows * cell.cy;
        ScrollWindowEx(hwnd, 0, deltaY, &scrollRc, &scrollRc, nullptr, nullptr, SW_INVALIDATE);
    }

    InvalidateTerminalRows(hwnd, state.rows, FALSE);
}


static int ViewRowToAbsRowFromMetrics(const TerminalBufferMetrics& metrics, int row)
{
    return metrics.historySize + row - metrics.scrollOffset;
}

static bool SnapshotSelectedRangeForRow(const TerminalRenderSnapshot& snap, int absY, int width, int& startX, int& endX)
{
    startX = 0;
    endX = -1;
    if (!snap.hasSelection || width <= 0)
        return false;

    int sy1 = snap.selStartY;
    int sx1 = snap.selStartX;
    int sy2 = snap.selEndY;
    int sx2 = snap.selEndX;
    if (sy1 > sy2 || (sy1 == sy2 && sx1 > sx2))
    {
        std::swap(sy1, sy2);
        std::swap(sx1, sx2);
    }

    if (absY < sy1 || absY > sy2)
        return false;

    startX = (absY == sy1) ? sx1 : 0;
    endX = (absY == sy2) ? sx2 : width - 1;
    startX = ClampInt(startX, 0, width - 1);
    endX = ClampInt(endX, 0, width - 1);
    return startX <= endX;
}

struct GlyphMeasureCacheEntry
{
    HFONT font = nullptr;
    wchar_t ch = 0;
    int width = 0;
    bool valid = false;
};

static int MeasureGlyphWidthCached(HDC hdc, wchar_t ch)
{
    static GlyphMeasureCacheEntry cache[512];
    static size_t nextSlot = 0;

    HFONT font = reinterpret_cast<HFONT>(GetCurrentObject(hdc, OBJ_FONT));
    for (const auto& entry : cache)
    {
        if (entry.valid && entry.font == font && entry.ch == ch)
            return entry.width;
    }

    wchar_t out[2] = { ch, 0 };
    SIZE glyphSz = {};
    if (!GetTextExtentPoint32W(hdc, out, 1, &glyphSz))
        glyphSz.cx = 0;

    cache[nextSlot] = { font, ch, glyphSz.cx, true };
    nextSlot = (nextSlot + 1) % (sizeof(cache) / sizeof(cache[0]));
    return glyphSz.cx;
}

struct CursorWordHitCache
{
    int col = -1;
    int absY = -1;
    int width = 0;
    int historySize = 0;
    int scrollOffset = 0;
    ULONGLONG tick = 0;
    bool hasWord = false;
    bool valid = false;
};

static CursorWordHitCache s_cursorWordHitCache;

static bool CachedWordHitAt(int col, int absY, const TerminalBufferMetrics& metrics)
{
    const ULONGLONG now = GetTickCount64();
    if (s_cursorWordHitCache.valid &&
        s_cursorWordHitCache.col == col &&
        s_cursorWordHitCache.absY == absY &&
        s_cursorWordHitCache.width == metrics.width &&
        s_cursorWordHitCache.historySize == metrics.historySize &&
        s_cursorWordHitCache.scrollOffset == metrics.scrollOffset &&
        now - s_cursorWordHitCache.tick <= 150)
    {
        return s_cursorWordHitCache.hasWord;
    }

    const bool hasWord = (g_app && g_app->termBuffer &&
        g_app->termBuffer->HasWordAt(col, absY));

    s_cursorWordHitCache.col = col;
    s_cursorWordHitCache.absY = absY;
    s_cursorWordHitCache.width = metrics.width;
    s_cursorWordHitCache.historySize = metrics.historySize;
    s_cursorWordHitCache.scrollOffset = metrics.scrollOffset;
    s_cursorWordHitCache.tick = now;
    s_cursorWordHitCache.hasWord = hasWord;
    s_cursorWordHitCache.valid = true;
    return hasWord;
}

static int GetLogDragAutoScrollLines(int y, int viewTop, int viewBottom, int cellHeight)
{
    if (cellHeight <= 0)
        cellHeight = 1;

    if (y < viewTop)
    {
        int dist = viewTop - y;
        if (dist > cellHeight * 4) return 4;
        if (dist > cellHeight * 2) return 2;
        return 1;
    }

    if (y > viewBottom)
    {
        int dist = y - viewBottom;
        if (dist > cellHeight * 4) return -4;
        if (dist > cellHeight * 2) return -2;
        return -1;
    }

    return 0;
}

static bool UpdateLogDragSelectionFromClientPoint(HWND hwnd, int x, int y, bool allowAutoScroll)
{
    if (!g_app || !g_app->termBuffer)
        return false;

    TerminalBufferMetrics metrics{};
    SIZE cell{};
    int offsetX = 0;
    int offsetY = 0;
    if (!GetTerminalRowInvalidateLayout(hwnd, metrics, cell, offsetX, offsetY))
        return false;

    const int viewTop = offsetY;
    const int viewBottom = offsetY + metrics.height * cell.cy - 1;

    if (allowAutoScroll)
    {
        int lines = GetLogDragAutoScrollLines(y, viewTop, viewBottom, cell.cy);
        if (lines != 0)
        {
            g_app->termBuffer->DoScroll(lines);
            metrics = g_app->termBuffer->GetMetrics();
        }
    }

    int col = (x - offsetX) / cell.cx;
    int row = (y - offsetY) / cell.cy;

    col = ClampInt(col, 0, metrics.width - 1);
    row = ClampInt(row, 0, metrics.height - 1);

    const int absY = ViewRowToAbsRowFromMetrics(metrics, row);
    g_app->termBuffer->SetSelectionEnd(col, absY);
    InvalidateTerminalDirtyRows(hwnd);
    return true;
}

LRESULT CALLBACK TerminalWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        ScopedPaintDC paint(hwnd);
        HDC hdc = paint.Get();
        if (!hdc) return 0;

        RECT rc;
        GetClientRect(hwnd, &rc);
        RECT paintRc = paint.PaintStruct().rcPaint;
        if (IsRectEmpty(&paintRc))
            paintRc = rc;

        UniqueMemoryDC memDc(hdc);
        HGDIOBJ oldBackBitmap = nullptr;
        HDC hdcDraw = hdc;
        bool useBackBuffer = false;

        const int bitmapWidth = RectWidthAtLeast(rc, 1);
        const int bitmapHeight = RectHeightAtLeast(rc, 1);
        if (memDc && EnsureLogBackBuffer(hdc, bitmapWidth, bitmapHeight))
        {
            oldBackBitmap = SelectObject(memDc.Get(), s_logBackBitmap.Get());
            if (oldBackBitmap && oldBackBitmap != HGDI_ERROR)
            {
                hdcDraw = memDc.Get();
                useBackBuffer = true;
            }
            else
            {
                oldBackBitmap = nullptr;
            }
        }

        COLORREF bgColor = g_app ? g_app->logStyle.backColor : RGB(0, 0, 0);
        if (!FillSolidRect(hdcDraw, paintRc, bgColor))
            FillRect(hdcDraw, &paintRc, GetSysColorBrush(COLOR_WINDOW));

        if (g_app && g_app->termBuffer && g_app->hFontLog)
        {
            TerminalBufferMetrics metrics = g_app->termBuffer->GetMetrics();
            SIZE cell = GetLogCellPixelSize(hwnd);

            int offsetX = 0;
            int offsetY = 0;
            ComputeTerminalOffsetFromLayout(hwnd, metrics, cell, offsetX, offsetY);

            ScopedSelectObject selectedFont(hdcDraw, g_app->hFontLog);
            SetBkMode(hdcDraw, TRANSPARENT);
            SetTextAlign(hdcDraw, TA_LEFT | TA_TOP | TA_NOUPDATECP);

            const int visibleWidth = metrics.width;
            const int visibleHeight = metrics.height;

            if (cell.cx > 0 && cell.cy > 0 && visibleWidth > 0 && visibleHeight > 0)
            {
                RECT terminalRc{};
                terminalRc.left = offsetX;
                terminalRc.top = offsetY;
                terminalRc.right = offsetX + visibleWidth * cell.cx;
                terminalRc.bottom = offsetY + visibleHeight * cell.cy;

                RECT clippedPaint{};
                if (IntersectRect(&clippedPaint, &paintRc, &terminalRc))
                {
                    int firstRow = (clippedPaint.top - offsetY) / cell.cy;
                int lastRow = (clippedPaint.bottom - 1 - offsetY) / cell.cy;
                firstRow = ClampInt(firstRow, 0, visibleHeight - 1);
                lastRow = ClampInt(lastRow, 0, visibleHeight - 1);

                TerminalRenderSnapshot snap = g_app->termBuffer->MakeRenderSnapshot(firstRow, lastRow);
                const int termWidth = snap.width;
                const int termHeight = snap.height;

                if (termWidth > 0 && termHeight > 0)
                {
                    std::wstring textRun;
                    std::vector<int> textDx;
                    textRun.reserve(static_cast<size_t>(termWidth));
                    textDx.reserve(static_cast<size_t>(termWidth));

                    for (int y = firstRow; y <= lastRow; ++y)
                    {
                        const int rowIndex = y - snap.firstRow;
                        const size_t rowBase = static_cast<size_t>(rowIndex) * static_cast<size_t>(termWidth);
                        if (rowIndex < 0 || rowBase + static_cast<size_t>(termWidth) > snap.cells.size())
                            continue;

                        const TerminalCell* rowCells = snap.cells.data() + rowBase;
                        const int absY = snap.historySize + y - snap.scrollOffset;
                        const int rowTop = offsetY + y * cell.cy;

                        int spanStart = -1;
                        int spanRight = 0;
                        COLORREF spanBg = bgColor;
                        auto flushBgSpan = [&]()
                        {
                            if (spanStart < 0)
                                return;
                            RECT spanRc{};
                            spanRc.left = offsetX + spanStart * cell.cx;
                            spanRc.top = rowTop;
                            spanRc.right = spanRight;
                            spanRc.bottom = spanRc.top + cell.cy;
                            SetBkColor(hdcDraw, spanBg);
                            ExtTextOutW(hdcDraw, spanRc.left, spanRc.top,
                                ETO_OPAQUE, &spanRc, L"", 0, nullptr);
                            spanStart = -1;
                        };

                        for (int x = 0; x < termWidth; ++x)
                        {
                            const TerminalCell& c = rowCells[x];
                            if (c.isWideTrailer)
                                continue;

                            COLORREF drawBg = (c.bg == RGB(0, 0, 0)) ? bgColor : c.bg;
                            if (drawBg == bgColor)
                            {
                                flushBgSpan();
                                continue;
                            }

                            int cw = KTinCharWidth(c.ch);
                            if (cw < 1) cw = 1;
                            if (cw > 2) cw = 2;

                            const int left = offsetX + x * cell.cx;
                            const int right = left + cell.cx * cw;

                            if (spanStart < 0)
                            {
                                spanStart = x;
                                spanRight = right;
                                spanBg = drawBg;
                            }
                            else if (drawBg == spanBg && left == spanRight)
                            {
                                spanRight = right;
                            }
                            else
                            {
                                flushBgSpan();
                                spanStart = x;
                                spanRight = right;
                                spanBg = drawBg;
                            }
                        }
                        flushBgSpan();

                        int textRunStartX = -1;
                        COLORREF textRunFg = RGB(0, 0, 0);

                        auto flushTextRun = [&]()
                        {
                            if (textRun.empty() || textRunStartX < 0)
                                return;
                            SetTextColor(hdcDraw, textRunFg);
                            ExtTextOutW(hdcDraw, textRunStartX, rowTop, 0, nullptr,
                                textRun.data(), static_cast<UINT>(textRun.size()), textDx.data());
                            textRun.clear();
                            textDx.clear();
                            textRunStartX = -1;
                        };

                        for (int x = 0; x < termWidth; ++x)
                        {
                            const TerminalCell& c = rowCells[x];
                            if (c.isWideTrailer || c.ch == L' ')
                            {
                                flushTextRun();
                                continue;
                            }

                            int cw = KTinCharWidth(c.ch);
                            if (cw < 1) cw = 1;
                            if (cw > 2) cw = 2;

                            const int cellLeft = offsetX + x * cell.cx;

                            if (cw == 1)
                            {
                                const int expectedRunX = textRunStartX + static_cast<int>(textRun.size()) * cell.cx;
                                if (textRun.empty())
                                {
                                    textRunStartX = cellLeft;
                                    textRunFg = c.fg;
                                }
                                else if (c.fg != textRunFg || cellLeft != expectedRunX)
                                {
                                    flushTextRun();
                                    textRunStartX = cellLeft;
                                    textRunFg = c.fg;
                                }
                                textRun.push_back(c.ch);
                                textDx.push_back(cell.cx);
                                continue;
                            }

                            flushTextRun();

                            wchar_t out[2] = { c.ch, 0 };
                            SetTextColor(hdcDraw, c.fg);

                            int drawX = cellLeft;
                            const int glyphWidth = MeasureGlyphWidthCached(hdcDraw, c.ch);
                            if (glyphWidth > 0)
                            {
                                int expectedWidth = cell.cx * 2;
                                if (glyphWidth < expectedWidth)
                                    drawX = cellLeft + (expectedWidth - glyphWidth) / 2;
                            }

                            TextOutW(hdcDraw, drawX, rowTop, out, 1);
                        }
                        flushTextRun();

                        int selStart = 0;
                        int selEnd = -1;
                        if (SnapshotSelectedRangeForRow(snap, absY, termWidth, selStart, selEnd))
                        {
                            int selSpanStart = -1;
                            int selSpanRight = 0;
                            auto flushSelectionSpan = [&]()
                            {
                                if (selSpanStart < 0)
                                    return;
                                RECT selRc{};
                                selRc.left = offsetX + selSpanStart * cell.cx;
                                selRc.top = rowTop;
                                selRc.right = selSpanRight;
                                selRc.bottom = selRc.top + cell.cy;
                                InvertRect(hdcDraw, &selRc);
                                selSpanStart = -1;
                            };

                            for (int x = selStart; x <= selEnd; ++x)
                            {
                                const TerminalCell& c = rowCells[x];
                                if (c.isWideTrailer)
                                    continue;

                                int cw = KTinCharWidth(c.ch);
                                if (cw < 1) cw = 1;
                                if (cw > 2) cw = 2;

                                const int left = offsetX + x * cell.cx;
                                const int right = left + cell.cx * cw;
                                if (selSpanStart < 0)
                                {
                                    selSpanStart = x;
                                    selSpanRight = right;
                                }
                                else if (left == selSpanRight)
                                {
                                    selSpanRight = right;
                                }
                                else
                                {
                                    flushSelectionSpan();
                                    selSpanStart = x;
                                    selSpanRight = right;
                                }
                            }
                            flushSelectionSpan();
                        }
                    }
                }
                }
            }
        }

        if (useBackBuffer)
            BitBlt(hdc, paintRc.left, paintRc.top, RectWidth(paintRc), RectHeight(paintRc),
                   hdcDraw, paintRc.left, paintRc.top, SRCCOPY);

        if (oldBackBitmap)
            SelectObject(memDc.Get(), oldBackBitmap);
        return 0;
    }

    case WM_VSCROLL:
    {
        if (!g_app || !g_app->termBuffer) return 0;
        int action = LOWORD(wParam);
        switch (action) {
        case SB_LINEUP: g_app->termBuffer->DoScroll(1); break;
        case SB_LINEDOWN: g_app->termBuffer->DoScroll(-1); break;
        case SB_PAGEUP: { TerminalBufferMetrics metrics = g_app->termBuffer->GetMetrics(); g_app->termBuffer->DoScroll(metrics.height / 2); break; }
        case SB_PAGEDOWN: { TerminalBufferMetrics metrics = g_app->termBuffer->GetMetrics(); g_app->termBuffer->DoScroll(-(metrics.height / 2)); break; }
        case SB_THUMBTRACK: {
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;
            GetScrollInfo(hwnd, SB_VERT, &si);
            g_app->termBuffer->SetScrollOffsetFromTrackPosition(si.nTrackPos);
            break;
        }
        }
        InvalidateTerminalDirtyRows(hwnd);
        return 0;
    }

    case WM_SETCURSOR:
    {
        if (LOWORD(lParam) == HTCLIENT && g_app && g_app->termBuffer) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
            TerminalBufferMetrics metrics{};
            SIZE cell{};
            int offsetX = 0;
            int offsetY = 0;
            if (!GetTerminalRowInvalidateLayout(hwnd, metrics, cell, offsetX, offsetY))
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            const int gridRight = offsetX + metrics.width * cell.cx;
            const int gridBottom = offsetY + metrics.height * cell.cy;
            if (pt.x < offsetX || pt.y < offsetY || pt.x >= gridRight || pt.y >= gridBottom)
                return DefWindowProcW(hwnd, msg, wParam, lParam);

            int col = (pt.x - offsetX) / cell.cx;
            int row = (pt.y - offsetY) / cell.cy;
            col = ClampInt(col, 0, metrics.width - 1);
            row = ClampInt(row, 0, metrics.height - 1);
            const int absY = ViewRowToAbsRowFromMetrics(metrics, row);
            if (CachedWordHitAt(col, absY, metrics)) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    case WM_LBUTTONDOWN:
    {
        if (g_app && g_app->termBuffer) {
            SetCapture(hwnd);

            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            s_logLastMouseClientPt = { x, y };

            TerminalBufferMetrics metrics{};
            SIZE cell{};
            int offsetX = 0;
            int offsetY = 0;
            if (!GetTerminalRowInvalidateLayout(hwnd, metrics, cell, offsetX, offsetY))
                return 0;
            int col = (x - offsetX) / cell.cx;
            int row = (y - offsetY) / cell.cy;

            col = ClampInt(col, 0, metrics.width - 1);
            row = ClampInt(row, 0, metrics.height - 1);

            const int absY = ViewRowToAbsRowFromMetrics(metrics, row);
            g_app->termBuffer->SetSelectionStart(col, absY);
            s_logDragging = true;
            s_logMouseDownPt = { col, absY };
            StartWinTimer(hwnd, ID_TIMER_LOG_DRAG_SCROLL, LOG_DRAG_SCROLL_INTERVAL_MS);
            InvalidateTerminalDirtyRows(hwnd);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (s_logDragging && g_app && g_app->termBuffer)
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            s_logLastMouseClientPt = { x, y };
            UpdateLogDragSelectionFromClientPoint(hwnd, x, y, true);
        }

        return 0;
    }
    case WM_MOUSELEAVE:
    {
        if (g_app)
        {
            g_app->trackingMenuMouse = false;
            if (g_app->hotMenuIndex != -1)
            {
                g_app->hotMenuIndex = -1;
                InvalidateTerminalAllRows(hwnd, FALSE);
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (s_logDragging && g_app && g_app->termBuffer) {
            KillWinTimer(hwnd, ID_TIMER_LOG_DRAG_SCROLL);
            ReleaseCapture(); s_logDragging = false;
            int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
            s_logLastMouseClientPt = { x, y };

            TerminalBufferMetrics metrics{};
            SIZE cell{};
            int offsetX = 0;
            int offsetY = 0;
            if (!GetTerminalRowInvalidateLayout(hwnd, metrics, cell, offsetX, offsetY))
                return 0;
            int col = (x - offsetX) / cell.cx, row = (y - offsetY) / cell.cy;
            col = ClampInt(col, 0, metrics.width - 1);
            row = ClampInt(row, 0, metrics.height - 1);

            const int absY = ViewRowToAbsRowFromMetrics(metrics, row);
            g_app->termBuffer->SetSelectionEnd(col, absY);

            if (s_logMouseDownPt.x == col && s_logMouseDownPt.y == absY) {
                g_app->termBuffer->ClearSelection();
                std::wstring word = g_app->termBuffer->GetWordAt(col, absY);
                if (!word.empty()) {
                    SendTextToMud(word);
                    if (g_app->hwndEdit[g_app->activeEditIndex]) SetFocus(g_app->hwndEdit[g_app->activeEditIndex]);
                }
            }
            InvalidateTerminalDirtyRows(hwnd);
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
    {
        if (s_logDragging)
        {
            KillWinTimer(hwnd, ID_TIMER_LOG_DRAG_SCROLL);
            s_logDragging = false;
        }
        return 0;
    }

    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
    {
        UniqueMenu hMenu(CreatePopupMenu());
        if (hMenu.IsValid()) {
            // 터미널 우클릭 메뉴는 안정성을 위해 일반 문자열 메뉴로 표시합니다.
            if (g_app && g_app->menuHidden) {
                AppendMenuW(hMenu.Get(), MF_STRING, ID_LOG_SHOW_MENU, L"상단 메뉴 보이기");
                AppendMenuW(hMenu.Get(), MF_SEPARATOR, 0, nullptr);
            }

            AppendMenuW(hMenu.Get(), MF_STRING, ID_LOG_COPY, L"복사하기");

            POINT pt;
            GetCursorPos(&pt);

            HWND hOwner = (g_app && g_app->hwndMain) ? g_app->hwndMain : hwnd;
            SetForegroundWindow(hOwner);

            TrackPopupMenu(
                hMenu.Get(),
                TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
                pt.x, pt.y, 0, hOwner, nullptr);

            PostMessageW(hOwner, WM_NULL, 0, 0);
        }
        return 0;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == ID_LOG_COPY) {
            if (g_app && g_app->termBuffer) {

                std::wstring text = g_app->termBuffer->GetSelectedText();
                if (!text.empty())
                    SetClipboardUnicodeText(hwnd, text);
                g_app->termBuffer->ClearSelection();
                InvalidateTerminalDirtyRows(hwnd);
            }
            return 0;
        }
        else if (id == ID_LOG_SHOW_MENU) {
            if (g_app) {
                HWND target = g_app->hwndMain ? g_app->hwndMain : GetParent(hwnd);
                g_app->menuHidden = false;
                LayoutChildren(target);
                InvalidateRect(target, nullptr, FALSE);
            }
            return 0;
        }
        else if (id == ID_LOG_CLEAR_CHAT) {
            // ★ 추가: 채팅 캡처창 내용 깨끗하게 지우기
            if (g_app && g_app->hwndChat) {
                SetWindowTextW(g_app->hwndChat, L"");
            }
            return 0;
        }
        else {
            // ★ 터미널에서 처리하지 않는 명령(도킹, 숨기기 등)은 부모 창(MainWndProc)으로 즉시 패스!
            SendMessageW(GetParent(hwnd), WM_COMMAND, wParam, lParam);
            return 0;
        }
    }


    case WM_TIMER:
    {
        if (wParam == ID_TIMER_LOG_DRAG_SCROLL)
        {
            if (s_logDragging && g_app && g_app->termBuffer)
                UpdateLogDragSelectionFromClientPoint(hwnd, s_logLastMouseClientPt.x, s_logLastMouseClientPt.y, true);
            else
                KillWinTimer(hwnd, ID_TIMER_LOG_DRAG_SCROLL);

            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }


    case WM_MOUSEWHEEL:
    {
        if (g_app && g_app->termBuffer) {
            int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            g_app->termBuffer->DoScroll(zDelta > 0 ? 3 : -3);
            InvalidateTerminalDirtyRows(hwnd);
        }
        return 0;
    }

    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
        if (g_app && g_app->termBuffer)
            g_app->termBuffer->MarkAllDirty();
        InvalidateTerminalAllRows(hwnd, FALSE);
        return 0;
    default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

