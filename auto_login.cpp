#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "auto_login.h"
#include "settings.h"
#include <algorithm>
#include <cwctype>
#include <ctime>

static const DWORD kAutoLoginScanMs = 60 * 1000;

static bool ContainsTextI(const std::wstring& haystack, const std::wstring& needle)
{
    if (needle.empty())
        return false;

    std::wstring h = haystack;
    std::wstring n = needle;

    std::transform(h.begin(), h.end(), h.begin(),
        [](wchar_t ch) { return (wchar_t)towlower(ch); });

    std::transform(n.begin(), n.end(), n.begin(),
        [](wchar_t ch) { return (wchar_t)towlower(ch); });

    return h.find(n) != std::wstring::npos;
}

static bool IsAnyPatternLine(const std::wstring& line,
    const std::wstring& p1, const std::wstring& p2, const std::wstring& p3)
{
    return ContainsTextI(line, p1) || ContainsTextI(line, p2) || ContainsTextI(line, p3);
}

static bool IsAutoLoginIdFailLine(const std::wstring& line)
{
    if (!g_app) return false;

    // 기존 success1/success2 저장 칸을 "아이디 실패패턴"으로 재사용합니다.
    if (g_app->hasActiveSession)
    {
        const AddressBookEntry& e = g_app->activeSession;
        if (IsAnyPatternLine(line, e.loginSuccessPattern1, e.loginSuccessPattern2, L""))
            return true;
    }

    return IsAnyPatternLine(line,
        g_app->autoLoginSuccessPattern1,
        g_app->autoLoginSuccessPattern2,
        L"");
}

static bool IsAutoLoginPwFailLine(const std::wstring& line)
{
    if (!g_app) return false;

    // 기존 fail1/fail2 저장 칸을 "비밀번호 실패패턴"으로 재사용합니다.
    if (g_app->hasActiveSession)
    {
        const AddressBookEntry& e = g_app->activeSession;
        if (IsAnyPatternLine(line, e.loginFailPattern1, e.loginFailPattern2, L""))
            return true;
    }

    return IsAnyPatternLine(line,
        g_app->autoLoginFailPattern1,
        g_app->autoLoginFailPattern2,
        L"");
}

static bool LooksLikeTinTinConnectionSuccess(const std::wstring& line)
{
    return ContainsTextI(line, L"서버에 접속 성공") ||
           ContainsTextI(line, L"session connected") ||
           ContainsTextI(line, L"connected to");
}

static bool LooksLikeTinTinConnectionDown(const std::wstring& line)
{
    if (line.empty())
        return false;

    if (ContainsTextI(line, L"현재 세션이 활성화되어 있지") ||
        ContainsTextI(line, L"no session is active") ||
        ContainsTextI(line, L"no active session"))
        return true;

    if (ContainsTextI(line, L"write_line_mud") ||
        ContainsTextI(line, L"broken pipe") ||
        ContainsTextI(line, L"connection refused") ||
        ContainsTextI(line, L"connection failed") ||
        ContainsTextI(line, L"connection reset") ||
        ContainsTextI(line, L"no route to host") ||
        ContainsTextI(line, L"timed out") ||
        ContainsTextI(line, L"time out") ||
        ContainsTextI(line, L"연결 시간이 초과") ||
        ContainsTextI(line, L"연결 시간 초과") ||
        ContainsTextI(line, L"접속 실패") ||
        ContainsTextI(line, L"연결 실패") ||
        ContainsTextI(line, L"연결 거부"))
        return true;

    bool sessionLine = ContainsTextI(line, L"세션") || ContainsTextI(line, L"session");
    if (sessionLine &&
        (ContainsTextI(line, L"종료") ||
         ContainsTextI(line, L"닫혔") ||
         ContainsTextI(line, L"closed") ||
         ContainsTextI(line, L"disconnected")))
        return true;

    return false;
}

static void CheckAddressBookAutoReconnectFromText(const std::wstring& text)
{
    if (!g_app || text.empty())
        return;

    if (LooksLikeTinTinConnectionSuccess(text))
    {
        g_app->isConnected = true;
        g_app->isSessionActive = true;
        g_app->lastConnectionSuccessTick = GetTickCount();
        if (g_app->sessionStartTime == 0)
            g_app->sessionStartTime = time(NULL);
        if (g_app->hwndMain)
            KillTimer(g_app->hwndMain, ID_TIMER_AUTORECONNECT);
        return;
    }

    if (!LooksLikeTinTinConnectionDown(text))
        return;

    g_app->isConnected = false;
    g_app->isSessionActive = false;
    g_app->lastConnectionDownTick = GetTickCount();
    g_app->autoLoginWindowActive = false;
    g_app->keepAliveBlockedUntilTick = GetTickCount() + 3000;

    if (!g_app->hasActiveSession || !g_app->activeSession.autoReconnect)
        return;

    if (g_app->hwndMain)
    {
        KillTimer(g_app->hwndMain, ID_TIMER_AUTORECONNECT);
        SetTimer(g_app->hwndMain, ID_TIMER_AUTORECONNECT, 3000, nullptr);
    }
}

void StopAutoLoginWindow(bool allowKeepAlive)
{
    if (!g_app) return;

    g_app->autoLoginWindowActive = false;
    g_app->autoLoginTriggered = true;

    if (allowKeepAlive)
        g_app->keepAliveBlockedUntilTick = 0;
    else
        g_app->keepAliveBlockedUntilTick = GetTickCount() + kAutoLoginScanMs;
}

void StartAutoLoginWindowFromGlobal()
{
    if (!g_app) return;

    DWORD now = GetTickCount();
    g_app->activeAutoLoginEnabled = g_app->autoLoginEnabled;
    g_app->activeAutoLoginIdPattern = g_app->autoLoginIdPattern;
    g_app->activeAutoLoginId = g_app->autoLoginId;
    g_app->activeAutoLoginPwPattern = g_app->autoLoginPwPattern;
    g_app->activeAutoLoginPw = g_app->autoLoginPw;

    g_app->autoLoginState = 0;
    g_app->lastConnectionSuccessTick = 0;
    g_app->lastConnectionDownTick = now;
    g_app->isConnected = false;
    g_app->isSessionActive = false;
    g_app->autoLoginStartTick = now;
    g_app->autoLoginWindowActive = true;
    g_app->autoLoginTriggered = false;
    g_app->keepAliveBlockedUntilTick = now + kAutoLoginScanMs;
}

void StartAutoLoginWindowForAddressEntry(const AddressBookEntry& entry)
{
    if (!g_app) return;

    DWORD now = GetTickCount();
    if (entry.autoLoginEnabled)
    {
        g_app->activeAutoLoginEnabled = true;
        g_app->activeAutoLoginIdPattern = entry.alIdPattern;
        g_app->activeAutoLoginId = entry.alId;
        g_app->activeAutoLoginPwPattern = entry.alPwPattern;
        g_app->activeAutoLoginPw = entry.alPw;
    }
    else
    {
        g_app->activeAutoLoginEnabled = g_app->autoLoginEnabled;
        g_app->activeAutoLoginIdPattern = g_app->autoLoginIdPattern;
        g_app->activeAutoLoginId = g_app->autoLoginId;
        g_app->activeAutoLoginPwPattern = g_app->autoLoginPwPattern;
        g_app->activeAutoLoginPw = g_app->autoLoginPw;
    }

    g_app->autoLoginState = 0;
    g_app->lastConnectionSuccessTick = 0;
    g_app->lastConnectionDownTick = now;
    g_app->isConnected = false;
    g_app->isSessionActive = false;
    g_app->autoLoginStartTick = now;
    g_app->autoLoginWindowActive = true;
    g_app->autoLoginTriggered = false;
    g_app->keepAliveBlockedUntilTick = now + kAutoLoginScanMs;
}

bool IsAutoLoginKeepAliveBlocked()
{
    if (!g_app) return false;

    DWORD now = GetTickCount();

    if (g_app->autoLoginWindowActive)
    {
        if (now - g_app->autoLoginStartTick < kAutoLoginScanMs)
            return true;

        // 1분이 지나면 패턴 검사를 자동 종료하고 접속유지를 허용합니다.
        StopAutoLoginWindow(true);
        return false;
    }

    if (g_app->keepAliveBlockedUntilTick)
    {
        if ((LONG)(now - g_app->keepAliveBlockedUntilTick) < 0)
            return true;
        g_app->keepAliveBlockedUntilTick = 0;
    }

    return false;
}

void NotifyPossibleConnectionCommand(const std::wstring& text)
{
    if (!g_app) return;

    std::wstring s = Trim(text);
    std::transform(s.begin(), s.end(), s.begin(), towlower);

    if (s.rfind(L"#session", 0) == 0 || s.rfind(L"#ses", 0) == 0 || s.rfind(L"#connect", 0) == 0)
    {
        // 주소록 연결은 ConnectAddressBookEntry에서 먼저 주소록용 설정을 올립니다.
        // 이미 창이 켜져 있으면 덮어쓰지 않습니다.
        if (!g_app->autoLoginWindowActive)
            StartAutoLoginWindowFromGlobal();
    }
}

void RunAutoLoginEngine(const std::wstring& text)
{
    if (!g_app || text.empty())
        return;

    // 주소록에서 "자동 재연결"을 켠 경우에는 TinTin++의 접속 실패/종료 문구를
    // 가볍게 단순 문자열로만 감지해서 3초 뒤 같은 주소록 항목으로 다시 접속합니다.
    CheckAddressBookAutoReconnectFromText(text);

    if (!g_app->autoLoginWindowActive)
        return;

    DWORD now = GetTickCount();
    if (now - g_app->autoLoginStartTick >= kAutoLoginScanMs)
    {
        StopAutoLoginWindow(true);
        return;
    }

    // buildfix33: 아이디/비밀번호 실패 패턴 검사는 제거했습니다.
    // 자동 로그인 패턴 검사는 접속 후 60초 동안 아이디 요청/비밀번호 요청 문구만 확인하고,
    // 아이디와 비밀번호를 전송하면 즉시 꺼집니다.

    if (!g_app->activeAutoLoginEnabled)
        return;

    if (g_app->autoLoginState == 0 &&
        !g_app->activeAutoLoginId.empty() &&
        ContainsTextI(text, g_app->activeAutoLoginIdPattern))
    {
        SendCommandToProcess(g_app->activeAutoLoginId);
        g_app->autoLoginState = 1;
        return;
    }

    if (g_app->autoLoginState <= 1 &&
        !g_app->activeAutoLoginPw.empty() &&
        ContainsTextI(text, g_app->activeAutoLoginPwPattern))
    {
        SendCommandToProcess(g_app->activeAutoLoginPw);
        g_app->autoLoginState = 2;

        // 사용자의 요구에 맞춰 아이디/비밀번호 전송까지 끝나면 1분이 안 되었어도 검사를 끕니다.
        StopAutoLoginWindow(true);
        return;
    }
}
