#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "chat_capture.h"
#include "resource.h"
#include "settings.h"
#include "async_file_writer.h"
#include "win_util.h"
#include <regex>
#include <shellapi.h>
#include <mutex>

static AsyncFileWriter g_captureLogWriter;
static AsyncFileWriter g_chatLogWriter;
static std::mutex g_logWriterMutex;
static std::wstring g_chatLogPath;

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

    if (g_captureLogWriter.Open(filePath, false, false))
    {
        g_app->captureLogOpen = true;
        g_app->captureLogPath = filePath;
    }
}

void FlushCaptureLogBuffer()
{
    if (!g_app || !g_app->captureLogOpen)
        return;
    g_captureLogWriter.Flush();
}

void StopCaptureLog()
{
    if (!g_app) return;

    if (g_app->captureLogOpen)
        g_captureLogWriter.Close();

    g_app->captureLogOpen = false;
    g_app->captureLogPath.clear();
}

void WriteRawAnsiBytesToCaptureLog(const char* data, size_t len)
{
    if (!g_app || !g_app->captureLogOpen || !data || len == 0)
        return;
    g_captureLogWriter.Write(data, len);
}

void WriteRunsToCaptureLog(const std::vector<StyledRun>& runs)
{
    if (!g_app || !g_app->captureLogOpen)
        return;

    std::wstring merged;
    for (const auto& run : runs)
        merged += run.text;

    if (merged.empty())
        return;

    NormalizeRunTextForRichEdit(merged);

    std::string utf8 = WideToUtf8(merged);
    if (!utf8.empty())
        g_captureLogWriter.Write(utf8);
}

static std::wstring BuildDailyChatLogPath()
{
    std::wstring logDir = MakeAbsolutePath(GetModuleDirectory(), L"chat");
    CreateDirectoryW(logDir.c_str(), nullptr);

    SYSTEMTIME st{};
    GetLocalTime(&st);

    wchar_t dayName[32] = {};
    wsprintfW(dayName, L"%04d%02d%02d.txt", st.wYear, st.wMonth, st.wDay);

    if (!logDir.empty() && logDir.back() != L'\\')
        logDir += L'\\';
    return logDir + dayName;
}

static bool EnsureChatLogWriterLocked(const std::wstring& path)
{
    if (path.empty())
        return false;

    if (g_chatLogWriter.IsOpen() && g_chatLogPath == path)
        return true;

    g_chatLogWriter.Close();
    g_chatLogPath.clear();

    if (!g_chatLogWriter.Open(path, true, true))
        return false;

    g_chatLogPath = path;
    return true;
}

void CloseChatLog()
{
    std::lock_guard<std::mutex> lock(g_logWriterMutex);
    g_chatLogWriter.Close();
    g_chatLogPath.clear();
}

void WriteToChatLog(const std::wstring& text)
{
    if (!g_app || text.empty())
        return;

    std::string utf8 = WideToUtf8(text + L"\r\n");
    if (utf8.empty())
        return;

    std::lock_guard<std::mutex> lock(g_logWriterMutex);
    const std::wstring path = BuildDailyChatLogPath();
    if (!EnsureChatLogWriterLocked(path))
        return;

    g_chatLogWriter.Write(utf8);
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

// ==============================================
// 채팅 입력창 서브클래스 프로시저
// ==============================================
LRESULT CALLBACK ChatEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_RBUTTONUP:
    {
        // [핵심 로직] RichEdit은 원본 WM_RBUTTONUP 내부에서 기본 메뉴를 띄워버리는 경우가 많습니다.
        // 원본으로 메시지가 넘어가지 않도록 차단(return 0)하고, 우리가 직접 WM_CONTEXTMENU를 호출합니다.
        POINT pt;
        GetCursorPos(&pt);
        SendMessageW(hwnd, WM_CONTEXTMENU, (WPARAM)hwnd, MAKELPARAM(pt.x, pt.y));
        return 0;
    }

    case WM_CONTEXTMENU:
    {
        UniqueMenu hMenu(CreatePopupMenu());
        if (hMenu.IsValid())
        {
            AppendMenuW(hMenu.Get(), MF_STRING, ID_CHAT_CUT, L"잘라내기(&T)\tCtrl+X");
            AppendMenuW(hMenu.Get(), MF_STRING, ID_CHAT_COPY, L"복사하기(&C)\tCtrl+C");
            AppendMenuW(hMenu.Get(), MF_STRING, ID_CHAT_PASTE, L"붙여넣기(&P)\tCtrl+V");
            AppendMenuW(hMenu.Get(), MF_STRING, ID_CHAT_DELETE, L"삭제(&D)\tDel");
            AppendMenuW(hMenu.Get(), MF_STRING, ID_CHAT_DELETE_LINE, L"행 삭제");
            AppendMenuW(hMenu.Get(), MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu.Get(), MF_STRING, ID_CHAT_SELECT_ALL, L"모두 선택(&A)\tCtrl+A");
            AppendMenuW(hMenu.Get(), MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu.Get(), MF_STRING, ID_CHAT_CLEAR_ALL, L"내용 지우기 (히스토리 포함)");

            int xPos = (short)LOWORD(lParam);
            int yPos = (short)HIWORD(lParam);

            if (xPos == -1 && yPos == -1) // 키보드 메뉴 키 대응
            {
                POINT pt;
                GetCursorPos(&pt);
                xPos = pt.x;
                yPos = pt.y;
            }

            SetForegroundWindow(g_app && g_app->hwndMain ? g_app->hwndMain : hwnd);
            int cmd = TrackPopupMenu(hMenu.Get(), TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN, xPos, yPos, 0, hwnd, nullptr);

            switch (cmd)
            {
            case ID_CHAT_CUT:         SendMessageW(hwnd, WM_CUT, 0, 0); break;
            case ID_CHAT_COPY:        SendMessageW(hwnd, WM_COPY, 0, 0); break;
            case ID_CHAT_PASTE:       SendMessageW(hwnd, WM_PASTE, 0, 0); break;
            case ID_CHAT_DELETE:      SendMessageW(hwnd, WM_CLEAR, 0, 0); break;
            case ID_CHAT_DELETE_LINE: SendMessageW(hwnd, EM_REPLACESEL, FALSE, (LPARAM)L""); break;
            case ID_CHAT_SELECT_ALL:  SendMessageW(hwnd, EM_SETSEL, 0, -1); break;
            case ID_CHAT_CLEAR_ALL:
                if (g_app)
                {
                    SetWindowTextW(hwnd, L"");
                    g_app->history.clear();
                    g_app->displayedHistoryIndex[0] = -1;
                    g_app->displayedHistoryIndex[1] = -1;
                    g_app->displayedHistoryIndex[2] = -1;
                    SetInputViewLatest();
                }
                break;
            }
        }
        return 0; // 처리 완료 후 0 반환
    }
    }

    // WM_RBUTTONDOWN 등 나머지 모든 메시지는 원본 프로시저가 정상 처리 (우클릭 커서 이동 지원)
    return CallWindowProcW(g_app->oldChatProc, hwnd, msg, wParam, lParam);
}
