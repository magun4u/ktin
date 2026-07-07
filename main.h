#pragma once

#include "constants.h"
#include "types.h"
#include "timer.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

// 전역 변수 및 창 클래스 이름
class TerminalBuffer;

extern struct AppState* g_app;
extern HMODULE g_hRichEdit;

extern const wchar_t kMainWindowClass[];
extern const wchar_t* kTerminalWindowClass;
extern const wchar_t kInputWindowClass[];
extern const wchar_t* kInputContainerClass;
extern const wchar_t* kShortcutBarClass;
extern const wchar_t* kStatusBarClass;

struct ProcessHandles
{
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
    HANDLE stdinWrite = nullptr;
    HANDLE stdoutRead = nullptr;
    HPCON hPC = nullptr;
};


struct AppState
{
    int inputLineHeight = 24;
    int inputAreaHeight = 90;
    int inputTextOffsetX = 8;
    int inputTextOffsetY = 4;
    bool imeComposing = false;

    HWND hwndMain = nullptr;
    HWND hwndLog = nullptr;
    HWND hwndInput = nullptr;
    HWND hwndStatusBar = nullptr;

    HWND hwndEdit[INPUT_ROWS] = {};
    WNDPROC oldEditProc[INPUT_ROWS] = {};
    int activeEditIndex = 0;
    bool internalEditUpdate = false;

    HFONT hFontLog = nullptr;
    HFONT hFontInput = nullptr;
    HFONT hFontChat = nullptr;

    UiStyle logStyle;
    UiStyle inputStyle;
    UiStyle chatStyle;
    COLORREF mainBackColor = RGB(45, 45, 48);

    HBRUSH hbrInputContainer = nullptr;
    HBRUSH hbrInputEdit = nullptr;
    HBRUSH hbrInputEditActive = nullptr;

    ProcessHandles proc;
    std::thread readerThread;
    std::atomic<bool> shuttingDown = false;

    std::wstring lastConnectCommand;
    bool keepAliveEnabled = false;
    int keepAliveIntervalSec = 10;
    std::wstring keepAliveCommand = L"점수";

    bool shortcutBarVisible = true;
    HWND hwndShortcutBar = nullptr;
    HWND hwndShortcutButtons[SHORTCUT_BUTTON_COUNT] = {};
    std::wstring shortcutLabels[SHORTCUT_BUTTON_COUNT];
    std::wstring shortcutCommands[SHORTCUT_BUTTON_COUNT];
    // ★ 신규 추가: 끄기 명령, 토글 여부, 현재 켜짐 상태
    std::wstring shortcutOffCommands[SHORTCUT_BUTTON_COUNT];
    bool shortcutIsToggle[SHORTCUT_BUTTON_COUNT] = { false };
    bool shortcutActive[SHORTCUT_BUTTON_COUNT] = { false };

    std::vector<std::wstring> history;
    std::vector<std::pair<std::wstring, int>> quickConnectHistory; // ★ 추가: 빠른 연결(주소, 문자셋) 독립 히스토리
    std::vector<AddressBookEntry> addressBook;
    int displayedHistoryIndex[INPUT_ROWS] = { -1, -1, -1 };
    int historyBrowseIndex = -1;
    bool saveInputOnExit = false;
    bool captureLogEnabled = false;
    bool chatCaptureEnabled = false;
    HANDLE hCaptureLogFile = INVALID_HANDLE_VALUE;
    std::wstring captureLogPath;

    // ANSI 원본 버퍼 (최적화/로그 분석용)
    std::string rawAnsiCurrentScreen;
    std::string rawAnsiHistory;

    // 로그창 다시그리기 예약 플래그
    bool logRedrawPending = false;

    int screenCols = 80;
    int screenRows = 32;
    int termAlign = 0; // 0=왼쪽, 1=중앙, 2=오른쪽
    int termMarginLeft = 0;
    int termMarginRight = 0;
    int termMarginTop = 0;
    int termMarginBottom = 0;
    bool mainAlwaysOnTop = false;
    bool tailSnapEnabled = true;

    HMENU hMainMenu = nullptr;
    bool menuHidden = false;

    int customMenuHeight = 26;
    int hotMenuIndex = -1;
    bool trackingMenuMouse = false;
    int pendingMenuIndex = -1;

    int ansiTheme = ID_THEME_XTERM;

    bool smoothFontEnabled = true;
    TerminalBuffer* termBuffer = nullptr;
    HWND hwndSymbolDialog = nullptr; // ★ 추가됨: 특수기호 창 상태를 기억

    // ★ 시작 시 팝업 설정 변수
    bool autoShowQuickConnect = false;
    bool autoShowAddressBook = false;

    // ★ 채팅 캡쳐창 도킹 관련 변수
    HWND hwndChat = nullptr;
    HWND hwndChatFloat = nullptr;
    bool chatDocked = true;
    int chatDockedLines = 5;
    RECT chatFloatRect = { 100, 100, 600, 300 };

    bool chatVisible = false;
    bool chatTimestampEnabled = false; // ★ 신규: 시간 출력 옵션 변수
    WNDPROC oldChatProc = nullptr; // ★ 추가: 채팅 캡쳐창 X버튼 가로채기용

    // ★ 신규 추가: 자동 로그인 설정 및 상태 변수
    bool autoLoginEnabled = false;
    std::wstring autoLoginIdPattern = L"아이디:";
    std::wstring autoLoginId = L"";
    std::wstring autoLoginPwPattern = L"비밀번호:";
    std::wstring autoLoginPw = L"";
    std::wstring autoLoginSuccessPattern1 = L"";
    std::wstring autoLoginSuccessPattern2 = L"";
    std::wstring autoLoginSuccessPattern3 = L"";
    std::wstring autoLoginFailPattern1 = L"";
    std::wstring autoLoginFailPattern2 = L"";
    std::wstring autoLoginFailPattern3 = L"";

    // ★ 신규 추가: '지금 연결할 때' 실제로 쓸 자동 로그인 정보 (전역 or 주소록)
    bool activeAutoLoginEnabled = false;
    std::wstring activeAutoLoginIdPattern;
    std::wstring activeAutoLoginId;
    std::wstring activeAutoLoginPwPattern;
    std::wstring activeAutoLoginPw;

    int autoLoginState = 0; // 0: 대기, 1: 아이디 전송됨, 2: 비밀번호 전송됨 (완료)    

    bool limitBackspaceToCurrentRow = true; // ★ 추가: Backspace 행 제한 옵션 변수
    int addressBookSortMode = 0; // ★ 신규: 주소록 정렬 옵션 (0:최근, 1:이름, 2:서버, 3:아이디)

    // ★ 신규 추가: 트레이 관련 변수
    bool closeToTray = false;
    bool trayIconVisible = false;

    bool soundEnabled = false; // 기본값: 소리 안 남(OFF)

    bool abbreviationGlobalEnabled = true;
    std::vector<AbbreviationItem> abbreviations;

    bool variableGlobalEnabled = true;
    std::vector<VariableItem> variables;

    // ★ 신규 추가: 숫자 키패드 매크로 데이터
    bool numpadMacroEnabled = true;
    std::wstring numpadMacros[15]; // 0~9번: 숫자, 10:/, 11:*, 12:-, 13:+, 14:.

    AddressBookEntry activeSession; // ★ 신규 추가: 현재/마지막으로 접속에 사용한 주소록 정보
    bool hasActiveSession = false;  // ★ 신규 추가: 활성화된 주소록 정보가 있는지 여부

    // ★ 신규 추가: 주소록에서 다른 서버로 전환 접속할 때 잠시 보관할 대상
    AddressBookEntry pendingConnectEntry;
    bool hasPendingConnect = false;

    int statusPartCount = 1;             // 분할 개수 (1~5개)
    std::wstring statusFormats[5];
    int statusAligns[5];

    bool isConnected = false;

    bool isSessionActive = false;
    time_t sessionStartTime = 0;

    HANDLE hFontRes = nullptr;
    bool privateFontFileRegistered = false;
    std::wstring privateFontFilePath;
    bool useCustomMudFont = true;      // 추가
    std::wstring userFontName;  // 추가 (error C2039 해결)

    HWND hwndSymbol;       // ★ 추가: 특수기호 창 핸들
    HWND hwndTargetEdit;   // ★ 추가: 특수기호를 입력할 대상 창 핸

    bool g_charsetDetected = false;
    int  g_detectCharsetRetry = 0;

    bool ambiguousEastAsianWide = true;

    std::vector<TimerItem> timers;

    bool logRedrawEnabled = true;

    DWORD autoLoginStartTick = 0;
    bool autoLoginWindowActive = false;
    DWORD keepAliveBlockedUntilTick = 0;
    DWORD lastConnectionSuccessTick = 0;
    DWORD lastConnectionDownTick = 0;

    bool autoLoginTriggered = false;
};

struct FindState
{
    HWND hwndDialog = nullptr;
    bool dialogOpen = false;

    std::wstring query;
    bool matchCase = false;
    bool fromStart = false;
    bool directionUp = false;

    LONG lastFoundMin = -1;
    LONG lastFoundMax = -1;
    bool hasLastFind = false;
};

struct MemoFindState {
    HWND hwndDialog = nullptr;
    std::wstring query;
    std::wstring replaceText;
    bool matchCase = false;
    bool isReplaceMode = false;
};

extern FindState g_findState;
extern bool GetConPtyApi(PFN_CreatePseudoConsole* createFn, PFN_ResizePseudoConsole* resizeFn, PFN_ClosePseudoConsole* closeFn);
extern COORD GetPseudoConsoleSizeFromLogWindow();
extern void ClosePseudoConsoleHandle(HPCON hpc);
extern void LayoutInputEdits();


// 메인 윈도우 콜백 및 주요 함수
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK TerminalWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ChatEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK InputContainerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ShortcutBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK StatusBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ChatFloatWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ReaderThreadProc(HWND hwndMain, HANDLE hRead);

void LayoutChildren(HWND hwnd);
void ApplyStyles();
bool StartTinTinProcess();
void StopProcessAndThread();
void CreateMainMenu(HWND hwnd);
void InitShortcutBindings();
void InitializeShortcutButtons();
void StartCaptureLog();
void StopCaptureLog();
void ApplyShortcutButtons(HWND hwnd);
void SetInputViewLatest();
void ApplyInputView();
void ShowChatCaptureDialog(HWND owner);
void QueueSaveWindowSettings(HWND hwnd);
void ShowSettingsDialog(HWND owner);
void SaveFontRenderSettings();
void OpenMemoWindow(HWND owner);
void ShowShortcutHelp(HWND owner);
void PromptNumpadDialog(HWND owner);
void PromptStatusBarDialog(HWND owner);
bool PromptAboutDialog(HWND hwnd);
void ShowQuickConnectDialog(HWND owner);
bool PromptAddressBook(HWND hwnd);
bool PromptKeepAliveSettings(HWND hwnd, bool& enabled, int& intervalSec, std::wstring& command);
void SaveKeepAliveSettings();
void ApplyKeepAliveTimer(HWND hwnd);
void SaveInputHistorySettings();
void SaveCaptureLogSettings();
bool PromptScreenSizeSettings(HWND hwnd, int& cols, int& rows);
void SaveScreenSizeSettings();
bool PromptShortcutEditor(HWND hwnd);
void ShowFindDialog(HWND owner);
void ShowShortcutDialog(HWND parent);
void UpdateAnsiThemeMenuChecks();

void LoadAddressBook();
void LoadFunctionKeySettings();
void LoadShortcutSettings();
void LoadKeepAliveSettings();
void LoadInputHistorySettings();
void LoadCaptureLogSettings();
void LoadChatCaptureSettings();
void LoadScreenSizeSettings();
void LoadQuickConnectHistory();
void LoadFontRenderSettings();
void LoadAutoLoginSettings();
void LoadNumpadSettings();
