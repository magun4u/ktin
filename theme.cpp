#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "theme.h"
#include "resource.h"
#include "settings.h"
#include "input.h"
#include "auto_login.h"
#include "log_tail.h"
#include <commctrl.h>
#include <algorithm>
#include <new>
#include <atomic>
#include <memory>
#include <thread>
#include <string_view>
#include "win_util.h"

namespace
{
    constexpr size_t kMaxAnsiCsiParamBytes = 128;
    constexpr size_t kMaxAnsiOscParamBytes = 8192;
}

// ==============================================
// ANSI 테마 테이블
// ==============================================


struct ThemeInfo {
    int id;
    const wchar_t* name;
};

static const ThemeInfo kAnsiThemes[] =
{
    { ID_THEME_WINDOWS,       L"윈도우 콘솔" },
    { ID_THEME_XTERM,         L"xterm 16색" },
    { ID_THEME_CAMPBELL,      L"캠벨" },
    { ID_THEME_POWERSHELL,    L"캠벨 파워셸" },
    { ID_THEME_ALMALINUX,     L"알마리눅스" },
    { ID_THEME_DARKPLUS,      L"다크+" },
    { ID_THEME_DIMIDIUM,      L"디미디움" },
    { ID_THEME_IBM5153,       L"IBM 5153" },
    { ID_THEME_ONE_HALF_DARK, L"원 하프 다크" },
    { ID_THEME_ONE_HALF_LIGHT,L"원 하프 라이트" },
    { ID_THEME_OTTOSSON,      L"오토손" },
    { ID_THEME_SOLARIZED_DARK,L"솔라라이즈드 다크" },
    { ID_THEME_SOLARIZED_LIGHT,L"솔라라이즈드 라이트" },
    { ID_THEME_TANGO_DARK,    L"탱고 다크" },
    { ID_THEME_TANGO_LIGHT,   L"탱고 라이트" },
    { ID_THEME_UBUNTU,        L"우분투" },
    { ID_THEME_UBUNTU_2004,   L"우분투 20.04" },
    { ID_THEME_UBUNTU_2204,   L"우분투 22.04" },
    { ID_THEME_UBUNTU_COLOR,  L"우분투 컬러" },
    { ID_THEME_VINTAGE,       L"빈티지" },
    { ID_THEME_CGA,           L"CGA" }
};

static const int kAnsiThemeCount = (int)(sizeof(kAnsiThemes) / sizeof(kAnsiThemes[0]));
static std::atomic<unsigned int> s_themeRecolorSerial{0};

// ==============================================
// Utf8Decoder 멤버 함수 구현
// ==============================================
std::wstring Utf8Decoder::Feed(const std::string& bytes)
{
    if (bytes.empty() && buffer_.empty())
        return L"";

    if (!bytes.empty())
    {
        if (buffer_.capacity() < buffer_.size() + bytes.size())
            buffer_.reserve(buffer_.size() + bytes.size());
        buffer_.append(bytes);
    }

    std::wstring out;
    out.reserve(buffer_.size());

    while (!buffer_.empty())
    {
        int okChars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            buffer_.data(), static_cast<int>(buffer_.size()), nullptr, 0);

        if (okChars > 0)
        {
            std::wstring temp(okChars, L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                buffer_.data(), static_cast<int>(buffer_.size()), &temp[0], okChars);
            out += temp;
            buffer_.clear();
            break;
        }

        bool converted = false;
        for (int prefix = static_cast<int>(buffer_.size()) - 1; prefix >= 1; --prefix)
        {
            int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                buffer_.data(), prefix, nullptr, 0);
            if (chars > 0)
            {
                std::wstring temp(chars, L'\0');
                MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                    buffer_.data(), prefix, &temp[0], chars);
                out += temp;
                buffer_.erase(0, prefix);
                converted = true;
                break;
            }
        }

        if (!converted)
        {
            unsigned char b = static_cast<unsigned char>(buffer_[0]);
            int need = 1;
            if ((b & 0x80) == 0x00) need = 1;
            else if ((b & 0xE0) == 0xC0) need = 2;
            else if ((b & 0xF0) == 0xE0) need = 3;
            else if ((b & 0xF8) == 0xF0) need = 4;
            else need = 1;

            if (static_cast<int>(buffer_.size()) < need)
                break;

            out.push_back(L'\uFFFD');
            buffer_.erase(0, 1);
        }
    }
    return out;
}

std::wstring Utf8Decoder::Flush()
{
    std::wstring out(buffer_.size(), L'\uFFFD');
    buffer_.clear();
    return out;
}

// ==============================================
// AnsiToRunsParser 멤버 함수 구현
// ==============================================
AnsiToRunsParser::AnsiToRunsParser()
{
    if (g_app) {
        cachedBg = g_app->logStyle.backColor;
        cachedFg = g_app->logStyle.textColor;
    }
    ResetStyle();
}

void AnsiToRunsParser::SyncTheme()
{
    if (!g_app) return;

    if (cachedBg != g_app->logStyle.backColor) {
        if (style_.bg == cachedBg)
            style_.bg = g_app->logStyle.backColor;
        cachedBg = g_app->logStyle.backColor;
    }
    if (cachedFg != g_app->logStyle.textColor) {
        if (style_.fg == cachedFg)
            style_.fg = g_app->logStyle.textColor;
        cachedFg = g_app->logStyle.textColor;
    }
}

bool AnsiToRunsParser::Feed(const char* data, size_t len)
{
    SyncTheme();
    dirty_ = false;

    if (!data || len == 0)
        return false;

    if (state_ == State::Normal)
    {
        const size_t wanted = textBytes_.size() + len;
        if (textBytes_.capacity() < wanted)
            textBytes_.reserve(std::min<size_t>(wanted, textBytes_.size() + 65536));
    }

    for (size_t i = 0; i < len; ++i)
        Consume(data[i]);

    if (state_ == State::Normal && !textBytes_.empty())
        FlushText();

    const bool changed = dirty_;
    dirty_ = false;
    return changed;
}

bool AnsiToRunsParser::Flush()
{
    dirty_ = false;
    FlushText();
    const bool changed = dirty_;
    dirty_ = false;
    return changed;
}

void AnsiToRunsParser::ResetStyle()
{
    style_.fg = g_app ? g_app->logStyle.textColor : RGB(220, 220, 220);
    style_.bg = g_app ? g_app->logStyle.backColor : RGB(0, 0, 0);
    style_.bold = false;
    fgBaseIndex = -1;
    bgBaseIndex = -1;
}

COLORREF AnsiToRunsParser::BaseAnsi16_Internal(int idx)
{
    const COLORREF* table = GetAnsiThemeTable(g_app ? g_app->ansiTheme : ID_THEME_CAMPBELL);
    if (idx < 0) idx = 0;
    if (idx > 15) idx = 15;
    return table[idx];
}

COLORREF AnsiToRunsParser::ColorFromAnsiIndex(int idx, bool bright, bool forBackground)
{
    int n = bright ? (idx + 8) : idx;
    if (forBackground && idx == 0 && g_app)
        return g_app->logStyle.backColor;
    return BaseAnsi16_Internal(n);
}

COLORREF AnsiToRunsParser::ColorFrom256(int idx, bool forBackground)
{
    if (idx < 0) idx = 0;
    if (idx > 255) idx = 255;
    if (forBackground && idx == 0 && g_app)
        return g_app->logStyle.backColor;

    if (idx < 16)
        return BaseAnsi16_Internal(idx);

    if (idx >= 232)
    {
        int gray = 8 + (idx - 232) * 10;
        return RGB(gray, gray, gray);
    }

    idx -= 16;
    int r = idx / 36;
    int g = (idx / 6) % 6;
    int b = idx % 6;
    static const int steps[6] = { 0, 95, 135, 175, 215, 255 };
    return RGB(steps[r], steps[g], steps[b]);
}

void AnsiToRunsParser::FlushText()
{
    if (textBytes_.empty()) return;
    std::wstring w = decoder_.Feed(textBytes_);
    textBytes_.clear();
    if (!w.empty())
    {
        RunAutoLoginEngine(w);
        AppendRun(w);
    }
}

void AnsiToRunsParser::AppendRun(const std::wstring& text)
{
    if (text.empty())
        return;

    if (g_app && g_app->termBuffer)
    {
        g_app->termBuffer->AppendText(text, style_.fg, style_.bg, style_.bold);
        dirty_ = true;
    }
}

void AnsiToRunsParser::HandleSgr()
{
    if (csiParams_.empty())
        csiParams_ = "0";

    std::vector<int> nums;
    int value = 0;
    bool have = false;

    for (char ch : csiParams_) {
        if (ch >= '0' && ch <= '9') {
            value = value * 10 + (ch - '0');
            have = true;
        }
        else if (ch == ';') {
            nums.push_back(have ? value : 0);
            value = 0;
            have = false;
        }
    }
    nums.push_back(have ? value : 0);

    for (size_t i = 0; i < nums.size(); ++i) {
        int n = nums[i];
        if (n == 0) { ResetStyle(); }
        else if (n == 1) { style_.bold = true; if (fgBaseIndex >= 0 && fgBaseIndex <= 7) style_.fg = ColorFromAnsiIndex(fgBaseIndex, true, false); }
        else if (n == 22) { style_.bold = false; if (fgBaseIndex >= 0 && fgBaseIndex <= 7) style_.fg = ColorFromAnsiIndex(fgBaseIndex, false, false); }
        else if (n >= 30 && n <= 37) { fgBaseIndex = n - 30; style_.fg = ColorFromAnsiIndex(fgBaseIndex, style_.bold, false); }
        else if (n >= 90 && n <= 97) { fgBaseIndex = n - 90; style_.fg = ColorFromAnsiIndex(fgBaseIndex, true, false); }
        else if (n == 39) { fgBaseIndex = -1; style_.fg = g_app ? g_app->logStyle.textColor : RGB(220, 220, 220); }
        else if (n >= 40 && n <= 47) { bgBaseIndex = n - 40; style_.bg = ColorFromAnsiIndex(bgBaseIndex, false, true); }
        else if (n >= 100 && n <= 107) { bgBaseIndex = n - 100; style_.bg = ColorFromAnsiIndex(bgBaseIndex, true, true); }
        else if (n == 49) { bgBaseIndex = -1; style_.bg = g_app ? g_app->logStyle.backColor : RGB(0, 0, 0); }
        else if (n == 38 || n == 48) {
            bool isFg = (n == 38);
            if (i + 1 < nums.size()) {
                if (nums[i + 1] == 5 && i + 2 < nums.size()) {
                    COLORREF c = ColorFrom256(nums[i + 2], !isFg);
                    if (isFg) { style_.fg = c; fgBaseIndex = -1; }
                    else { style_.bg = c; bgBaseIndex = -1; }
                    i += 2;
                }
                else if (nums[i + 1] == 2 && i + 4 < nums.size()) {
                    int r = ClampByteRange(nums[i + 2], 0, 255);
                    int g = ClampByteRange(nums[i + 3], 0, 255);
                    int b = ClampByteRange(nums[i + 4], 0, 255);
                    COLORREF c = RGB(r, g, b);
                    if (!isFg && r == 0 && g == 0 && b == 0 && g_app) c = g_app->logStyle.backColor;
                    if (isFg) { style_.fg = c; fgBaseIndex = -1; }
                    else { style_.bg = c; bgBaseIndex = -1; }
                    i += 4;
                }
            }
        }
    }
}

void AnsiToRunsParser::Consume(char ch)
{
    switch (state_)
    {
    case State::Normal:
        if (ch == 0x1B) { FlushText(); state_ = State::Esc; }
        else { textBytes_.push_back(ch); }
        break;
    case State::Esc:
        if (ch == '[') { csiParams_.clear(); state_ = State::Csi; }
        else if (ch == ']') {
            oscParams_.clear(); // ★ OSC 시작 시 파라미터 버퍼 비우기
            state_ = State::Osc;
        }
        else { state_ = State::Normal; }
        break;
    case State::Csi:
        if ((ch >= '0' && ch <= '9') || ch == ';') {
            if (csiParams_.size() < kMaxAnsiCsiParamBytes)
                csiParams_.push_back(ch);
            else
                state_ = State::CsiDiscard;
        }
        else if (ch == 'm') {
            HandleSgr();
            csiParams_.clear();
            state_ = State::Normal;
        }
        else if (ch >= 0x40 && ch <= 0x7E) {
            if (g_app && g_app->termBuffer) {
                g_app->termBuffer->HandleCommand(ch, csiParams_);
            }
            csiParams_.clear();
            state_ = State::Normal;
        }
        break;
    case State::CsiDiscard:
        if (ch >= 0x40 && ch <= 0x7E)
        {
            csiParams_.clear();
            state_ = State::Normal;
        }
        break;
    case State::Osc:
        if (ch == 0x07) { // ★ BEL(0x07) 문자를 만나면 신호 수집 완료!
            HandleOsc();  // ★ 모아둔 데이터를 해석해서 메인창으로 보냅니다.
            state_ = State::Normal;
        }
        else if (ch == 0x1B) {
            state_ = State::OscEsc;
        }
        else {
            if (oscParams_.size() < kMaxAnsiOscParamBytes)
                oscParams_.push_back(ch); // ★ 신호 내용을 차곡차곡 수집합니다.
            else
            {
                oscParams_.clear();
                state_ = State::OscDiscard;
            }
        }
        break;
    case State::OscEsc:
        if (ch == '\\') { // ★ ESC \ (ST) 문자를 만나도 신호 수집 완료!
            HandleOsc();
            state_ = State::Normal;
        }
        else {
            if (oscParams_.size() + 2 <= kMaxAnsiOscParamBytes)
            {
                oscParams_.push_back(0x1B); // ESC 문자가 종료용이 아니었다면 다시 버퍼에 넣음
                oscParams_.push_back(ch);
                state_ = State::Osc;
            }
            else
            {
                oscParams_.clear();
                state_ = State::OscDiscard;
            }
        }
        break;
    case State::OscDiscard:
        if (ch == 0x07)
            state_ = State::Normal;
        else if (ch == 0x1B)
            state_ = State::OscDiscardEsc;
        break;
    case State::OscDiscardEsc:
        state_ = (ch == '\\') ? State::Normal : State::OscDiscard;
        break;
    }
}

// ==============================================
// 테마 유틸 함수들
// ==============================================

const COLORREF* GetAnsiThemeTable(int themeId)
{
    static const COLORREF kWindows[16] = { RGB(12,12,12), RGB(197,15,31), RGB(19,161,14), RGB(193,156,0), RGB(0,55,218), RGB(136,23,152), RGB(58,150,221), RGB(204,204,204), RGB(118,118,118), RGB(231,72,86), RGB(22,198,12), RGB(249,241,165), RGB(59,120,255), RGB(180,0,158), RGB(97,214,214), RGB(242,242,242) };
    static const COLORREF kXterm[16] = { RGB(0,0,0), RGB(205,0,0), RGB(0,205,0), RGB(205,205,0), RGB(0,0,238), RGB(205,0,205), RGB(0,205,205), RGB(229,229,229), RGB(127,127,127), RGB(255,0,0), RGB(0,255,0), RGB(255,255,0), RGB(92,92,255), RGB(255,0,255), RGB(0,255,255), RGB(255,255,255) };
    static const COLORREF kCampbell[16] = { RGB(12,12,12), RGB(197,15,31), RGB(19,161,14), RGB(193,156,0), RGB(0,55,218), RGB(136,23,152), RGB(58,150,221), RGB(204,204,204), RGB(118,118,118), RGB(231,72,86), RGB(22,198,12), RGB(249,241,165), RGB(59,120,255), RGB(180,0,158), RGB(97,214,214), RGB(242,242,242) };
    static const COLORREF kPowershell[16] = { RGB(1,36,86), RGB(197,15,31), RGB(19,161,14), RGB(193,156,0), RGB(0,55,218), RGB(136,23,152), RGB(58,150,221), RGB(204,204,204), RGB(118,118,118), RGB(231,72,86), RGB(22,198,12), RGB(249,241,165), RGB(59,120,255), RGB(180,0,158), RGB(97,214,214), RGB(242,242,242) };
    static const COLORREF kAlmaLinux[16] = { RGB(18,18,18), RGB(232,71,86), RGB(74,222,128), RGB(250,204,21), RGB(96,165,250), RGB(192,132,252), RGB(45,212,191), RGB(229,231,235), RGB(107,114,128), RGB(248,113,113), RGB(134,239,172), RGB(253,224,71), RGB(147,197,253), RGB(216,180,254), RGB(153,246,228), RGB(255,255,255) };
    static const COLORREF kDarkPlus[16] = { RGB(30,30,30), RGB(205,49,49), RGB(13,188,121), RGB(229,229,16), RGB(36,114,200), RGB(188,63,188), RGB(17,168,205), RGB(204,204,204), RGB(118,118,118), RGB(241,76,76), RGB(35,209,139), RGB(245,245,67), RGB(59,142,234), RGB(214,112,214), RGB(41,184,219), RGB(255,255,255) };
    static const COLORREF kDimidium[16] = { RGB(16,16,16), RGB(201,71,71), RGB(130,180,80), RGB(210,180,90), RGB(95,135,215), RGB(170,120,200), RGB(100,180,180), RGB(210,210,210), RGB(96,96,96), RGB(230,110,110), RGB(160,210,110), RGB(235,210,120), RGB(135,175,235), RGB(200,150,220), RGB(140,220,220), RGB(245,245,245) };
    static const COLORREF kIbm5153[16] = { RGB(0,0,0), RGB(170,0,0), RGB(0,170,0), RGB(170,85,0), RGB(0,0,170), RGB(170,0,170), RGB(0,170,170), RGB(170,170,170), RGB(85,85,85), RGB(255,85,85), RGB(85,255,85), RGB(255,255,85), RGB(85,85,255), RGB(255,85,255), RGB(85,255,255), RGB(255,255,255) };
    static const COLORREF kOneHalfDark[16] = { RGB(40,44,52), RGB(224,108,117), RGB(152,195,121), RGB(229,192,123), RGB(97,175,239), RGB(198,120,221), RGB(86,182,194), RGB(220,223,228), RGB(92,99,112), RGB(224,108,117), RGB(152,195,121), RGB(229,192,123), RGB(97,175,239), RGB(198,120,221), RGB(86,182,194), RGB(255,255,255) };
    static const COLORREF kOneHalfLight[16] = { RGB(250,250,250), RGB(228,86,73), RGB(80,161,79), RGB(196,160,0), RGB(1,132,188), RGB(166,38,164), RGB(9,139,128), RGB(56,58,66), RGB(160,161,167), RGB(202,18,67), RGB(80,161,79), RGB(152,104,1), RGB(64,120,242), RGB(166,38,164), RGB(9,139,128), RGB(9,10,10) };
    static const COLORREF kOttosson[16] = { RGB(22,22,22), RGB(219,83,117), RGB(95,180,90), RGB(224,184,65), RGB(79,140,255), RGB(181,126,220), RGB(80,200,200), RGB(220,220,220), RGB(100,100,100), RGB(240,110,138), RGB(120,210,110), RGB(245,210,90), RGB(120,170,255), RGB(210,160,240), RGB(120,230,230), RGB(255,255,255) };
    static const COLORREF kSolarizedDark[16] = { RGB(7,54,66), RGB(220,50,47), RGB(133,153,0), RGB(181,137,0), RGB(38,139,210), RGB(211,54,130), RGB(42,161,152), RGB(238,232,213), RGB(0,43,54), RGB(203,75,22), RGB(88,110,117), RGB(101,123,131), RGB(131,148,150), RGB(108,113,196), RGB(147,161,161), RGB(253,246,227) };
    static const COLORREF kSolarizedLight[16] = { RGB(238,232,213), RGB(220,50,47), RGB(133,153,0), RGB(181,137,0), RGB(38,139,210), RGB(211,54,130), RGB(42,161,152), RGB(7,54,66), RGB(253,246,227), RGB(203,75,22), RGB(88,110,117), RGB(101,123,131), RGB(131,148,150), RGB(108,113,196), RGB(147,161,161), RGB(0,43,54) };
    static const COLORREF kTangoDark[16] = { RGB(0,0,0), RGB(204,0,0), RGB(78,154,6), RGB(196,160,0), RGB(52,101,164), RGB(117,80,123), RGB(6,152,154), RGB(211,215,207), RGB(85,87,83), RGB(239,41,41), RGB(138,226,52), RGB(252,233,79), RGB(114,159,207), RGB(173,127,168), RGB(52,226,226), RGB(238,238,236) };
    static const COLORREF kTangoLight[16] = { RGB(238,238,236), RGB(204,0,0), RGB(78,154,6), RGB(196,160,0), RGB(52,101,164), RGB(117,80,123), RGB(6,152,154), RGB(46,52,54), RGB(136,138,133), RGB(239,41,41), RGB(138,226,52), RGB(252,233,79), RGB(114,159,207), RGB(173,127,168), RGB(52,226,226), RGB(0,0,0) };
    static const COLORREF kUbuntu[16] = { RGB(1,1,1), RGB(222,56,43), RGB(57,181,74), RGB(255,199,6), RGB(0,111,184), RGB(118,38,113), RGB(44,181,233), RGB(204,204,204), RGB(128,128,128), RGB(255,0,0), RGB(0,255,0), RGB(255,255,0), RGB(0,0,255), RGB(255,0,255), RGB(0,255,255), RGB(255,255,255) };
    static const COLORREF kUbuntu2004[16] = { RGB(46,18,34), RGB(222,56,43), RGB(57,181,74), RGB(255,199,6), RGB(0,111,184), RGB(118,38,113), RGB(44,181,233), RGB(238,238,236), RGB(128,128,128), RGB(255,96,88), RGB(88,255,98), RGB(255,231,111), RGB(109,158,235), RGB(173,127,168), RGB(87,227,255), RGB(255,255,255) };
    static const COLORREF kUbuntu2204[16] = { RGB(45,12,41), RGB(232,65,24), RGB(51,171,91), RGB(208,166,0), RGB(18,113,255), RGB(165,69,255), RGB(38,201,197), RGB(238,238,236), RGB(112,112,112), RGB(255,93,58), RGB(102,255,124), RGB(255,224,101), RGB(109,158,255), RGB(197,136,255), RGB(110,240,230), RGB(255,255,255) };
    static const COLORREF kUbuntuColor[16] = { RGB(46,52,54), RGB(204,0,0), RGB(78,154,6), RGB(196,160,0), RGB(52,101,164), RGB(117,80,123), RGB(6,152,154), RGB(211,215,207), RGB(85,87,83), RGB(239,41,41), RGB(138,226,52), RGB(252,233,79), RGB(114,159,207), RGB(173,127,168), RGB(52,226,226), RGB(238,238,236) };
    static const COLORREF kVintage[16] = { RGB(0,0,0), RGB(128,0,0), RGB(0,128,0), RGB(128,128,0), RGB(0,0,128), RGB(128,0,128), RGB(0,128,128), RGB(192,192,192), RGB(128,128,128), RGB(255,0,0), RGB(0,255,0), RGB(255,255,0), RGB(0,0,255), RGB(255,0,255), RGB(0,255,255), RGB(255,255,255) };
    static const COLORREF kCga[16] = { RGB(0,0,0), RGB(170,0,0), RGB(0,170,0), RGB(170,85,0), RGB(0,0,170), RGB(170,0,170), RGB(0,170,170), RGB(170,170,170), RGB(85,85,85), RGB(255,85,85), RGB(85,255,85), RGB(255,255,85), RGB(85,85,255), RGB(255,85,255), RGB(85,255,255), RGB(255,255,255) };

    switch (themeId)
    {
    case ID_THEME_XTERM:           return kXterm;
    case ID_THEME_CAMPBELL:        return kCampbell;
    case ID_THEME_POWERSHELL:      return kPowershell;
    case ID_THEME_ALMALINUX:       return kAlmaLinux;
    case ID_THEME_DARKPLUS:        return kDarkPlus;
    case ID_THEME_DIMIDIUM:        return kDimidium;
    case ID_THEME_IBM5153:         return kIbm5153;
    case ID_THEME_ONE_HALF_DARK:   return kOneHalfDark;
    case ID_THEME_ONE_HALF_LIGHT:  return kOneHalfLight;
    case ID_THEME_OTTOSSON:        return kOttosson;
    case ID_THEME_SOLARIZED_DARK:  return kSolarizedDark;
    case ID_THEME_SOLARIZED_LIGHT: return kSolarizedLight;
    case ID_THEME_TANGO_DARK:      return kTangoDark;
    case ID_THEME_TANGO_LIGHT:     return kTangoLight;
    case ID_THEME_UBUNTU:          return kUbuntu;
    case ID_THEME_UBUNTU_2004:     return kUbuntu2004;
    case ID_THEME_UBUNTU_2204:     return kUbuntu2204;
    case ID_THEME_UBUNTU_COLOR:    return kUbuntuColor;
    case ID_THEME_VINTAGE:         return kVintage;
    case ID_THEME_CGA:             return kCga;
    case ID_THEME_WINDOWS:
    default:
        return kWindows;
    }
}

static const wchar_t* GetThemeNameById(int id)
{
    for (int i = 0; i < kAnsiThemeCount; ++i)
    {
        if (kAnsiThemes[i].id == id)
            return kAnsiThemes[i].name;
    }
    return L"(알 수 없음)";
}

COLORREF BaseAnsi16(int idx)
{
    const COLORREF* table = GetAnsiThemeTable(g_app ? g_app->ansiTheme : ID_THEME_CAMPBELL);
    if (idx < 0) idx = 0;
    if (idx > 15) idx = 15;
    return table[idx];
}

ThemeVisuals GetThemeVisuals(int themeId)
{
    switch (themeId)
    {
    case ID_THEME_XTERM:
        return { RGB(0,0,0), RGB(229,229,229), RGB(0,0,0), RGB(229,229,229), RGB(18,18,18), RGB(240,240,240) };
    case ID_THEME_CAMPBELL:
        return { RGB(12,12,12), RGB(204,204,204), RGB(12,12,12), RGB(242,242,242), RGB(24,24,24), RGB(245,245,245) };
    case ID_THEME_POWERSHELL:
        return { RGB(1,36,86), RGB(238,238,238), RGB(1,36,86), RGB(255,255,255), RGB(7,50,110), RGB(255,255,255) };
    case ID_THEME_ALMALINUX:
        return { RGB(18,18,18), RGB(229,231,235), RGB(18,18,18), RGB(255,255,255), RGB(30,30,30), RGB(245,245,245) };
    case ID_THEME_DARKPLUS:
        return { RGB(30,30,30), RGB(212,212,212), RGB(30,30,30), RGB(240,240,240), RGB(45,45,45), RGB(245,245,245) };
    case ID_THEME_DIMIDIUM:
        return { RGB(16,16,16), RGB(210,210,210), RGB(16,16,16), RGB(245,245,245), RGB(30,30,30), RGB(245,245,245) };
    case ID_THEME_IBM5153:
        return { RGB(0,0,0), RGB(170,170,170), RGB(0,0,0), RGB(255,255,255), RGB(20,20,20), RGB(255,255,255) };
    case ID_THEME_ONE_HALF_DARK:
        return { RGB(40,44,52), RGB(220,223,228), RGB(40,44,52), RGB(255,255,255), RGB(55,60,70), RGB(245,245,245) };
    case ID_THEME_ONE_HALF_LIGHT:
        return { RGB(250,250,250), RGB(56,58,66), RGB(255,255,255), RGB(30,30,30), RGB(240,240,240), RGB(40,40,40) };
    case ID_THEME_OTTOSSON:
        return { RGB(22,22,22), RGB(220,220,220), RGB(22,22,22), RGB(255,255,255), RGB(35,35,35), RGB(245,245,245) };
    case ID_THEME_SOLARIZED_DARK:
        return { RGB(0,43,54), RGB(131,148,150), RGB(7,54,66), RGB(147,161,161), RGB(0,54,66), RGB(238,232,213) };
    case ID_THEME_SOLARIZED_LIGHT:
        return { RGB(253,246,227), RGB(101,123,131), RGB(253,246,227), RGB(88,110,117), RGB(238,232,213), RGB(88,110,117) };
    case ID_THEME_TANGO_DARK:
        return { RGB(0,0,0), RGB(211,215,207), RGB(0,0,0), RGB(238,238,236), RGB(24,24,24), RGB(238,238,236) };
    case ID_THEME_TANGO_LIGHT:
        return { RGB(238,238,236), RGB(46,52,54), RGB(255,255,255), RGB(30,30,30), RGB(245,245,245), RGB(46,52,54) };
    case ID_THEME_UBUNTU:
        return { RGB(48,10,36), RGB(238,238,238), RGB(48,10,36), RGB(255,255,255), RGB(68,20,50), RGB(255,255,255) };
    case ID_THEME_UBUNTU_2004:
        return { RGB(46,18,34), RGB(238,238,236), RGB(46,18,34), RGB(255,255,255), RGB(66,26,48), RGB(255,255,255) };
    case ID_THEME_UBUNTU_2204:
        return { RGB(45,12,41), RGB(238,238,236), RGB(45,12,41), RGB(255,255,255), RGB(64,20,58), RGB(255,255,255) };
    case ID_THEME_UBUNTU_COLOR:
        return { RGB(46,52,54), RGB(211,215,207), RGB(46,52,54), RGB(238,238,236), RGB(60,66,68), RGB(245,245,245) };
    case ID_THEME_VINTAGE:
        return { RGB(0,0,0), RGB(192,192,192), RGB(0,0,0), RGB(255,255,255), RGB(20,20,20), RGB(240,240,240) };
    case ID_THEME_CGA:
        return { RGB(0,0,0), RGB(170,170,170), RGB(0,0,0), RGB(255,255,255), RGB(15,15,15), RGB(255,255,255) };
    case ID_THEME_WINDOWS:
    default:
        return { RGB(12,12,12), RGB(204,204,204), RGB(12,12,12), RGB(242,242,242), RGB(24,24,24), RGB(245,245,245) };
    }
}

static void DrawAnsiThemePreview(HDC hdc, const RECT& rc, int themeId)
{
    const COLORREF* table = GetAnsiThemeTable(themeId);
    ThemeVisuals tv = GetThemeVisuals(themeId);

    UniqueGdiObject hBack(CreateSolidBrush(tv.logBack));
    if (hBack.IsValid())
        FillRect(hdc, &rc, (HBRUSH)hBack.Get());

    RECT titlePanel = { rc.left + 12, rc.top + 12, rc.right - 12, rc.top + 64 };
    if (hBack.IsValid())
        FillRect(hdc, &titlePanel, (HBRUSH)hBack.Get());

    int left = rc.left + 18;
    int top = rc.top + 18;
    int box = 22;
    int gap = 6;

    for (int i = 0; i < 8; ++i)
    {
        RECT cell = { left + i * (box + gap), top, left + i * (box + gap) + box, top + box };
        UniqueGdiObject b(CreateSolidBrush(table[i]));
        if (b.IsValid())
            FillRect(hdc, &cell, (HBRUSH)b.Get());
        FrameRect(hdc, &cell, (HBRUSH)GetStockObject(GRAY_BRUSH));
    }

    top += box + 8;

    for (int i = 8; i < 16; ++i)
    {
        RECT cell = { left + (i - 8) * (box + gap), top, left + (i - 8) * (box + gap) + box, top + box };
        UniqueGdiObject b(CreateSolidBrush(table[i]));
        if (b.IsValid())
            FillRect(hdc, &cell, (HBRUSH)b.Get());
        FrameRect(hdc, &cell, (HBRUSH)GetStockObject(GRAY_BRUSH));
    }

    RECT textPanel = { rc.left + 12, rc.top + 78, rc.right - 12, rc.bottom - 12 };
    if (hBack.IsValid())
        FillRect(hdc, &textPanel, (HBRUSH)hBack.Get());

    SetBkMode(hdc, TRANSPARENT);
    ScopedSelectObject fontSelect(hdc, g_app ? g_app->hFontLog : (HFONT)GetStockObject(DEFAULT_GUI_FONT));

    int textY = rc.top + 88;

    SetTextColor(hdc, tv.panelText);
    TextOutW(hdc, left, textY, L"[테마 미리보기]", 9);

    textY += 32;

    const wchar_t* title = GetThemeNameById(themeId);
    SetTextColor(hdc, tv.logText);
    TextOutW(hdc, left, textY, title, (int)wcslen(title));

    textY += 34;

    struct ColorPairLine
    {
        int normalIndex;
        int brightIndex;
        const wchar_t* normalName;
        const wchar_t* brightName;
    };

    const ColorPairLine lines[] =
    {
        { 1, 9,  L"빨강 샘플",   L"밝은 빨강 샘플" },
        { 2, 10, L"초록 샘플",   L"밝은 초록 샘플" },
        { 3, 11, L"노랑 샘플",   L"밝은 노랑 샘플" },
        { 4, 12, L"파랑 샘플",   L"밝은 파랑 샘플" },
        { 5, 13, L"자홍 샘플",   L"밝은 자홍 샘플" },
        { 6, 14, L"청록 샘플",   L"밝은 청록 샘플" },
        { 7, 15, L"흰색/회색 샘플", L"밝은 흰색 샘플" },
        { 0, 8,  L"검정 샘플",   L"밝은 검정/회색 샘플" },
    };

    // 미리보기 패널 폭이 좁아도 오른쪽 밝은 색 샘플이 잘리지 않도록
    // 두 번째 컬럼 간격을 줄여서 그립니다.
    for (const auto& line : lines)
    {
        SetTextColor(hdc, table[line.normalIndex]);
        TextOutW(hdc, left, textY, line.normalName, (int)wcslen(line.normalName));

        SetTextColor(hdc, table[line.brightIndex]);
        TextOutW(hdc, left + 145, textY, line.brightName, (int)wcslen(line.brightName));

        textY += 24;
    }

    textY += 8;
    SetTextColor(hdc, tv.inputText);
    TextOutW(hdc, left, textY, L"[입력창 예시] 점수", 13);

}

static bool IsKnownThemeLogBackColor(COLORREF c)
{
    // RecolorTheme() calls this for every terminal cell. Build the theme
    // background table once so a theme change does not repeatedly walk the
    // full theme registry for each history/live cell.
    static bool initialized = false;
    static COLORREF knownBacks[32] = {};
    static int knownCount = 0;

    if (!initialized)
    {
        const int themes[] = {
            ID_THEME_WINDOWS, ID_THEME_XTERM, ID_THEME_CAMPBELL, ID_THEME_POWERSHELL,
            ID_THEME_ALMALINUX, ID_THEME_DARKPLUS, ID_THEME_DIMIDIUM, ID_THEME_IBM5153,
            ID_THEME_ONE_HALF_DARK, ID_THEME_ONE_HALF_LIGHT, ID_THEME_OTTOSSON,
            ID_THEME_SOLARIZED_DARK, ID_THEME_SOLARIZED_LIGHT, ID_THEME_TANGO_DARK,
            ID_THEME_TANGO_LIGHT, ID_THEME_UBUNTU, ID_THEME_UBUNTU_2004,
            ID_THEME_UBUNTU_2204, ID_THEME_UBUNTU_COLOR, ID_THEME_VINTAGE, ID_THEME_CGA
        };

        for (int i = 0; i < (int)(sizeof(themes) / sizeof(themes[0])) && knownCount < (int)(sizeof(knownBacks) / sizeof(knownBacks[0])); ++i)
        {
            ThemeVisuals tv = GetThemeVisuals(themes[i]);
            bool exists = false;
            for (int j = 0; j < knownCount; ++j)
            {
                if (knownBacks[j] == tv.logBack)
                {
                    exists = true;
                    break;
                }
            }
            if (!exists)
                knownBacks[knownCount++] = tv.logBack;
        }
        initialized = true;
    }

    for (int i = 0; i < knownCount; ++i)
    {
        if (knownBacks[i] == c)
            return true;
    }
    return false;
}

void ApplyThemeVisualsToApp(int themeId)
{
    if (!g_app)
        return;

    COLORREF oldBg = g_app->logStyle.backColor;
    COLORREF oldFg = g_app->logStyle.textColor;

    ThemeVisuals tv = GetThemeVisuals(themeId);

    g_app->logStyle.backColor = tv.logBack;
    g_app->logStyle.textColor = tv.logText;
    g_app->inputStyle.backColor = tv.inputBack;
    g_app->inputStyle.textColor = tv.inputText;
    g_app->chatStyle.backColor = tv.logBack;
    g_app->chatStyle.textColor = tv.logText;
    g_app->mainBackColor = tv.panelBack;

    if (g_app->termBuffer)
    {
        std::shared_ptr<TerminalBuffer> buffer = g_app->termBuffer;
        HWND notifyHwnd = g_app->hwndMain;
        const unsigned int recolorSerial = ++s_themeRecolorSerial;
        buffer->SetDefaultColors(tv.logBack, tv.logText);
        std::thread([buffer, notifyHwnd, oldBg, oldFg, newBg = tv.logBack, newFg = tv.logText, recolorSerial]() {
            if (recolorSerial != s_themeRecolorSerial.load())
                return;

            if (buffer)
                buffer->RecolorExistingTheme(oldBg, oldFg, newBg, newFg, IsKnownThemeLogBackColor);

            if (recolorSerial == s_themeRecolorSerial.load() && notifyHwnd && IsWindow(notifyHwnd))
                PostMessageW(notifyHwnd, WM_APP_THEME_RECOLOR_DONE, 0, 0);
        }).detach();
    }

    for (int i = 0; i < (int)(sizeof(g_app->hwndEdit) / sizeof(g_app->hwndEdit[0])); ++i)
    {
        if (g_app->hwndEdit[i])
        {
            SendMessageW(g_app->hwndEdit[i], EM_SETBKGNDCOLOR, 0, g_app->inputStyle.backColor);

            CHARFORMAT2W cf = {};
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_COLOR | CFM_BACKCOLOR;
            cf.crTextColor = g_app->inputStyle.textColor;
            cf.crBackColor = g_app->inputStyle.backColor;
            SendMessageW(g_app->hwndEdit[i], EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);

            InvalidateRect(g_app->hwndEdit[i], nullptr, FALSE);
        }
    }

    if (g_app->hwndChat)
    {
        SendMessageW(g_app->hwndChat, EM_SETBKGNDCOLOR, 0, g_app->chatStyle.backColor);

        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR | CFM_BACKCOLOR;
        cf.crTextColor = g_app->chatStyle.textColor;
        cf.crBackColor = g_app->chatStyle.backColor;
        SendMessageW(g_app->hwndChat, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);

        InvalidateRect(g_app->hwndChat, nullptr, FALSE);
    }

    ApplyStyles();

    if (g_app->hwndInput)
    {
        InvalidateRect(g_app->hwndInput, nullptr, FALSE);
    }

    if (g_app->hwndLog)
    {
        SendMessageW(g_app->hwndLog, EM_SETBKGNDCOLOR, 0, g_app->logStyle.backColor);

        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR | CFM_BACKCOLOR;
        cf.crTextColor = g_app->logStyle.textColor;
        cf.crBackColor = g_app->logStyle.backColor;

        SendMessageW(g_app->hwndLog, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);

        InvalidateRect(g_app->hwndLog, nullptr, FALSE);
    }

    if (g_app->hwndMain)
    {
        LayoutChildren(g_app->hwndMain);
        InvalidateRect(g_app->hwndMain, nullptr, FALSE);
    }
}

// ==============================================
// 테마 대화상자 프로시저
// ==============================================
static LRESULT CALLBACK ThemePreviewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ThemeDialogState* state = (ThemeDialogState*)GetPropW(hwnd, L"ThemeDialogState");

    switch (msg)
    {
    case WM_PAINT:
    {
        ScopedPaintDC paint(hwnd);
        HDC hdc = paint.Get();
        if (!hdc) return 0;
        RECT rc;
        GetClientRect(hwnd, &rc);
        int themeId = (state ? state->selectedTheme : ID_THEME_WINDOWS);
        DrawAnsiThemePreview(hdc, rc, themeId);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK ThemeDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ThemeDialogState* state = (ThemeDialogState*)GetPropW(hwnd, L"ThemeDialogState");
    switch (msg)
    {
    case WM_CREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        ThemeDialogState* initState = (ThemeDialogState*)cs->lpCreateParams;
        SetPropW(hwnd, L"ThemeDialogState", (HANDLE)initState);
        state = initState;

        state->hwndList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
            12, 12, 190, 470, hwnd, (HMENU)100, GetModuleHandleW(nullptr), nullptr);

        state->hwndPreview = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE, 224, 12, 340, 470, hwnd, (HMENU)101, GetModuleHandleW(nullptr), nullptr);

        // ★ 단축키 추가
        state->hwndOk = CreateWindowExW(0, L"BUTTON", L"확인(&O)",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 384, 494, 84, 30, hwnd, (HMENU)IDOK, GetModuleHandleW(nullptr), nullptr);

        state->hwndCancel = CreateWindowExW(0, L"BUTTON", L"취소(&C)",
            WS_CHILD | WS_VISIBLE, 480, 494, 84, 30, hwnd, (HMENU)IDCANCEL, GetModuleHandleW(nullptr), nullptr);

        SetPropW(state->hwndPreview, L"ThemeDialogState", (HANDLE)state);
        SetWindowLongPtrW(state->hwndPreview, GWLP_WNDPROC, (LONG_PTR)ThemePreviewProc);

        for (int i = 0; i < kAnsiThemeCount; ++i)
        {
            SendMessageW(state->hwndList, LB_ADDSTRING, 0, (LPARAM)kAnsiThemes[i].name);
            if (kAnsiThemes[i].id == state->selectedTheme)
            {
                SendMessageW(state->hwndList, LB_SETCURSEL, i, 0);
            }
        }
        SendMessageW(state->hwndList, LB_SETITEMHEIGHT, 0, 22);
        return 0;
    }

    // ★★★ ALT 단축키 처리 추가 ★★★
    case WM_SYSCHAR:
    {
        wchar_t ch = towlower((wchar_t)wParam);
        if (ch == 'o')
        {
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
            return 0;
        }
        if (ch == 'c')
        {
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
            return 0;
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case 100:
            if (HIWORD(wParam) == LBN_SELCHANGE && state)
            {
                int sel = (int)SendMessageW(state->hwndList, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < kAnsiThemeCount)
                {
                    state->selectedTheme = kAnsiThemes[sel].id;
                    InvalidateRect(state->hwndPreview, nullptr, FALSE);
                }
            }
            return 0;
        case IDOK:
            if (state) state->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        case IDCANCEL:
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_MEASUREITEM:
    {
        MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*)lParam;
        if (mis && mis->CtlID == 100)
        {
            mis->itemHeight = 16;
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis && dis->CtlID == 100)
        {
            if (dis->itemID == (UINT)-1) return TRUE;
            wchar_t text[128] = {};
            SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, (LPARAM)text);
            COLORREF bg = (dis->itemState & ODS_SELECTED) ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_WINDOW);
            COLORREF fg = (dis->itemState & ODS_SELECTED) ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_WINDOWTEXT);
            UniqueGdiObject hbr(CreateSolidBrush(bg));
            if (hbr.IsValid())
                FillRect(dis->hDC, &dis->rcItem, (HBRUSH)hbr.Get());
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, fg);
            ScopedSelectObject fontSelect(dis->hDC, GetPopupUIFont(hwnd));
            RECT rc = dis->rcItem;
            rc.left += 8;
            DrawTextW(dis->hDC, text, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        RemovePropW(hwnd, L"ThemeDialogState");
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ShowThemeDialog(HWND owner, int* selectedTheme)
{
    if (!selectedTheme)
        return false;

    static const wchar_t* kThemeDialogClass = L"TTGuiThemeDialogClass";
    static bool s_registered = false;

    if (!s_registered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = ThemeDialogProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kThemeDialogClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        s_registered = true;
    }

    ThemeDialogState state = {};
    state.originalTheme = *selectedTheme;
    state.selectedTheme = *selectedTheme;
    state.accepted = false;

    int dlgW = 600;
    int dlgH = 580;

    RECT rcOwner = {};
    if (owner && IsWindow(owner))
    {
        GetWindowRect(owner, &rcOwner);
    }
    else
    {
        rcOwner.left = 0;
        rcOwner.top = 0;
        rcOwner.right = GetSystemMetrics(SM_CXSCREEN);
        rcOwner.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    int ownerW = rcOwner.right - rcOwner.left;
    int ownerH = rcOwner.bottom - rcOwner.top;
    int x = rcOwner.left + (ownerW - dlgW) / 2;
    int y = rcOwner.top + (ownerH - dlgH) / 2;

    if (x < 0) x = 0;
    if (y < 0) y = 0;

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kThemeDialogClass, L"ANSI 테마 선택",
        WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE,
        x, y, dlgW, dlgH, owner, nullptr, GetModuleHandleW(nullptr), &state);

    if (!hwnd) return false;

    HFONT hFont = GetPopupUIFont(hwnd);
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)hFont, TRUE);
    EnumChildWindows(hwnd, [](HWND child, LPARAM lParam) -> BOOL {
        SendMessageW(child, WM_SETFONT, lParam, TRUE);
        return TRUE;
        }, (LPARAM)hFont);

    EnableWindow(owner, FALSE);

    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0))
    {
        if (!IsDialogMessageW(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);

    if (state.accepted)
    {
        *selectedTheme = state.selectedTheme;
        return true;
    }
    return false;
}

void ApplyStyles() {
    if (!g_app) return;

    g_app->logStyle.font.lfQuality = GetCurrentFontQuality();
    g_app->inputStyle.font.lfQuality = GetCurrentFontQuality();
    g_app->chatStyle.font.lfQuality = GetCurrentFontQuality();

    // ★ 핵심: 로그창 폰트만 별도 보정
    LOGFONTW logLf = g_app->logStyle.font;
    logLf.lfCharSet = HANGEUL_CHARSET;
    logLf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    logLf.lfOutPrecision = OUT_TT_ONLY_PRECIS;

    UniqueGdiObject newLogFont(CreateFontIndirectW(&logLf));
    UniqueGdiObject newInputFont(CreateFontIndirectW(&g_app->inputStyle.font));
    UniqueGdiObject newChatFont(CreateFontIndirectW(&g_app->chatStyle.font));

    if (!newLogFont.IsValid() || !newInputFont.IsValid() || !newChatFont.IsValid())
        return;

    UniqueGdiObject oldLog(g_app->hFontLog);
    UniqueGdiObject oldInput(g_app->hFontInput);
    UniqueGdiObject oldChat(g_app->hFontChat);
    (void)oldLog;
    (void)oldInput;
    (void)oldChat;

    g_app->hFontLog = (HFONT)newLogFont.Release();
    g_app->hFontInput = (HFONT)newInputFont.Release();
    g_app->hFontChat = (HFONT)newChatFont.Release();
    ResetLogCellPixelSizeCache();

    RebuildInputBrushes();

    if (g_app->hwndLog)
        SendMessageW(g_app->hwndLog, WM_SETFONT, (WPARAM)g_app->hFontLog, TRUE);

    if (g_app->hwndInput)
        SendMessageW(g_app->hwndInput, WM_SETFONT, (WPARAM)g_app->hFontInput, TRUE);

    if (g_app->hwndChat) {
        SendMessageW(g_app->hwndChat, WM_SETFONT, (WPARAM)g_app->hFontChat, TRUE);
        SetupChatRichEditDefaults(g_app->hwndChat);
        InvalidateRect(g_app->hwndChat, nullptr, FALSE);
    }

    for (int i = 0; i < INPUT_ROWS; ++i) {
        if (g_app->hwndEdit[i]) {
            SendMessageW(g_app->hwndEdit[i], WM_SETFONT, (WPARAM)g_app->hFontInput, TRUE);
            SendMessageW(g_app->hwndEdit[i], EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
            SetWindowTheme(g_app->hwndEdit[i], L"", L"");
            InvalidateRect(g_app->hwndEdit[i], nullptr, FALSE);
        }
    }

    for (int i = 0; i < SHORTCUT_BUTTON_COUNT; ++i) {
        if (g_app->hwndShortcutButtons[i]) {
            SendMessageW(
                g_app->hwndShortcutButtons[i],
                WM_SETFONT,
                (WPARAM)GetShortcutButtonUIFont(g_app->hwndMain ? g_app->hwndMain : g_app->hwndShortcutButtons[i]),
                TRUE);
            InvalidateRect(g_app->hwndShortcutButtons[i], nullptr, FALSE);
        }
    }

    if (g_app->hwndShortcutBar)
        InvalidateRect(g_app->hwndShortcutBar, nullptr, FALSE);

    // buildfix26: 이미 열려 있는 갈무리 보기 창도 메인 출력창 폰트를 즉시 따라가게 합니다.
    ApplyTailWindowFonts();

    RecalcInputMetrics();

    if (g_app->hwndMain)
        LayoutChildren(g_app->hwndMain);

    if (g_app->hwndLog)
        InvalidateRect(g_app->hwndLog, nullptr, FALSE);

    if (g_app->hwndInput)
        InvalidateRect(g_app->hwndInput, nullptr, FALSE);

    if (g_app->hwndMain)
        InvalidateRect(g_app->hwndMain, nullptr, FALSE);

    if (g_app->proc.hPC)
        ResizePseudoConsoleToLogWindow();

    if (g_app->hwndEdit[g_app->activeEditIndex] &&
        GetFocus() == g_app->hwndEdit[g_app->activeEditIndex])
    {
        EnsureVisibleEditCaret(g_app->hwndEdit[g_app->activeEditIndex]);
    }
}


namespace
{
    constexpr size_t kMaxGuiVarNameBytes = 256;
    constexpr size_t kMaxGuiVarValueBytes = 4096;

    bool PostGuiVarUpdate(std::string_view nameBytes, std::string_view valueBytes)
    {
        if (!g_app || !g_app->hwndMain || nameBytes.empty() ||
            nameBytes.size() > kMaxGuiVarNameBytes ||
            valueBytes.size() > kMaxGuiVarValueBytes)
        {
            return false;
        }

        std::string nameUtf8(nameBytes);
        std::string valueUtf8(valueBytes);
        std::wstring* name = new (std::nothrow) std::wstring(Utf8ToWide(nameUtf8));
        std::wstring* value = new (std::nothrow) std::wstring(Utf8ToWide(valueUtf8));
        if (!name || !value)
        {
            delete name;
            delete value;
            return false;
        }

        if (!PostMessageW(g_app->hwndMain, WM_APP_VAR_UPDATE,
            reinterpret_cast<WPARAM>(name), reinterpret_cast<LPARAM>(value)))
        {
            delete name;
            delete value;
            return false;
        }
        return true;
    }
}

void AnsiToRunsParser::HandleOsc()
{
    // oscParams_에 담긴 내용이 비어있거나 앱 객체가 없으면 중단
    if (oscParams_.empty() || !g_app) return;

    // OSC 신호는 보통 "번호;데이터" 형식입니다.
    // 우리가 보낸 신호는 "0;GUI_VAR:..." 형태이므로 이를 분석합니다.
    constexpr std::string_view kGuiVarPrefix = "GUI_VAR:";
    std::string_view osc(oscParams_);
    size_t semi = osc.find(';');
    if (semi != std::string_view::npos) {
        std::string_view type = osc.substr(0, semi);
        std::string_view payload = osc.substr(semi + 1);

        // 창 제목 변경 규약(0번 또는 2번)을 가로채서 GUI 변수 업데이트용으로 사용
        if (type.size() == 1 && (type[0] == '0' || type[0] == '2') &&
            payload.size() >= kGuiVarPrefix.size() &&
            payload.compare(0, kGuiVarPrefix.size(), kGuiVarPrefix) == 0) {
            std::string_view varData = payload.substr(kGuiVarPrefix.size());
            size_t eq = varData.find('=');

            if (eq != std::string_view::npos) {
                PostGuiVarUpdate(varData.substr(0, eq), varData.substr(eq + 1));
            }
        }
    }

    // 처리가 끝났으므로 바구니를 비웁니다.
    oscParams_.clear();
}
