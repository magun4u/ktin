#pragma once
#include "constants.h"
#include "types.h"
#include <cstddef>

std::wstring GetAppVersionString();
std::wstring GetTinTinVersionString();

extern HHOOK g_hMsgBoxHook;
extern HWND g_hMsgBoxOwner;

HFONT GetCurrentAppFont(int size, int weight = FW_NORMAL);

std::wstring Trim(const std::wstring& s);
std::wstring FormatNumberWithCommas(const std::wstring& s);
std::wstring ApplyAbbreviationToText(const std::wstring& input);
std::string WideToUtf8(const std::wstring& ws);
std::string WideToMultiByte(const std::wstring& ws, UINT codePage);
std::wstring MultiByteToWide(const std::string& s, UINT codePage);
std::wstring Utf8ToWide(const std::string& s);
std::wstring GetModuleDirectory();
std::wstring MakeAbsolutePath(const std::wstring& baseDir, const std::wstring& relativePath);
std::wstring GetSessionsPath();
std::wstring GetSettingsPath();
bool SetClipboardUnicodeText(HWND hwnd, const std::wstring& text);
void CopyToClipboard(HWND hwnd, const std::wstring& text);
void SaveTextToFile(HWND hwnd, const std::wstring& text);

bool WriteAllToWinFile(HANDLE file, const void* data, size_t len);
bool WriteBytesToFile(const std::wstring& path, const void* data, size_t len, bool append = false);
bool WriteStringToFile(const std::wstring& path, const std::string& data, bool append = false);
bool WriteUtf8BomTextFile(const std::wstring& path, const std::string& utf8);
void PlayAudioFile(const std::wstring& path);
std::wstring GetEditTextW(HWND hEdit);
std::wstring GetWindowTextString(HWND hwnd);
int GetInputAreaHeight();
int GetShortcutBarHeight();
HFONT GetPopupUIFont(HWND hwnd);
HFONT GetShortcutButtonUIFont(HWND hwnd);
void CleanupCachedUiFonts();
void ApplyPopupTitleBarTheme(HWND hwnd);
LONG MakeLfHeightFromPointSize(HWND hwnd, int pt);
int GetFontPointSizeFromLogFont(const LOGFONTW& lf);
BYTE GetCurrentFontQuality();
bool ChooseFontOnly(HWND hwnd, LOGFONTW& font);
bool ChooseColorOnly(HWND hwnd, COLORREF& color);
bool ChooseBackgroundColor(HWND hwnd, COLORREF& color);
bool ChooseScriptFile(HWND hwnd, std::wstring& outPath);
bool ChooseFontAndColor(HWND hwnd, UiStyle& style);
int GetStatusBarHeight();
int ClampInt(int v, int lo, int hi);
int ClampByteRange(int v, int lo, int hi);
int ShowCenteredMessageBox(HWND hwnd, const wchar_t* text, const wchar_t* caption, UINT type);
void MeasureOwnerDrawMenuItem(HWND hwnd, MEASUREITEMSTRUCT* mis);
void DrawOwnerDrawMenuItem(DRAWITEMSTRUCT* dis);
bool FindOwnerDrawMenuMeta(HMENU hMenu, ULONG_PTR itemData, bool* hasSubmenu);
void GetTerminalOffset(HWND hwnd, int& offsetX, int& offsetY);
SIZE GetLogCellPixelSize(HWND hwnd);
void ResetLogCellPixelSizeCache();
bool FitWindowToScreenGrid(HWND hwnd, int cols, int rows, bool onlyIfTooSmall = false);
void ScrollRichEditByLines(HWND hwndRich, int lineDelta);
void JumpRichEditToTop(HWND hwndRich);
void JumpRichEditToBottom(HWND hwndRich);
void SendTextToMud(const std::wstring& text);
void SendRawCommandToMud(const std::wstring& text);
void MarkKnownTinTinSession(const std::wstring& sessionName);
void ResetKnownTinTinSession();
bool ZapKnownTinTinSession();
void SendKeepAliveNow();
void SendCommandToProcess(const std::wstring& line);
void SaveLastConnectCommand(const std::wstring& text);
void ShowTrayIcon(HWND hwnd);
void HideTrayIcon(HWND hwnd);
void AddStyledText(HWND hRich, const wchar_t* text, int fontSize, bool bold, COLORREF color, int spaceBefore = 0);
void AppendRunsToRichEdit(HWND hwndRich, const std::vector<StyledRun>& runs);
void NormalizeRunTextForRichEdit(std::wstring& s);
void SetupRichEditDefaults(HWND hwndRich);
void SetupChatRichEditDefaults(HWND hwndRich);
std::wstring ColorToString(COLORREF c);
COLORREF StringToColor(const wchar_t* s, COLORREF def = 0);
int GetSyntaxLanguageFromPath(const std::wstring& path);
int GetCustomMenuItemWidth(int index);
int HitTestCustomMenuBar(int x, int y);
void DrawCustomMenuBar(HDC hdc, HWND hwnd);
bool IsRichEditNearBottom(HWND hwndRich);
void EnsureVisibleEditCaret(HWND hwndEdit);
void InitStyleFont(LOGFONTW& lf, HWND hwnd, int pointSize);
void RegisterEmbeddedFont();
void UnloadEmbeddedFont();
// 비밀번호 간단 암호화 (XOR)
std::wstring SimpleEncrypt(const std::wstring& plain);
std::wstring SimpleDecrypt(const std::wstring& cipher);