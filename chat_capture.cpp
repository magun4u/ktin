#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "chat_capture.h"
#include "resource.h"
#include "settings.h"
#include <regex>
#include <shellapi.h>
#include <mutex>

static std::string g_capturePendingUtf8;
static DWORD g_captureLastFlushTick = 0;
static std::mutex g_captureLogMutex;

ChatCaptureItem g_chatCaptures[10];

// ==============================================
// 내부 헬퍼 함수
// ==============================================
std::wstring MakeCaptureLogTimestamp()
{
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[32];
    wsprintfW(buf, L"%04u%02u%02u%02u%02u%02u",
        (UINT)st.wYear, (UINT)st.wMonth, (UINT)st.wDay,
        (UINT)st.wHour, (UINT)st.wMinute, (UINT)st.wSecond);
    return buf;
}

// ==============================================
// 채팅 캡처 엔진
// ==============================================
void AppendChatWindowText(const std::wstring& text)
{
    // 안전판: RichEdit 채팅 캡처창은 장시간 실행 시 누적 부하가 커질 수 있어 제거합니다.
    (void)text;
}


void RunChatCaptureEngine(const std::wstring& cleanLine)
{
    if (!g_app || !g_app->hwndChat || cleanLine.empty()) return;

    for (int i = 0; i < 10; ++i)
    {
        if (!g_chatCaptures[i].active || g_chatCaptures[i].pattern.empty())
            continue;

        // %1, %2 ... → (.*?) 정규식으로 변환
        std::wstring regPattern = g_chatCaptures[i].pattern;
        for (int j = 1; j <= 9; ++j)
        {
            wchar_t var[10];
            wsprintfW(var, L"%%%d", j);
            size_t pos = 0;
            while ((pos = regPattern.find(var, pos)) != std::wstring::npos)
            {
                regPattern.replace(pos, 2, L"(.*?)");
                pos += 5;
            }
        }

        try
        {
            std::wregex e(regPattern);
            std::wsmatch m;
            if (std::regex_search(cleanLine, m, e))
            {
                std::wstring output = g_chatCaptures[i].format;

                // %1 ~ %9 치환
                for (int j = 1; j <= 9; ++j)
                {
                    if (j < (int)m.size())
                    {
                        wchar_t var[10];
                        wsprintfW(var, L"%%%d", j);
                        std::wstring matched = m[j].str();
                        size_t pos = 0;
                        while ((pos = output.find(var, pos)) != std::wstring::npos)
                        {
                            output.replace(pos, 2, matched);
                            pos += matched.length();
                        }
                    }
                }

                // 시간 출력 옵션
                if (g_app->chatTimestampEnabled)
                {
                    SYSTEMTIME st;
                    GetLocalTime(&st);
                    wchar_t timeBuf[32];
                    wsprintfW(timeBuf, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
                    output = std::wstring(timeBuf) + output;
                }

                AppendChatWindowText(output);
                break;   // 한 줄은 하나의 패턴에만 매칭
            }
        }
        catch (...) {
            // 잘못된 정규식 패턴 방어
        }
    }
}

// ==============================================
// 설정 로드/저장
// ==============================================
void LoadChatCaptureSettings()
{
    std::wstring path = GetSettingsPath();

    if (g_app)
        g_app->chatTimestampEnabled = GetPrivateProfileIntW(L"chat_capture", L"timestamp", 0, path.c_str()) != 0;

    for (int i = 0; i < 10; ++i)
    {
        wchar_t keyA[32], keyP[32], keyF[32], buf[1024] = {};
        wsprintfW(keyA, L"cap_act_%d", i);
        wsprintfW(keyP, L"cap_pat_%d", i);
        wsprintfW(keyF, L"cap_fmt_%d", i);

        g_chatCaptures[i].active = GetPrivateProfileIntW(L"chat_capture", keyA, 0, path.c_str()) != 0;
        GetPrivateProfileStringW(L"chat_capture", keyP, L"", buf, 1024, path.c_str());
        g_chatCaptures[i].pattern = buf;
        GetPrivateProfileStringW(L"chat_capture", keyF, L"", buf, 1024, path.c_str());
        g_chatCaptures[i].format = buf;
    }
}

void SaveChatCaptureSettings()
{
    std::wstring path = GetSettingsPath();

    WritePrivateProfileStringW(L"chat_capture", L"timestamp",
        g_app && g_app->chatTimestampEnabled ? L"1" : L"0", path.c_str());

    for (int i = 0; i < 10; ++i)
    {
        wchar_t keyA[32], keyP[32], keyF[32];
        wsprintfW(keyA, L"cap_act_%d", i);
        wsprintfW(keyP, L"cap_pat_%d", i);
        wsprintfW(keyF, L"cap_fmt_%d", i);

        WritePrivateProfileStringW(L"chat_capture", keyA, g_chatCaptures[i].active ? L"1" : L"0", path.c_str());
        WritePrivateProfileStringW(L"chat_capture", keyP, g_chatCaptures[i].pattern.c_str(), path.c_str());
        WritePrivateProfileStringW(L"chat_capture", keyF, g_chatCaptures[i].format.c_str(), path.c_str());
    }
}

void LoadCaptureLogSettings()
{
    if (!g_app) return;
    std::wstring path = GetSettingsPath();
    g_app->captureLogEnabled = GetPrivateProfileIntW(L"capture_log", L"enabled", 0, path.c_str()) != 0;
}

void SaveCaptureLogSettings()
{
    if (!g_app) return;
    std::wstring path = GetSettingsPath();
    WritePrivateProfileStringW(L"capture_log", L"enabled",
        g_app->captureLogEnabled ? L"1" : L"0", path.c_str());
}

// ==============================================
// 로그 파일 관련
// ==============================================
void StartCaptureLog()
{
    if (!g_app) return;
    StopCaptureLog();
    if (!g_app->captureLogEnabled) return;

    std::wstring logDir = MakeAbsolutePath(GetModuleDirectory(), L"log");
    CreateDirectoryW(logDir.c_str(), nullptr);

    std::wstring filePath = logDir;
    if (!filePath.empty() && filePath.back() != L'\\')
        filePath += L'\\';
    filePath += MakeCaptureLogTimestamp() + L".txt";

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hFile != INVALID_HANDLE_VALUE)
    {
        g_app->hCaptureLogFile = hFile;
        g_app->captureLogPath = filePath;
    }
}

void FlushCaptureLogBuffer()
{
    if (!g_app || g_app->hCaptureLogFile == INVALID_HANDLE_VALUE)
        return;

    std::lock_guard<std::mutex> lock(g_captureLogMutex);

    if (g_capturePendingUtf8.empty())
        return;

    DWORD written = 0;
    WriteFile(
        g_app->hCaptureLogFile,
        g_capturePendingUtf8.data(),
        (DWORD)g_capturePendingUtf8.size(),
        &written,
        nullptr);

    g_capturePendingUtf8.clear();
    g_captureLastFlushTick = GetTickCount();
}

void StopCaptureLog()
{
    if (!g_app) return;

    if (g_app->hCaptureLogFile != INVALID_HANDLE_VALUE)
    {
        FlushCaptureLogBuffer();

        CloseHandle(g_app->hCaptureLogFile);
        g_app->hCaptureLogFile = INVALID_HANDLE_VALUE;
    }

    g_app->captureLogPath.clear();
}


void WriteRawAnsiBytesToCaptureLog(const char* data, size_t len)
{
    if (!g_app || g_app->hCaptureLogFile == INVALID_HANDLE_VALUE || !data || len == 0)
        return;

    std::lock_guard<std::mutex> lock(g_captureLogMutex);
    g_capturePendingUtf8.append(data, data + len);

    const size_t kFlushSize = 32 * 1024;
    const DWORD kFlushMs = 500;
    DWORD now = GetTickCount();
    bool needFlush = false;

    if (g_capturePendingUtf8.size() >= kFlushSize)
        needFlush = true;
    if (g_captureLastFlushTick == 0)
        g_captureLastFlushTick = now;
    if (now - g_captureLastFlushTick >= kFlushMs)
        needFlush = true;

    if (!needFlush)
        return;

    DWORD written = 0;
    WriteFile(g_app->hCaptureLogFile, g_capturePendingUtf8.data(),
        (DWORD)g_capturePendingUtf8.size(), &written, nullptr);
    g_capturePendingUtf8.clear();
    g_captureLastFlushTick = now;
}

void WriteRunsToCaptureLog(const std::vector<StyledRun>& runs)
{
    if (!g_app || g_app->hCaptureLogFile == INVALID_HANDLE_VALUE)
        return;

    std::wstring merged;
    for (const auto& run : runs)
        merged += run.text;

    if (merged.empty())
        return;

    NormalizeRunTextForRichEdit(merged);

    std::string utf8 = WideToUtf8(merged);
    if (utf8.empty())
        return;

    std::lock_guard<std::mutex> lock(g_captureLogMutex);
    g_capturePendingUtf8 += utf8;

    const size_t kFlushSize = 32 * 1024;
    const DWORD kFlushMs = 500;

    DWORD now = GetTickCount();
    bool needFlush = false;

    if (g_capturePendingUtf8.size() >= kFlushSize)
        needFlush = true;

    if (g_captureLastFlushTick == 0)
        g_captureLastFlushTick = now;

    if (now - g_captureLastFlushTick >= kFlushMs)
        needFlush = true;

    if (!needFlush)
        return;

    DWORD written = 0;
    WriteFile(
        g_app->hCaptureLogFile,
        g_capturePendingUtf8.data(),
        (DWORD)g_capturePendingUtf8.size(),
        &written,
        nullptr);

    g_capturePendingUtf8.clear();
    g_captureLastFlushTick = now;
}

void WriteToChatLog(const std::wstring& text)
{
    if (!g_app) return;

    std::wstring logDir = MakeAbsolutePath(GetModuleDirectory(), L"chat");
    CreateDirectoryW(logDir.c_str(), nullptr);

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t fileName[MAX_PATH];
    wsprintfW(fileName, L"%s\\%04d%02d%02d.txt",
        logDir.c_str(), st.wYear, st.wMonth, st.wDay);

    HANDLE hFile = CreateFileW(fileName, FILE_APPEND_DATA, FILE_SHARE_READ,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hFile != INVALID_HANDLE_VALUE)
    {
        if (GetFileSize(hFile, nullptr) == 0)
        {
            unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
            DWORD written;
            WriteFile(hFile, bom, 3, &written, nullptr);
        }

        std::string utf8 = WideToUtf8(text + L"\r\n");
        DWORD written;
        WriteFile(hFile, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
        CloseHandle(hFile);
    }
}

// ==============================================
// 떠 있는 채팅 창 프로시저
// ==============================================
LRESULT CALLBACK ChatFloatWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
        if (g_app && g_app->hwndChat && GetParent(g_app->hwndChat) == hwnd)
        {
            MoveWindow(g_app->hwndChat, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        }
        return 0;

    case WM_CLOSE:
        if (g_app && g_app->hwndMain)
        {
            if (g_app->chatVisible)
                SendMessageW(g_app->hwndMain, WM_COMMAND, ID_MENU_OPTIONS_CHAT_DOCK, 0);
            else
                SendMessageW(g_app->hwndMain, WM_COMMAND, ID_MENU_OPTIONS_CHAT_TOGGLE_VISIBLE, 0);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}