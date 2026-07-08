#pragma once
#include <windows.h>
#include <string>

// 함수 선언
void ShowQuickConnectDialog(HWND owner);
void ShowChatCaptureDialog(HWND owner);
void ShowFindDialog(HWND owner);
void ShowSymbolDialog(HWND owner);
void ShowInfoPopup(HWND owner);

bool PromptShortcutEditor(HWND hwnd);
