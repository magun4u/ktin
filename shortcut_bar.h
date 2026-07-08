#pragma once
#include <windows.h>
#include "functionkey.h"
#include <string>

// 단축키 모디파이어
enum ShortcutModifier
{
    SCMOD_NONE  = 0,
    SCMOD_ALT   = 1,
    SCMOD_SHIFT = 2,
    SCMOD_CTRL  = 4
};

// 단축 버튼 편집용 상태 구조체
struct ShortcutEditState
{
    int index = -1;
    std::wstring* label = nullptr;
    std::wstring* command = nullptr;
    bool accepted = false;
};

struct ShortcutEditorState
{
    bool accepted = false;
};

// 함수 선언
void InitShortcutBindings();
void LoadShortcutSettings();
void SaveShortcutSettings();

void ShowShortcutDialog(HWND parent);

void InitializeShortcutButtons();
void ApplyShortcutButtons(HWND hwnd);

int GetShortcutBarHeight();

#ifndef KTIN_MAIN_LOCAL_IMPL
LRESULT CALLBACK ShortcutBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif
