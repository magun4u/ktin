#pragma once
#include <windows.h>
#include <string>

// 함수 선언
LRESULT CALLBACK StatusBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void CreateMainMenu(HWND hwnd);
void ShowCustomMenuPopup(HWND hwnd, int menuIndex);

void PromptStatusBarDialog(HWND owner);

std::wstring ExpandStatusVariables(const std::wstring& format);
