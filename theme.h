#pragma once
#include "constants.h"
#include "types.h"
#include <string>
#include <vector>

// ==============================================
// 공통 구조체 및 함수 선언
// ==============================================
struct ThemeVisuals {
    COLORREF logBack;
    COLORREF logText;
    COLORREF inputBack;
    COLORREF inputText;
    COLORREF panelBack;
    COLORREF panelText;
};

void ApplyThemeVisualsToApp(int themeId);
const COLORREF* GetAnsiThemeTable(int themeId);
bool ShowThemeDialog(HWND owner, int* selectedTheme);
ThemeVisuals GetThemeVisuals(int themeId);
COLORREF BaseAnsi16(int idx);

// ==============================================
// 클래스 설계도 (정의부)
// ==============================================
class Utf8Decoder {
public:
    std::wstring Feed(const std::string& bytes);
    std::wstring Flush();
private:
    std::string buffer_;
};

struct ThemeDialogState {
    int originalTheme = ID_THEME_WINDOWS;
    int selectedTheme = ID_THEME_WINDOWS;
    bool accepted = false;

    HWND hwndList = nullptr;
    HWND hwndPreview = nullptr;
    HWND hwndOk = nullptr;
    HWND hwndCancel = nullptr;
};

class AnsiToRunsParser {
public:
    COLORREF cachedBg = RGB(0, 0, 0);
    COLORREF cachedFg = RGB(220, 220, 220);

    AnsiToRunsParser();
    void SyncTheme();
    bool Feed(const char* data, size_t len);
    bool Flush();

private:
    enum class State { Normal, Esc, Csi, CsiDiscard, Osc, OscEsc, OscDiscard, OscDiscardEsc };
    TextStyle style_{};
    State state_ = State::Normal;
    std::string textBytes_;
    std::string csiParams_;

    std::string oscParams_;

    Utf8Decoder decoder_;
    bool dirty_ = false;

    int fgBaseIndex = -1;
    int bgBaseIndex = -1;

    void ResetStyle();
    static COLORREF BaseAnsi16_Internal(int idx);
    static COLORREF ColorFromAnsiIndex(int idx, bool bright, bool forBackground);
    static COLORREF ColorFrom256(int idx, bool forBackground);
    void FlushText();
    void AppendRun(const std::wstring& text);
    void HandleSgr();
    void HandleOsc();
    void Consume(char ch);
};