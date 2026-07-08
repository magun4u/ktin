#pragma once
#include <windows.h>
#include "types.h"
#include <vector>
#include <string>

// 채팅 캡처용 데이터 구조체
struct ChatCaptureItem {
    bool active = false;
    std::wstring pattern;
    std::wstring format;
};
extern ChatCaptureItem g_chatCaptures[10]; // 최대 10개의 규칙 지원

// 함수 선언
void RunChatCaptureEngine(const std::wstring& cleanLine);
void AppendChatWindowText(const std::wstring& text);

void LoadChatCaptureSettings();
void SaveChatCaptureSettings();

void LoadCaptureLogSettings();
void SaveCaptureLogSettings();
void StartCaptureLog();
void StopCaptureLog();
void FlushCaptureLogBuffer();

void WriteRunsToCaptureLog(const std::vector<StyledRun>& runs);
void WriteRawAnsiBytesToCaptureLog(const char* data, size_t len);
void WriteToChatLog(const std::wstring& text);
void CloseChatLog();

LRESULT CALLBACK ChatFloatWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ChatEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
