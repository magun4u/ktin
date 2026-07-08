#pragma once
#include <windows.h>
#include <string>
#include <vector>

// 구조체 선언
struct ScreenSizePopupState
{
    int* cols = nullptr;
    int* rows = nullptr;
    bool accepted = false;
};

struct SettingsDlgState
{
    HWND hwndList = nullptr;
    HWND hwndGroups[5] = { nullptr };
    std::vector<HWND> panelCtrls[5];
    int currentIdx = 0;
};

struct KeepAliveDialogState
{
    bool* enabled = nullptr;
    int* intervalSec = nullptr;
    std::wstring* command = nullptr;
    bool accepted = false;
};

// 함수 선언 (public)
std::wstring GetSettingsPath();

void UpdateSettingPreviews(HWND hwnd);

void ShowSettingsDialog(HWND owner);
void SaveWindowSettings(HWND hwnd);
void LoadWindowSettings(HWND hwnd);

void LoadQuickConnectHistory();
void SaveQuickConnectHistory();

void LoadScreenSizeSettings();
void SaveScreenSizeSettings();

void LoadAutoLoginSettings();
void SaveAutoLoginSettings();

void LoadCaptureLogSettings();
void SaveCaptureLogSettings();

void LoadNumpadSettings();

void LoadFunctionKeySettings();
void SaveFunctionKeySettings();

void SaveChatCaptureSettings();
void LoadChatCaptureSettings();


void LoadShortcutSettings();

void SaveShortcutSettings();

void LoadKeepAliveSettings();
void SaveKeepAliveSettings();

void LoadInputHistorySettings();
void SaveInputHistorySettings();
void ClearInputHistorySettings();

void LoadFontRenderSettings();
void SaveFontRenderSettings();
BYTE GetCurrentFontQuality();
void RebuildInputBrushes();

// 내부에서만 사용하는 팝업 함수들
bool PromptScreenSizeSettings(HWND hwnd, int& cols, int& rows);
bool PromptKeepAliveSettings(HWND hwnd, bool& enabled, int& intervalSec, std::wstring& command);

void LoadAddressBook();
void SaveAddressBook();

void SaveNumpadSettings();
void LoadNumpadSettings();

void ApplyKeepAliveTimer(HWND hwnd);

void UpdateMenuToggleStates();


void LoadGeneralSettings();
void SaveGeneralSettings();
