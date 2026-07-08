#pragma once
#include "constants.h"
#include "types.h"

#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <cstddef>



struct TerminalCell {
    wchar_t ch = L' ';
    COLORREF fg = RGB(220, 220, 220);
    COLORREF bg = RGB(0, 0, 0);
    bool bold = false;
    bool isWideTrailer = false;
};

struct TerminalBufferMetrics {
    int width = 0;
    int height = 0;
    int historySize = 0;
    int scrollOffset = 0;
};


struct TerminalDirtyState {
    std::vector<int> rows;
    int liveScrollRows = 0;
    bool fullDirty = false;
};

struct TerminalRenderSnapshot {
    int width = 0;
    int height = 0;
    int scrollOffset = 0;
    int historySize = 0;
    int firstRow = 0;
    int lastRow = -1;
    bool hasSelection = false;
    int selStartX = 0;
    int selStartY = 0;
    int selEndX = 0;
    int selEndY = 0;
    COLORREF defaultFg = RGB(220, 220, 220);
    COLORREF defaultBg = RGB(0, 0, 0);
    std::vector<TerminalCell> cells;
};

struct TerminalFindSnapshot {
    int width = 0;
    int height = 0;
    int historySize = 0;
    int scrollOffset = 0;
    bool hasSelection = false;
    int selStartX = 0;
    int selStartY = 0;
    int selEndX = 0;
    int selEndY = 0;
    COLORREF defaultFg = RGB(220, 220, 220);
    COLORREF defaultBg = RGB(0, 0, 0);
};

struct TerminalRowTextSnapshot {
    int width = 0;
    int height = 0;
    int historySize = 0;
    int absY = -1;
    bool valid = false;
    std::wstring text;
    std::vector<int> xByChar;
    std::vector<int> charByColumn;
};

class TerminalBuffer {
public:
    TerminalBuffer(int w, int h);
    void Resize(int w, int h);
    void ClearSelection();
    void SetSelectionStart(int x, int y);
    void SetSelectionEnd(int x, int y);
    void SetSelectionRange(int startX, int startY, int endX, int endY);
    bool IsSelected(int x, int absY);
    std::wstring GetSelectedText();
    std::wstring GetWordAt(int x, int absY);
    bool HasWordAt(int x, int absY);
    std::wstring GetCurrentScreenText();
    std::wstring GetHistoryText();
    size_t RowBase(int y) const;
    void MarkDirtyRow(int y);
    void MarkDirtyRange(int y1, int y2);
    void MarkAllDirty();
    TerminalDirtyState TakeDirtyState();
    std::vector<int> TakeDirtyRows();
    TerminalCell GetViewCell(int x, int y);
    void DoScroll(int lines);
    int ViewRowToAbsRow(int row);
    void SetScrollOffsetFromTrackPosition(int trackPos);
    void ScrollToHistoryTop();
    void ScrollToLive();
    void ClearSingleCell(int x, int y);
    void ClearCellPairAware(int x, int y);
    void NormalizeCursorForWrite();
    void MoveCursorLeftVisual(int n = 1);
    void MoveCursorRightVisual(int n = 1);
    void ClearLineRangePairAware(int y, int x1, int x2);
    void PutChar(wchar_t ch, COLORREF fg, COLORREF bg, bool bold);
    void AppendText(std::wstring_view text, COLORREF fg, COLORREF bg, bool bold);
    void ScrollUp();
    TerminalBufferMetrics GetMetrics();
    bool HasHistory();
    TerminalRenderSnapshot MakeRenderSnapshot(int firstRow, int lastRow);
    TerminalFindSnapshot MakeFindSnapshot();
    TerminalRowTextSnapshot MakeRowTextSnapshot(int absY, bool foldCase);
    void SelectAndRevealRange(int startX, int startY, int endX, int endY);
    void ClearLog(bool clearAllBuffer);
    void SetDefaultColors(COLORREF newBg, COLORREF newFg);
    void RecolorTheme(COLORREF oldBg, COLORREF oldFg, COLORREF newBg, COLORREF newFg, bool (*isKnownBack)(COLORREF));
    void RecolorExistingTheme(COLORREF oldBg, COLORREF oldFg, COLORREF newBg, COLORREF newFg, bool (*isKnownBack)(COLORREF));
    void HandleCommand(char cmd, const std::string& params);
    void ResetHistoryBrowse();

private:
    int width = 80;
    int height = 24;
    int cursorX = 0;
    int cursorY = 0;
    std::vector<TerminalCell> cells;
    int rowOffset = 0;
    std::vector<unsigned char> dirtyRows;
    bool allRowsDirty = true;
    std::mutex mtx;
    COLORREF defaultBg = RGB(0, 0, 0);
    COLORREF defaultFg = RGB(220, 220, 220);
    std::deque<std::vector<TerminalCell>> history;
    int maxHistory = 5000;
    int scrollOffset = 0;
    bool pendingWrap = false;
    bool hasSelection = false;
    int selStartX = 0, selStartY = 0;
    int selEndX = 0, selEndY = 0;
    int pendingLiveScrollRows = 0;

    TerminalCell& GetCell(int x, int y);
    void MoveCursorLeftVisualUnlocked(int n = 1);
    void MoveCursorRightVisualUnlocked(int n = 1);
    void ClearSingleCellUnlocked(int x, int y);
    void ClearCellPairAwareUnlocked(int x, int y);
    void NormalizeCursorForWriteUnlocked();
    void ClearLineRangePairAwareUnlocked(int y, int x1, int x2);
    void PutCharUnlocked(wchar_t ch, COLORREF fg, COLORREF bg, bool bold);
    void ScrollUpUnlocked();
    void ClearSelectionUnlocked();
    void MarkDirtyRowUnlocked(int y);
    void MarkDirtyRangeUnlocked(int y1, int y2);
    void MarkAbsRowRangeDirtyUnlocked(int absY1, int absY2);
    void MarkSelectionDirtyUnlocked(int startX, int startY, int endX, int endY);
    void MarkAllDirtyUnlocked();
    bool IsSelectedUnlocked(int x, int absY) const;
    TerminalCell GetViewCellUnlocked(int x, int y) const;
};

bool ResizePseudoConsoleToLogWindow();

bool GetConPtyApi(PFN_CreatePseudoConsole* createFn, PFN_ResizePseudoConsole* resizeFn, PFN_ClosePseudoConsole* closeFn);
COORD GetPseudoConsoleSizeFromLogWindow();
void ClosePseudoConsoleHandle(HPCON hpc);
void ClearLogWindow(bool clearAllBuffer);
int GetDisplayCellWidth(wchar_t ch);


bool IsBoxDrawingChar(wchar_t ch);
int GetTerminalGlyphWidth(wchar_t ch);
int GetTerminalGlyphWidth(wchar_t ch, bool forceAmbiguousWide);
int KTinCharWidth(wchar_t ch);
int KTinCharWidth(wchar_t ch, bool forceAmbiguousWide);
bool NeedsExtraRightShiftForWideGlyph(wchar_t ch);

