#pragma once
#include <windows.h>
#include <string>

// mode: 0=전체, 1=잡담, 2=경매, 3=아이템 획득, 4=임시 문자열, 5=대화, 6=경험치, 7~9=사용자 정의
// buildfix22: 갈무리 보기 창은 여러 개를 동시에 열 수 있고, 각 창은 자체 탭 목록을 가집니다.
void ShowCaptureTailWindow(HWND owner, int mode);
void OpenCaptureLogFolder(HWND owner);
void PromptTailFilterSettings(HWND owner);

bool HasCaptureTailWindows();
void CloseAllCaptureTailWindows();

std::wstring GetTailModeMenuTitle(int mode);

// buildfix26: 열린 갈무리 보기 창의 출력/상태/탭 폰트를 메인 출력창 폰트와 맞춥니다.
void ApplyTailWindowFonts();

// buildfix32: 메인창이 이동할 때 메인창에 붙은 갈무리창/갈무리창 스택도 함께 이동합니다.
void TailNotifyMainWindowMoved(HWND hwndMain, const RECT& oldMainWindowRect, const RECT& newMainWindowRect);
