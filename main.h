#pragma once

#include "constants.h"
#include "types.h"
#include "app_state.h"

// 전역 변수
extern AppState* g_app;

// 주요 함수
void ApplyStyles();
void CreateMainMenu(HWND hwnd);
void InitShortcutBindings();
void InitializeShortcutButtons();
void ApplyShortcutButtons(HWND hwnd);
void SetInputViewLatest();
void ApplyInputView();
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
void LoadScreenSizeSettings();
void LoadQuickConnectHistory();
void LoadFontRenderSettings();
void LoadAutoLoginSettings();
void LoadNumpadSettings();

// main/window/runtime helpers
extern const wchar_t kMainWindowClass[];
extern const wchar_t* kTerminalWindowClass;
extern const wchar_t kInputWindowClass[];
extern const wchar_t* kInputContainerClass;
extern const wchar_t* kShortcutBarClass;
extern const wchar_t* kStatusBarClass;

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK TerminalWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void InvalidateTerminalDirtyRows(HWND hwnd);
void InvalidateTerminalAllRows(HWND hwnd, BOOL erase);
bool RegisterMainWindowClasses(HINSTANCE hInstance);
HWND CreateMainAppWindow(HINSTANCE hInstance);
bool InitializeMainRuntime();
void CleanupMainRuntime();
bool EnsureRichEditLoaded();
int RunMainMessageLoop();
bool DispatchModelessDialogMessage(MSG& msg);
LRESULT HandleMainCreate(HWND hwnd);
void LayoutChildren(HWND hwnd);
bool HandleMainTimer(HWND hwnd, WPARAM timerId);
void CancelMainTimers(HWND hwnd);
void ScheduleLogRedraw(HWND hwnd);
bool ShutdownMainWindow(HWND hwnd);
bool HandleGlobalMenuShortcut(HWND hwnd, UINT msg, WPARAM wParam);

bool HandleMainInitMenuPopup(HMENU menu);
bool HandleMainMenuMouseDown(HWND hwnd, LPARAM lParam);
bool HandleMainMenuMouseMove(HWND hwnd, LPARAM lParam);
bool HandleMainHiddenMenuContext(HWND hwnd, UINT msg, LPARAM lParam);
bool HandleMainLogChunk(HWND hwnd, LPARAM lParam);
bool HandleMainHistoryExportDone(HWND hwnd, LPARAM lParam);
bool HandleMainStartBackend(HWND hwnd);
void HandleMainVarUpdate(HWND hwnd, WPARAM wParam, LPARAM lParam);
bool HandleMainTrayIcon(HWND hwnd, LPARAM lParam);
bool HandleMainSize(HWND hwnd);
bool HandleMainMove(HWND hwnd);
bool HandleMainExitSizeMove(HWND hwnd);
bool HandleMainPaint(HWND hwnd);
bool HandleMainFocus(HWND hwnd);
bool HandleMainClose(HWND hwnd);
bool HandleMainDestroy(HWND hwnd);
bool HandleMainProcessExit(HWND hwnd);
bool HandleMainCommand(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

HBRUSH GetMainBackBrush();
HBRUSH GetInputContainerBrush();
HBRUSH GetInputEditBrush();
void ResetInputBrushCache(COLORREF color);
void CleanupAppGdiResources();

void SetSessionVariableValue(const std::wstring& name, const std::wstring& value);
void ResetSessionUptimeValue();
void UpdateSessionUptimeValue();
void SetSessionActiveState(HWND hwnd, bool active);
