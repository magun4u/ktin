#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "address_book.h"
#include "resource.h"
#include "settings.h"
#include "auto_login.h"
#include <commctrl.h>


static int HexVal(wchar_t ch)
{
    if (ch >= L'0' && ch <= L'9') return ch - L'0';
    if (ch >= L'a' && ch <= L'f') return ch - L'a' + 10;
    if (ch >= L'A' && ch <= L'F') return ch - L'A' + 10;
    return -1;
}

static std::wstring EncodePasswordForIni(const std::wstring& plain)
{
    if (plain.empty()) return L"";
    static const wchar_t* hex = L"0123456789ABCDEF";
    const wchar_t key = 0x5A;
    std::wstring out = L"HEX:";
    for (wchar_t ch : plain)
    {
        unsigned int v = ((unsigned int)ch) ^ key;
        out.push_back(hex[(v >> 12) & 0xF]);
        out.push_back(hex[(v >> 8) & 0xF]);
        out.push_back(hex[(v >> 4) & 0xF]);
        out.push_back(hex[v & 0xF]);
    }
    return out;
}

static std::wstring DecodePasswordFromIni(const std::wstring& stored)
{
    if (stored.rfind(L"HEX:", 0) != 0)
        return stored; // 이전 빌드에서 평문으로 저장된 값 보존

    std::wstring out;
    const wchar_t key = 0x5A;
    for (size_t i = 4; i + 3 < stored.size(); i += 4)
    {
        int a = HexVal(stored[i]);
        int b = HexVal(stored[i + 1]);
        int c = HexVal(stored[i + 2]);
        int d = HexVal(stored[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0)
            return L"";
        wchar_t ch = (wchar_t)((((a << 12) | (b << 8) | (c << 4) | d) ^ key) & 0xFFFF);
        out.push_back(ch);
    }
    return out;
}

// 내부 전방 선언
LRESULT CALLBACK AddressBookEntryEditorProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK AddressBookPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void SortAddressBook() {
    if (!g_app) return;
    std::stable_sort(g_app->addressBook.begin(), g_app->addressBook.end(), [](const AddressBookEntry& a, const AddressBookEntry& b) {
        if (g_app->addressBookSortMode == 1) return a.name < b.name;     // 이름순
        if (g_app->addressBookSortMode == 2) return a.host < b.host;     // 서버주소순
        if (g_app->addressBookSortMode > 2) g_app->addressBookSortMode = 0;
        return a.lastConnected > b.lastConnected; // 기본(0): 최근 접속순 (내림차순)
        });
}

void LoadAddressBook()
{
    if (!g_app) return;

    g_app->addressBook.clear();

    std::wstring path = GetSessionsPath();
    int count = GetPrivateProfileIntW(L"sessions", L"count", 0, path.c_str());
    count = ClampInt(count, 0, 1000);

    g_app->addressBookSortMode = GetPrivateProfileIntW(L"sessions", L"sort_mode", 0, path.c_str());

    wchar_t buf[1024] = {};

    for (int i = 0; i < count; ++i)
    {
        AddressBookEntry entry;

        wchar_t keyN[32], keyH[32], keyP[32], keyS[32], keyC[32];
        wchar_t keyAL[32], keyIdP[32], keyId[32], keyPwP[32], keyPw[32];
        wchar_t keyLC[32], keyAR[32];
        wchar_t keyLS1[32], keyLS2[32], keyLS3[32];
        wchar_t keyLF1[32], keyLF2[32], keyLF3[32];

        wsprintfW(keyN, L"name_%d", i);
        wsprintfW(keyH, L"host_%d", i);
        wsprintfW(keyP, L"port_%d", i);
        wsprintfW(keyS, L"script_%d", i);
        wsprintfW(keyC, L"charset_%d", i);
        wsprintfW(keyAL, L"al_en_%d", i);
        wsprintfW(keyIdP, L"al_idpat_%d", i);
        wsprintfW(keyId, L"al_id_%d", i);
        wsprintfW(keyPwP, L"al_pwpat_%d", i);
        wsprintfW(keyPw, L"al_pw_%d", i);
        wsprintfW(keyLC, L"lastconn_%d", i);
        wsprintfW(keyAR, L"al_recon_%d", i);

        wsprintfW(keyLS1, L"login_success1_%d", i);
        wsprintfW(keyLS2, L"login_success2_%d", i);
        wsprintfW(keyLS3, L"login_success3_%d", i);
        wsprintfW(keyLF1, L"login_fail1_%d", i);
        wsprintfW(keyLF2, L"login_fail2_%d", i);
        wsprintfW(keyLF3, L"login_fail3_%d", i);

        GetPrivateProfileStringW(L"sessions", keyN, L"", buf, 1024, path.c_str());
        entry.name = buf;

        GetPrivateProfileStringW(L"sessions", keyH, L"", buf, 1024, path.c_str());
        entry.host = buf;

        entry.port = GetPrivateProfileIntW(L"sessions", keyP, 23, path.c_str());

        GetPrivateProfileStringW(L"sessions", keyS, L"", buf, 1024, path.c_str());
        entry.scriptPath = buf;

        entry.charset = GetPrivateProfileIntW(L"sessions", keyC, 0, path.c_str());
        if (entry.charset != 0 && entry.charset != 1)
            entry.charset = 0;

        entry.autoLoginEnabled = GetPrivateProfileIntW(L"sessions", keyAL, 0, path.c_str()) != 0;

        GetPrivateProfileStringW(L"sessions", keyIdP, L"아이디:", buf, 1024, path.c_str());
        entry.alIdPattern = buf;

        GetPrivateProfileStringW(L"sessions", keyId, L"", buf, 1024, path.c_str());
        entry.alId = buf;

        GetPrivateProfileStringW(L"sessions", keyPwP, L"비밀번호:", buf, 1024, path.c_str());
        entry.alPwPattern = buf;

        GetPrivateProfileStringW(L"sessions", keyPw, L"", buf, 1024, path.c_str());
        entry.alPw = DecodePasswordFromIni(buf);

        GetPrivateProfileStringW(L"sessions", keyLS1, L"", buf, 1024, path.c_str());
        entry.loginSuccessPattern1 = buf;

        GetPrivateProfileStringW(L"sessions", keyLS2, L"", buf, 1024, path.c_str());
        entry.loginSuccessPattern2 = buf;

        GetPrivateProfileStringW(L"sessions", keyLS3, L"", buf, 1024, path.c_str());
        entry.loginSuccessPattern3 = buf;

        GetPrivateProfileStringW(L"sessions", keyLF1, L"", buf, 1024, path.c_str());
        entry.loginFailPattern1 = buf;

        GetPrivateProfileStringW(L"sessions", keyLF2, L"", buf, 1024, path.c_str());
        entry.loginFailPattern2 = buf;

        GetPrivateProfileStringW(L"sessions", keyLF3, L"", buf, 1024, path.c_str());
        entry.loginFailPattern3 = buf;

        GetPrivateProfileStringW(L"sessions", keyLC, L"0", buf, 1024, path.c_str());
        entry.lastConnected = wcstoull(buf, nullptr, 10);

        entry.autoReconnect = GetPrivateProfileIntW(L"sessions", keyAR, 0, path.c_str()) != 0;

        if (!entry.name.empty() && !entry.host.empty())
        {
            g_app->addressBook.push_back(entry);
        }
    }

    SortAddressBook();
}

void SaveAddressBook()
{
    if (!g_app) return;

    std::wstring path = GetSessionsPath();
    int count = ClampInt((int)g_app->addressBook.size(), 0, 1000);

    wchar_t buf[64] = {};
    wsprintfW(buf, L"%d", count);
    WritePrivateProfileStringW(L"sessions", L"count", buf, path.c_str());

    // 정렬 옵션 저장
    wsprintfW(buf, L"%d", g_app->addressBookSortMode);
    WritePrivateProfileStringW(L"sessions", L"sort_mode", buf, path.c_str());

    for (int i = 0; i < count; ++i)
    {
        wchar_t keyN[32], keyH[32], keyP[32], keyS[32], keyC[32];
        wchar_t keyAL[32], keyIdP[32], keyId[32], keyPwP[32], keyPw[32];
        wchar_t keyLC[32], keyAR[32];
        wchar_t keyLS1[32], keyLS2[32], keyLS3[32];
        wchar_t keyLF1[32], keyLF2[32], keyLF3[32];

        wsprintfW(keyN, L"name_%d", i);
        wsprintfW(keyH, L"host_%d", i);
        wsprintfW(keyP, L"port_%d", i);
        wsprintfW(keyS, L"script_%d", i);
        wsprintfW(keyC, L"charset_%d", i);
        wsprintfW(keyAL, L"al_en_%d", i);
        wsprintfW(keyIdP, L"al_idpat_%d", i);
        wsprintfW(keyId, L"al_id_%d", i);
        wsprintfW(keyPwP, L"al_pwpat_%d", i);
        wsprintfW(keyPw, L"al_pw_%d", i);
        wsprintfW(keyLC, L"lastconn_%d", i);
        wsprintfW(keyAR, L"al_recon_%d", i);

        wsprintfW(keyLS1, L"login_success1_%d", i);
        wsprintfW(keyLS2, L"login_success2_%d", i);
        wsprintfW(keyLS3, L"login_success3_%d", i);
        wsprintfW(keyLF1, L"login_fail1_%d", i);
        wsprintfW(keyLF2, L"login_fail2_%d", i);
        wsprintfW(keyLF3, L"login_fail3_%d", i);

        WritePrivateProfileStringW(L"sessions", keyN, g_app->addressBook[i].name.c_str(), path.c_str());
        WritePrivateProfileStringW(L"sessions", keyH, g_app->addressBook[i].host.c_str(), path.c_str());

        wsprintfW(buf, L"%d", g_app->addressBook[i].port);
        WritePrivateProfileStringW(L"sessions", keyP, buf, path.c_str());

        WritePrivateProfileStringW(L"sessions", keyS, g_app->addressBook[i].scriptPath.c_str(), path.c_str());

        wsprintfW(buf, L"%d", g_app->addressBook[i].charset);
        WritePrivateProfileStringW(L"sessions", keyC, buf, path.c_str());

        WritePrivateProfileStringW(L"sessions", keyAL, g_app->addressBook[i].autoLoginEnabled ? L"1" : L"0", path.c_str());

        WritePrivateProfileStringW(L"sessions", keyIdP, g_app->addressBook[i].alIdPattern.c_str(), path.c_str());
        WritePrivateProfileStringW(L"sessions", keyId, g_app->addressBook[i].alId.c_str(), path.c_str());
        WritePrivateProfileStringW(L"sessions", keyPwP, g_app->addressBook[i].alPwPattern.c_str(), path.c_str());
        WritePrivateProfileStringW(L"sessions", keyPw, EncodePasswordForIni(g_app->addressBook[i].alPw).c_str(), path.c_str());

        WritePrivateProfileStringW(L"sessions", keyLS1, g_app->addressBook[i].loginSuccessPattern1.c_str(), path.c_str());
        WritePrivateProfileStringW(L"sessions", keyLS2, g_app->addressBook[i].loginSuccessPattern2.c_str(), path.c_str());
        WritePrivateProfileStringW(L"sessions", keyLS3, g_app->addressBook[i].loginSuccessPattern3.c_str(), path.c_str());

        WritePrivateProfileStringW(L"sessions", keyLF1, g_app->addressBook[i].loginFailPattern1.c_str(), path.c_str());
        WritePrivateProfileStringW(L"sessions", keyLF2, g_app->addressBook[i].loginFailPattern2.c_str(), path.c_str());
        WritePrivateProfileStringW(L"sessions", keyLF3, g_app->addressBook[i].loginFailPattern3.c_str(), path.c_str());

        wsprintfW(buf, L"%I64u", g_app->addressBook[i].lastConnected);
        WritePrivateProfileStringW(L"sessions", keyLC, buf, path.c_str());

        WritePrivateProfileStringW(L"sessions", keyAR, g_app->addressBook[i].autoReconnect ? L"1" : L"0", path.c_str());
    }

    // 남은 빈칸 청소
    for (int i = count; i < 1000; ++i)
    {
        wchar_t key[64];

        wsprintfW(key, L"name_%d", i);
        WritePrivateProfileStringW(L"sessions", key, nullptr, path.c_str());

        wsprintfW(key, L"host_%d", i);
        WritePrivateProfileStringW(L"sessions", key, nullptr, path.c_str());

        wsprintfW(key, L"port_%d", i);
        WritePrivateProfileStringW(L"sessions", key, nullptr, path.c_str());

        wsprintfW(key, L"script_%d", i);
        WritePrivateProfileStringW(L"sessions", key, nullptr, path.c_str());

        wsprintfW(key, L"charset_%d", i);
        WritePrivateProfileStringW(L"sessions", key, nullptr, path.c_str());

        wsprintfW(key, L"al_en_%d", i);
        WritePrivateProfileStringW(L"sessions", key, nullptr, path.c_str());

        wsprintfW(key, L"al_idpat_%d", i);
        WritePrivateProfileStringW(L"sessions", key, nullptr, path.c_str());

        wsprintfW(key, L"al_id_%d", i);
        WritePrivateProfileStringW(L"sessions", key, nullptr, path.c_str());

        wsprintfW(key, L"al_pwpat_%d", i);
        WritePrivateProfileStringW(L"sessions", key, nullptr, path.c_str());

        wsprintfW(key, L"al_pw_%d", i);
        WritePrivateProfileStringW(L"sessions", key, nullptr, path.c_str());

        wsprintfW(key, L"login_success1_%d", i);
        WritePrivateProfileStringW(L"sessions", key, nullptr, path.c_str());

        wsprintfW(key, L"login_success2_%d", i);
        WritePrivateProfileStringW(L"sessions", key, nullptr, path.c_str());

        wsprintfW(key, L"login_success3_%d", i);
        WritePrivateProfileStringW(L"sessions", key, nullptr, path.c_str());

        wsprintfW(key, L"login_fail1_%d", i);
        WritePrivateProfileStringW(L"sessions", key, nullptr, path.c_str());

        wsprintfW(key, L"login_fail2_%d", i);
        WritePrivateProfileStringW(L"sessions", key, nullptr, path.c_str());

        wsprintfW(key, L"login_fail3_%d", i);
        WritePrivateProfileStringW(L"sessions", key, nullptr, path.c_str());

        wsprintfW(key, L"lastconn_%d", i);
        WritePrivateProfileStringW(L"sessions", key, nullptr, path.c_str());

        wsprintfW(key, L"al_recon_%d", i);
        WritePrivateProfileStringW(L"sessions", key, nullptr, path.c_str());
    }
}


void RefreshAddressBookList(HWND hList)
{
    if (!g_app || !hList) return;
    ListView_DeleteAllItems(hList);

    for (size_t i = 0; i < g_app->addressBook.size(); ++i) {
        const auto& e = g_app->addressBook[i];
        LVITEMW lvi = {};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = (int)i;
        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)e.name.c_str();
        ListView_InsertItem(hList, &lvi);

        wchar_t hostPort[256];
        wsprintfW(hostPort, L"%s:%d", e.host.c_str(), e.port);
        ListView_SetItemText(hList, i, 1, hostPort);

    }
}

void BeginSwitchToAddressBookEntry(const AddressBookEntry& entry)
{
    if (!g_app || !g_app->hwndMain)
        return;

    std::wstring sessionName = Trim(entry.name);
    std::wstring host = Trim(entry.host);
    if (sessionName.empty() || host.empty())
        return;

    g_app->pendingConnectEntry = entry;
    g_app->hasPendingConnect = true;

    KillTimer(g_app->hwndMain, ID_TIMER_AUTORECONNECT);

    // 기존 세션 강제 종료
    // buildfix38: 주소록 세션뿐 아니라 빠른연결의 고정 세션명 new도 함께 종료합니다.
    ZapKnownTinTinSession();

    g_app->isConnected = false; // 실제 세션 활성화 메시지를 받을 때 true로 전환

    if (g_app->hwndStatusBar)
        InvalidateRect(g_app->hwndStatusBar, nullptr, TRUE);

    // ★★★ 강제 charset 설정 (전환 시에도 반드시 먼저 보냄) ★★★
    if (entry.charset == 0) {
        SendRawCommandToMud(L"#CONFIG {CHARSET} {UTF-8}");
    }
    else {
        SendRawCommandToMud(L"#CONFIG {CHARSET} {CP949TOUTF8}");
    }

    // 자동 감지 완전 차단
    g_app->g_charsetDetected = true;
    g_app->g_detectCharsetRetry = 999;

    // ★★★ 타이머 시간을 500ms로 늘려서 #zap이 완전히 끝난 후 #session이 실행되도록 함 ★★★
    SetTimer(g_app->hwndMain, ID_TIMER_SWITCH_CONNECT, 500, nullptr);
}

void ConnectAddressBookEntry(const AddressBookEntry& entry)
{
    if (!g_app) return;

    std::wstring sessionName = Trim(entry.name);
    std::wstring host = Trim(entry.host);
    if (sessionName.empty() || host.empty()) return;

    int port = (entry.port <= 0 || entry.port > 65535) ? 23 : entry.port;
    wchar_t portBuf[32];
    wsprintfW(portBuf, L"%d", port);

    g_app->activeSession = entry;
    g_app->hasActiveSession = true;

    KillTimer(g_app->hwndMain, ID_TIMER_AUTORECONNECT);

    // 접속 직후 60초 동안만 로그인 패턴을 검사합니다.
    StartAutoLoginWindowForAddressEntry(entry);

    // ★★★ 강제 charset 재설정 (전환 시에도 안전장치) ★★★
    if (entry.charset == 0) {
        SendRawCommandToMud(L"#CONFIG {CHARSET} {UTF-8}");
    }
    else {
        SendRawCommandToMud(L"#CONFIG {CHARSET} {CP949TOUTF8}");
    }

    // #session 실행
    std::wstring cmd = L"#session {" + sessionName + L"} {" + host + L"} {" + portBuf + L"}";
    SendRawCommandToMud(cmd);
    MarkKnownTinTinSession(sessionName);

    // StartAutoLoginWindowForAddressEntry()에서 이미 초기화했습니다.

    if (!Trim(entry.scriptPath).empty()) {
        SendRawCommandToMud(L"#read {" + Trim(entry.scriptPath) + L"}");
    }

    g_app->g_charsetDetected = true;
    g_app->g_detectCharsetRetry = 999;

}

bool PromptAddressBookEntryEditor(HWND hwnd, AddressBookEntry& entry, bool isEdit)
{
    const wchar_t kDlgClass[] = L"TTGuiAddressBookEntryEditorClass";
    static bool s_registered = false;

    if (!s_registered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = AddressBookEntryEditorProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kDlgClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        s_registered = true;
    }

    AddressBookEntryEditorState state;
    state.entry = &entry;
    state.accepted = false;
    state.isEdit = isEdit;

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kDlgClass,
        isEdit ? L"주소 수정" : L"새 주소 추가",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT,
        500, 475,
        hwnd,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (!hDlg)
        return false;

    SetPropW(hDlg, L"AddressBookEntryEditorState", &state);

    CreateWindowExW(0, L"STATIC", L"　　서버 이름:", WS_CHILD | WS_VISIBLE,
        20, 24, 168, 20, hDlg, nullptr, nullptr, nullptr);
    HWND hEditName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        110, 20, 320, 24, hDlg, (HMENU)(UINT_PTR)ID_ADDRESSBOOK_NAME, nullptr, nullptr);

    CreateWindowExW(0, L"STATIC", L"　　서버 주소:", WS_CHILD | WS_VISIBLE,
        20, 60, 168, 20, hDlg, nullptr, nullptr, nullptr);
    HWND hEditHost = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        110, 56, 320, 24, hDlg, (HMENU)(UINT_PTR)ID_ADDRESSBOOK_HOST, nullptr, nullptr);

    CreateWindowExW(0, L"STATIC", L"　　서버 포트:", WS_CHILD | WS_VISIBLE,
        20, 96, 168, 20, hDlg, nullptr, nullptr, nullptr);
    HWND hEditPort = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
        110, 92, 100, 24, hDlg, (HMENU)(UINT_PTR)ID_ADDRESSBOOK_PORT, nullptr, nullptr);

    CreateWindowExW(0, L"STATIC", L"　서버 문자셋:", WS_CHILD | WS_VISIBLE,
        20, 132, 168, 20, hDlg, nullptr, nullptr, nullptr);
    HWND hComboCharset = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
        110, 128, 140, 100, hDlg, (HMENU)(UINT_PTR)ID_ADDRESSBOOK_CHARSET, nullptr, nullptr);
    SendMessageW(hComboCharset, CB_ADDSTRING, 0, (LPARAM)L"UTF-8");
    SendMessageW(hComboCharset, CB_ADDSTRING, 0, (LPARAM)L"EUC-KR (CP949)");
    SendMessageW(hComboCharset, CB_SETCURSEL, entry.charset, 0);

    CreateWindowExW(0, L"STATIC", L"TinTin 스크립트:", WS_CHILD | WS_VISIBLE,
        20, 168, 110, 20, hDlg, nullptr, nullptr, nullptr);
    HWND hEditScript = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        110, 164, 230, 24, hDlg, (HMENU)(UINT_PTR)ID_ADDRESSBOOK_SCRIPT, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"찾아보기...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        350, 164, 80, 24, hDlg, (HMENU)(UINT_PTR)ID_ADDRESSBOOK_BROWSE, nullptr, nullptr);

    int alY = 205;
    HWND hChkAL = CreateWindowExW(0, L"BUTTON", L"이 주소에 개별 자동 로그인 사용", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        20, alY, 250, 24, hDlg, (HMENU)(UINT_PTR)ID_ADDRESSBOOK_CHK_AL, nullptr, nullptr);
    SendMessageW(hChkAL, BM_SETCHECK, entry.autoLoginEnabled ? BST_CHECKED : BST_UNCHECKED, 0);

    CreateWindowExW(0, L"STATIC", L"아이디 패턴:", WS_CHILD | WS_VISIBLE, 20, alY + 36, 85, 20, hDlg, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", entry.alIdPattern.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        110, alY + 32, 155, 24, hDlg, (HMENU)(UINT_PTR)ID_ADDRESSBOOK_AL_ID_PAT, nullptr, nullptr);
    CreateWindowExW(0, L"STATIC", L"아이디:", WS_CHILD | WS_VISIBLE, 275, alY + 36, 55, 20, hDlg, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", entry.alId.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        330, alY + 32, 120, 24, hDlg, (HMENU)(UINT_PTR)ID_ADDRESSBOOK_AL_ID, nullptr, nullptr);

    CreateWindowExW(0, L"STATIC", L"비번 패턴:", WS_CHILD | WS_VISIBLE, 20, alY + 70, 85, 20, hDlg, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", entry.alPwPattern.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        110, alY + 66, 155, 24, hDlg, (HMENU)(UINT_PTR)ID_ADDRESSBOOK_AL_PW_PAT, nullptr, nullptr);
    CreateWindowExW(0, L"STATIC", L"비번:", WS_CHILD | WS_VISIBLE, 275, alY + 70, 55, 20, hDlg, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", entry.alPw.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
        330, alY + 66, 120, 24, hDlg, (HMENU)(UINT_PTR)ID_ADDRESSBOOK_AL_PW, nullptr, nullptr);

    HWND hChkReconnect = CreateWindowExW(0, L"BUTTON", L"연결 실패/끊김 시 자동 재연결",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        20, alY + 108, 260, 24, hDlg, (HMENU)(UINT_PTR)ID_ADDRESSBOOK_CHK_AUTORECONN, nullptr, nullptr);
    SendMessageW(hChkReconnect, BM_SETCHECK, entry.autoReconnect ? BST_CHECKED : BST_UNCHECKED, 0);

    CreateWindowExW(0, L"STATIC", L"※ 접속 후 60초만 검사하고 아이디/비번 전송 후 즉시 OFF", WS_CHILD | WS_VISIBLE,
        20, alY + 138, 430, 20, hDlg, nullptr, nullptr, nullptr);

    CreateWindowExW(0, L"BUTTON", L"확인(&O)",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        290, 390, 80, 30, hDlg, (HMENU)(UINT_PTR)IDOK, nullptr, nullptr);

    CreateWindowExW(0, L"BUTTON", L"취소(&C)",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        380, 390, 80, 30, hDlg, (HMENU)(UINT_PTR)IDCANCEL, nullptr, nullptr);

    SetWindowTextW(hEditName, entry.name.c_str());
    SetWindowTextW(hEditHost, entry.host.c_str());

    wchar_t portBuf[32];
    wsprintfW(portBuf, L"%d", entry.port > 0 ? entry.port : 23);
    SetWindowTextW(hEditPort, portBuf);
    SetWindowTextW(hEditScript, entry.scriptPath.c_str());

    HFONT hFont = GetPopupUIFont(hDlg);
    EnumChildWindows(hDlg,
        [](HWND c, LPARAM f) -> BOOL
        {
            SendMessageW(c, WM_SETFONT, f, TRUE);
            return TRUE;
        },
        (LPARAM)hFont);

    RECT rcO{}, rcD{};
    GetWindowRect(hwnd, &rcO);
    GetWindowRect(hDlg, &rcD);

    SetWindowPos(hDlg, HWND_TOP,
        rcO.left + ((rcO.right - rcO.left) - (rcD.right - rcD.left)) / 2,
        rcO.top + ((rcO.bottom - rcO.top) - (rcD.bottom - rcD.top)) / 2,
        0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);

    EnableWindow(hwnd, FALSE);
    SetFocus(hEditName);

    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, nullptr, 0, 0))
    {
        if (!IsDialogMessageW(hDlg, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(hwnd, TRUE);
    SetActiveWindow(hwnd);
    SetForegroundWindow(hwnd);

    return state.accepted;
}

bool PromptAddressBook(HWND hwnd)
{
    const wchar_t kDlgClass[] = L"TTGuiAddressBookPopupClass";
    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSW wc = {}; wc.lpfnWndProc = AddressBookPopupProc; wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kDlgClass; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassW(&wc); s_registered = true;
    }

    // ★ 리스트뷰 칼럼이 들어가도록 가로 창 너비를 490 -> 540으로 넓혔습니다.
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kDlgClass, L"주소록", WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 540, 360, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hDlg) return false;
    LoadAddressBook();

    CreateWindowExW(0, L"STATIC", L"연결할 주소를 선택하세요", WS_CHILD | WS_VISIBLE, 16, 16, 220, 20, hDlg, nullptr, 0, 0);

    // ★ 기존 LISTBOX 대신 멋진 엑셀형 리스트 뷰(WC_LISTVIEWW)를 생성합니다.
    HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        16, 44, 380, 250, hDlg, (HMENU)(INT_PTR)ID_ADDRESSBOOK_LIST, 0, nullptr);

    // 격자선(그리드)과 전체 행 선택 효과를 추가합니다.
    ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    // 헤더(칼럼) 만들기
    LVCOLUMNW lvc = {};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    lvc.pszText = (LPWSTR)L"이름"; lvc.cx = 140; ListView_InsertColumn(hList, 0, &lvc);
    lvc.pszText = (LPWSTR)L"서버주소:포트"; lvc.cx = 220; ListView_InsertColumn(hList, 1, &lvc);

    RefreshAddressBookList(hList);

    // ★ 신규: 새로 만들기 버튼 상단에 정렬 콤보박스 배치
    CreateWindowExW(0, L"STATIC", L"정렬순서:", WS_CHILD | WS_VISIBLE, 350, 16, 60, 20, hDlg, nullptr, 0, 0);
    HWND hComboSort = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP, 410, 12, 100, 150, hDlg, (HMENU)(UINT_PTR)ID_ADDRESSBOOK_SORT, 0, nullptr);
    SendMessageW(hComboSort, CB_ADDSTRING, 0, (LPARAM)L"최근 접속순");
    SendMessageW(hComboSort, CB_ADDSTRING, 0, (LPARAM)L"이름순");
    SendMessageW(hComboSort, CB_ADDSTRING, 0, (LPARAM)L"서버주소순");
    if (g_app->addressBookSortMode > 2) g_app->addressBookSortMode = 0;
    SendMessageW(hComboSort, CB_SETCURSEL, g_app->addressBookSortMode, 0);

    int btnX = 410; // 버튼 X좌표 뒤로 밀기
    CreateWindowExW(0, L"BUTTON", L"새로 만들기(&N)", WS_CHILD | WS_VISIBLE, btnX, 44, 100, 32, hDlg, (HMENU)(UINT_PTR)ID_ADDRESSBOOK_NEW, 0, 0);
    CreateWindowExW(0, L"BUTTON", L"수정(&E)", WS_CHILD | WS_VISIBLE, btnX, 84, 100, 32, hDlg, (HMENU)(UINT_PTR)ID_ADDRESSBOOK_EDIT, 0, 0);
    CreateWindowExW(0, L"BUTTON", L"삭제(&D)", WS_CHILD | WS_VISIBLE, btnX, 124, 100, 32, hDlg, (HMENU)(UINT_PTR)ID_ADDRESSBOOK_DELETE, 0, 0);
    CreateWindowExW(0, L"BUTTON", L"연결(&C)", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, btnX, 272, 100, 32, hDlg, (HMENU)(UINT_PTR)ID_ADDRESSBOOK_CONNECT, 0, 0);

    HFONT hFont = GetPopupUIFont(hDlg);
    EnumChildWindows(hDlg, [](HWND c, LPARAM f) -> BOOL { SendMessageW(c, WM_SETFONT, f, TRUE); return TRUE; }, (LPARAM)hFont);

    // 리스트뷰도 팝업 기본 폰트와 동일하게 사용
    SendMessageW(hList, WM_SETFONT, (WPARAM)hFont, TRUE);
    HWND hHeader = ListView_GetHeader(hList);
    if (hHeader) SendMessageW(hHeader, WM_SETFONT, (WPARAM)hFont, TRUE);

    // 행 높이를 억지로 맞추기 위해 small image list를 붙이면
    // 보고서(ListView Report) 모드에서 첫 칼럼 왼쪽에 아이콘 여백이 생겨
    // 검은 세로선/찌꺼기처럼 보일 수 있습니다. 주소록은 아이콘을 쓰지 않으므로
    // 이미지 리스트를 붙이지 않습니다.

    RECT rcOwner{}, rcDlg{}; GetWindowRect(hwnd, &rcOwner); GetWindowRect(hDlg, &rcDlg);
    int dlgW = rcDlg.right - rcDlg.left; int dlgH = rcDlg.bottom - rcDlg.top;
    int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - dlgW) / 2; int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - dlgH) / 2;
    SetWindowPos(hDlg, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);

    EnableWindow(hwnd, FALSE); SetFocus(hList);
    MSG msg; while (IsWindow(hDlg) && GetMessageW(&msg, nullptr, 0, 0)) { if (!IsDialogMessageW(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    EnableWindow(hwnd, TRUE); SetActiveWindow(hwnd); SetForegroundWindow(hwnd);
    return true;
}

LRESULT CALLBACK AddressBookEntryEditorProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    AddressBookEntryEditorState* state =
        (AddressBookEntryEditorState*)GetPropW(hwnd, L"AddressBookEntryEditorState");

    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_ADDRESSBOOK_BROWSE: {
            if (state && state->entry) {
                std::wstring path;
                if (ChooseScriptFile(hwnd, path)) SetWindowTextW(GetDlgItem(hwnd, ID_ADDRESSBOOK_SCRIPT), path.c_str());
            }
            return 0;
        }
        case IDOK: {
            if (state && state->entry) {
                wchar_t nameBuf[256] = {}, hostBuf[256] = {}, portBuf[64] = {}, scriptBuf[1024] = {};
                GetWindowTextW(GetDlgItem(hwnd, ID_ADDRESSBOOK_NAME), nameBuf, 256);
                GetWindowTextW(GetDlgItem(hwnd, ID_ADDRESSBOOK_HOST), hostBuf, 256);
                GetWindowTextW(GetDlgItem(hwnd, ID_ADDRESSBOOK_PORT), portBuf, 64);
                GetWindowTextW(GetDlgItem(hwnd, ID_ADDRESSBOOK_SCRIPT), scriptBuf, 1024);

                std::wstring name = Trim(nameBuf), host = Trim(hostBuf), script = Trim(scriptBuf);
                int port = _wtoi(portBuf);

                if (name.empty()) {
                    MessageBoxW(hwnd, L"이름을 입력하세요.", L"주소록", MB_OK | MB_ICONWARNING);
                    SetFocus(GetDlgItem(hwnd, ID_ADDRESSBOOK_NAME));
                    return 0;
                }

                if (host.empty()) {
                    MessageBoxW(hwnd, L"주소를 입력하세요.", L"주소록", MB_OK | MB_ICONWARNING);
                    SetFocus(GetDlgItem(hwnd, ID_ADDRESSBOOK_HOST));
                    return 0;
                }

                state->entry->name = name;
                state->entry->host = host;
                state->entry->port = (port <= 0 || port > 65535) ? 23 : port;
                state->entry->scriptPath = script;
                state->entry->charset = (int)SendMessageW(GetDlgItem(hwnd, ID_ADDRESSBOOK_CHARSET), CB_GETCURSEL, 0, 0);

                state->entry->autoLoginEnabled = (SendMessageW(GetDlgItem(hwnd, ID_ADDRESSBOOK_CHK_AL), BM_GETCHECK, 0, 0) == BST_CHECKED);
                GetWindowTextW(GetDlgItem(hwnd, ID_ADDRESSBOOK_AL_ID_PAT), nameBuf, 256);
                state->entry->alIdPattern = Trim(nameBuf);
                GetWindowTextW(GetDlgItem(hwnd, ID_ADDRESSBOOK_AL_ID), nameBuf, 256);
                state->entry->alId = Trim(nameBuf);
                GetWindowTextW(GetDlgItem(hwnd, ID_ADDRESSBOOK_AL_PW_PAT), nameBuf, 256);
                state->entry->alPwPattern = Trim(nameBuf);
                GetWindowTextW(GetDlgItem(hwnd, ID_ADDRESSBOOK_AL_PW), nameBuf, 256);
                state->entry->alPw = Trim(nameBuf);
                state->entry->loginSuccessPattern1.clear();
                state->entry->loginSuccessPattern2.clear();
                state->entry->loginSuccessPattern3.clear();
                state->entry->loginFailPattern1.clear();
                state->entry->loginFailPattern2.clear();
                state->entry->loginFailPattern3.clear();
                state->entry->autoReconnect = (SendMessageW(GetDlgItem(hwnd, ID_ADDRESSBOOK_CHK_AUTORECONN), BM_GETCHECK, 0, 0) == BST_CHECKED);

                state->accepted = true;
            }

            DestroyWindow(hwnd);
            return 0;
        }
        case IDCANCEL: DestroyWindow(hwnd); return 0;
        }
        break;
    case WM_SYSCHAR:  // ← ALT + 단축키 처리 추가
    {
        wchar_t ch = towlower((wchar_t)wParam);
        if (ch == 'o') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0); return 0; }     // 확인
        if (ch == 'c') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0); return 0; } // 취소
        break;
    }
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
    }
    case WM_NCDESTROY:
        RemovePropW(hwnd, L"AddressBookEntryEditorState");
        return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: RemovePropW(hwnd, L"AddressBookEntryEditorState"); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ConfirmDeleteAddressBookEntry(HWND hwnd, const std::wstring& name)
{
    std::wstring msg = L"'" + name + L"' 주소를 삭제하시겠습니까?";
    return MessageBoxW(hwnd, msg.c_str(), L"주소록", MB_YESNO | MB_ICONQUESTION) == IDYES;
}

LRESULT CALLBACK AddressBookPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_NOTIFY:
    {
        LPNMHDR pnmh = (LPNMHDR)lParam;
        if (pnmh->idFrom == ID_ADDRESSBOOK_LIST) {
            if (pnmh->code == NM_DBLCLK) {
                if (!g_app) return 0;
                HWND hList = GetDlgItem(hwnd, ID_ADDRESSBOOK_LIST);
                int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
                if (sel >= 0 && sel < (int)g_app->addressBook.size()) {
                    AddressBookEntry entryToConnect = g_app->addressBook[sel];
                    FILETIME ft; GetSystemTimeAsFileTime(&ft);
                    g_app->addressBook[sel].lastConnected = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
                    SortAddressBook(); SaveAddressBook();
                    BeginSwitchToAddressBookEntry(entryToConnect);
                    DestroyWindow(hwnd);
                }
                return 0;
            }
            else if (pnmh->code == LVN_COLUMNCLICK) {
                if (!g_app) return 0;
                LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lParam;
                if (pnmv->iSubItem == 0) g_app->addressBookSortMode = 1;
                else if (pnmv->iSubItem == 1) g_app->addressBookSortMode = 2;
                else if (pnmv->iSubItem == 2) g_app->addressBookSortMode = 3;
                SendMessageW(GetDlgItem(hwnd, ID_ADDRESSBOOK_SORT), CB_SETCURSEL, g_app->addressBookSortMode, 0);
                SortAddressBook();
                SaveAddressBook();
                RefreshAddressBookList(GetDlgItem(hwnd, ID_ADDRESSBOOK_LIST));
                return 0;
            }
            else if (pnmh->code == NM_CUSTOMDRAW) {
                LPNMLVCUSTOMDRAW lplvcd = (LPNMLVCUSTOMDRAW)lParam;
                switch (lplvcd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT:
                    if (lplvcd->nmcd.dwItemSpec % 2 == 0) {
                        lplvcd->clrTextBk = RGB(255, 255, 255);
                    }
                    else {
                        lplvcd->clrTextBk = RGB(242, 245, 249);
                    }
                    return CDRF_NEWFONT;
                }
            }
        }
        break;
    }

    case WM_SYSCHAR:  // ← ALT + 단축키 처리
    {
        wchar_t ch = towlower((wchar_t)wParam);
        if (ch == 'n') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_ADDRESSBOOK_NEW, BN_CLICKED), 0); return 0; } // 새로 만들기
        if (ch == 'e') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_ADDRESSBOOK_EDIT, BN_CLICKED), 0); return 0; } // 수정
        if (ch == 'd') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_ADDRESSBOOK_DELETE, BN_CLICKED), 0); return 0; } // 삭제
        if (ch == 'c') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_ADDRESSBOOK_CONNECT, BN_CLICKED), 0); return 0; } // 연결
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_ADDRESSBOOK_NEW: {
            if (!g_app) return 0;
            AddressBookEntry entry; entry.port = 9999;
            if (PromptAddressBookEntryEditor(hwnd, entry, false)) {
                g_app->addressBook.push_back(entry); SaveAddressBook();
                HWND hList = GetDlgItem(hwnd, ID_ADDRESSBOOK_LIST);
                RefreshAddressBookList(hList);
                ListView_SetItemState(hList, g_app->addressBook.size() - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
            return 0;
        }
        case ID_ADDRESSBOOK_EDIT: {
            if (!g_app) return 0;
            HWND hList = GetDlgItem(hwnd, ID_ADDRESSBOOK_LIST);
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel < 0 || sel >= (int)g_app->addressBook.size()) return 0;
            AddressBookEntry entry = g_app->addressBook[sel];
            if (PromptAddressBookEntryEditor(hwnd, entry, true)) {
                g_app->addressBook[sel] = entry; SaveAddressBook();
                RefreshAddressBookList(hList);
                ListView_SetItemState(hList, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
            return 0;
        }
        case ID_ADDRESSBOOK_DELETE: {
            if (!g_app) return 0;
            HWND hList = GetDlgItem(hwnd, ID_ADDRESSBOOK_LIST);
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel < 0 || sel >= (int)g_app->addressBook.size()) return 0;
            if (ConfirmDeleteAddressBookEntry(hwnd, g_app->addressBook[sel].name)) {
                g_app->addressBook.erase(g_app->addressBook.begin() + sel);
                SaveAddressBook(); RefreshAddressBookList(hList);
                int count = (int)g_app->addressBook.size();
                if (count > 0) ListView_SetItemState(hList, min(sel, count - 1), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
            return 0;
        }
        case ID_ADDRESSBOOK_CONNECT: {
            if (!g_app) return 0;
            HWND hList = GetDlgItem(hwnd, ID_ADDRESSBOOK_LIST);
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel < 0 || sel >= (int)g_app->addressBook.size()) return 0;
            AddressBookEntry entryToConnect = g_app->addressBook[sel];
            FILETIME ft; GetSystemTimeAsFileTime(&ft);
            g_app->addressBook[sel].lastConnected = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
            SortAddressBook(); SaveAddressBook();
            BeginSwitchToAddressBookEntry(entryToConnect);
            DestroyWindow(hwnd);
            return 0;
        }
        case ID_ADDRESSBOOK_SORT: {
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                if (!g_app) return 0;
                int sel = (int)SendMessageW(GetDlgItem(hwnd, ID_ADDRESSBOOK_SORT), CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) {
                    g_app->addressBookSortMode = sel;
                    SortAddressBook();
                    SaveAddressBook();
                    RefreshAddressBookList(GetDlgItem(hwnd, ID_ADDRESSBOOK_LIST));
                }
            }
            return 0;
        }
        case IDCANCEL: DestroyWindow(hwnd); return 0;
        }
        break;

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
    }
    case WM_DESTROY:
        return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
