#pragma once
#include <windows.h>
#include <string>

// 함수 선언
#ifndef KTIN_MAIN_LOCAL_IMPL
LRESULT CALLBACK StatusBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

void CreateMainMenu(HWND hwnd);
void ShowCustomMenuPopup(HWND hwnd, int menuIndex);

void PromptStatusBarDialog(HWND owner);

#ifndef KTIN_MAIN_LOCAL_IMPL
std::wstring ExpandStatusVariables(const std::wstring& format);
#endif
