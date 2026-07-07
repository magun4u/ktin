#pragma once
#include <windows.h>

const int INPUT_SEPARATOR_HEIGHT = 6;

// 함수 선언
void LayoutInputEdits();
void RecalcInputMetrics();
int GetInputAreaHeight();

void SetInputViewLatest();
void ApplyInputView();

bool ShiftInputViewOlder();
bool ShiftInputViewNewer();

void FocusInputRow(int row);
std::wstring GetInputRowText(int row);

LRESULT CALLBACK InputContainerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 내부 헬퍼 함수
void RecreateInputCaret(HWND hwnd);
int GetInputRowY(int row);

void ExecuteShortcutButton(int idx);