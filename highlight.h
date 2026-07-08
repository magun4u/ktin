#pragma once
#include "constants.h"
#include "types.h" // ★ 새로 만든 구조체들을 여기서 인식하게 함!

struct HighlightState
{
    std::vector<HighlightRule> rules;
    bool active = false;
};

// 외부에서 접근 가능한 전역 상태
extern HighlightState g_hiState;

// 공개 함수
void LoadHighlightSettings();
void SaveHighlightSettings();
#ifndef KTIN_MAIN_LOCAL_IMPL
void ShowHighlightDialog(HWND owner);
#endif

void ExecuteHighlightRuleAction(const HighlightRule& rule, const std::vector<std::wstring>& caps);
bool MatchHighlightPattern(const std::wstring& pattern, const std::wstring& text, std::vector<std::wstring>& caps);
std::wstring ExpandHighlightCaptures(const std::wstring& src, const std::vector<std::wstring>& caps);
