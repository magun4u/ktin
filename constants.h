#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

// 1. 공통 Windows & C++ 헤더
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h> 
#include <commdlg.h>
#include <richedit.h>
#include <richole.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <mmsystem.h> 

#include <algorithm> 
#include <atomic>
#include <cwctype>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <regex>
#include <shellapi.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using std::min;
using std::max;

// 2. 기본 상수 및 ConPTY 매크로
#define INPUT_ROWS                  3
#define SHORTCUT_BUTTON_COUNT       10
#define STATUS_BAR_HEIGHT           22
#define SHORTCUT_BAR_HEIGHT         32

#define WM_APP_LOG_CHUNK            (WM_APP + 1)
#define WM_APP_PROCESS_EXIT         (WM_APP + 2)
#define WM_APP_TRAYICON             (WM_APP + 3)

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

typedef HANDLE HPCON;
typedef HRESULT(WINAPI* PFN_CreatePseudoConsole)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
typedef HRESULT(WINAPI* PFN_ResizePseudoConsole)(HPCON, COORD);
typedef VOID(WINAPI* PFN_ClosePseudoConsole)(HPCON);

#define IDC_MEMOFIND_QUERY 101
#define IDC_MEMOFIND_REPLACE 102
#define IDC_MEMOFIND_CASE 103
#define IDC_MEMOFIND_NEXT 104
#define IDC_MEMOFIND_PREV 105
#define IDC_MEMOREPLACE_DO 106
#define IDC_MEMOREPLACE_ALL 107
#define IDC_MEMOGOTO_EDIT 108

// 3. UI 관련 각종 ID 모음
static const int ID_MENU_OPTIONS_SHORTCUTBAR = 1012;
static const int ID_MENU_OPTIONS_SAVE_INPUT_ON_EXIT = 1013;
static const int ID_MENU_OPTIONS_SHORTCUT_EDIT = 1014;
static const int ID_MENU_OPTIONS_CAPTURE_LOG = 1015;
static const int ID_MENU_OPTIONS_SCREEN_SIZE = 1017;
static const int ID_MENU_OPTIONS_FIT_WINDOW = 1018;
static const int ID_MENU_VIEW_SMOOTH_FONT = 1019;
static const int ID_MENU_HELP_ABOUT = 1020;


static const int ID_MENU_STYLE_LOG_FONT = 1001;
static const int ID_MENU_STYLE_LOG_COLOR = 1002;
static const int ID_MENU_STYLE_INPUT_FONT = 1003;
static const int ID_MENU_STYLE_INPUT_COLOR = 1004;
static const int ID_MENU_BG_MAIN = 1005;
static const int ID_MENU_BG_INPUT = 1006;
static const int ID_MENU_EXIT = 1007;
static const int ID_MENU_FILE_READ_SCRIPT = 1008;
static const int ID_MENU_FILE_ZAP = 1009;
static const int ID_MENU_FILE_QUICK_CONNECT = 1010;
static const int ID_MENU_OPTIONS_KEEPALIVE = 1011;
static const int ID_MENU_FILE_ADDRESSBOOK = 1016;

static const int ID_MENU_HELP_SHORTCUT = 1021;
static const int ID_MENU_OPTIONS_CHAT_DOCK = 1022;
static const int ID_MENU_OPTIONS_CHAT_CAPTURE = 1024;
static const int ID_MENU_OPTIONS_CHAT_TOGGLE_VISIBLE = 1025;
static const int ID_MENU_FILE_NEW_WINDOW = 1026;

static const int ID_LOG_COPY = 1101;
static const int ID_LOG_CLEAR_CHAT = 1102;
static const int ID_TIMER_DEFER_SAVE = 2001;
static const int ID_TIMER_KEEPALIVE = 2002;
static const int ID_TIMER_LOG_REDRAW = 2003;
static const int ID_TIMER_AUTORECONNECT = 2004;
static const int ID_TIMER_SWITCH_CONNECT = 2005;

// 타이머 엔진 구동용 Win32 타이머 ID
static const int ID_TIMER_USER_ENGINE = 2010;

static const int ID_KEEPALIVE_ENABLE = 3001;
static const int ID_KEEPALIVE_INTERVAL = 3002;
static const int ID_KEEPALIVE_COMMAND = 3003;
static const int ID_KEEPALIVE_LABEL_INTERVAL = 3004;
static const int ID_KEEPALIVE_LABEL_COMMAND = 3005;

static const int ID_SHORTCUT_BUTTON_BASE = 4000;
static const int ID_SHORTCUT_EDIT_LABEL = 4101;
static const int ID_SHORTCUT_EDIT_COMMAND = 4102;
static const int ID_SHORTCUT_EDIT_LABEL_TEXT = 4103;
static const int ID_SHORTCUT_EDIT_COMMAND_TEXT = 4104;

static const int ID_SHORTCUT_EDITOR_LABEL_BASE = 4200;

static const int ID_SHORTCUT_EDITOR_COMMAND_BASE = 4300;
static const int ID_SHORTCUT_EDITOR_RESET = 4400;
static const int ID_SHORTCUT_EDITOR_OFF_BASE = 4450;
static const int ID_SHORTCUT_EDITOR_TOGGLE_BASE = 4550;

static const int ID_ADDRESSBOOK_LIST = 4501;
static const int ID_ADDRESSBOOK_NEW = 4502;
static const int ID_ADDRESSBOOK_EDIT = 4503;
static const int ID_ADDRESSBOOK_DELETE = 4504;
static const int ID_ADDRESSBOOK_LAUNCH = 4505;
static const int ID_ADDRESSBOOK_CONNECT = 4506;

static const int ID_ADDRESSBOOK_NAME = 4510;
static const int ID_ADDRESSBOOK_HOST = 4511;
static const int ID_ADDRESSBOOK_PORT = 4512;
static const int ID_ADDRESSBOOK_SCRIPT = 4513;
static const int ID_ADDRESSBOOK_BROWSE = 4514;
static const int ID_ADDRESSBOOK_CHARSET = 4515;

static const int ID_ADDRESSBOOK_CHK_AL = 4516;
static const int ID_ADDRESSBOOK_AL_ID_PAT = 4517;
static const int ID_ADDRESSBOOK_AL_ID = 4518;
static const int ID_ADDRESSBOOK_AL_PW_PAT = 4519;
static const int ID_ADDRESSBOOK_AL_PW = 4520;
static const int ID_ADDRESSBOOK_SORT = 4521;
static const int ID_ADDRESSBOOK_CHK_AUTORECONN = 4522;

static const int ID_ADDRESSBOOK_LOGIN_SUCCESS1 = 4523;
static const int ID_ADDRESSBOOK_LOGIN_SUCCESS2 = 4524;
static const int ID_ADDRESSBOOK_LOGIN_SUCCESS3 = 4525;

static const int ID_ADDRESSBOOK_LOGIN_FAIL1 = 4526;
static const int ID_ADDRESSBOOK_LOGIN_FAIL2 = 4527;
static const int ID_ADDRESSBOOK_LOGIN_FAIL3 = 4528;

static const int ID_SCREEN_SIZE_COLS = 4601;
static const int ID_SCREEN_SIZE_ROWS = 4602;

static const int ID_MENU_VIEW_HIDE_MENU = 4603;
static const int ID_LOG_SHOW_MENU = 4604;
static const int ID_MENU_THEME_DIALOG = 4605;
static const int ID_MENU_VIEW_SYMBOLS = 4606;

static const int ID_THEME_WINDOWS = 4701;
static const int ID_THEME_XTERM = 4702;
static const int ID_THEME_CAMPBELL = 4703;
static const int ID_THEME_POWERSHELL = 4704;
static const int ID_THEME_ONE_HALF_DARK = 4705;
static const int ID_THEME_SOLARIZED_DARK = 4706;
static const int ID_THEME_TANGO_DARK = 4707;
static const int ID_THEME_UBUNTU = 4708;
static const int ID_THEME_UBUNTU_COLOR = 4709;
static const int ID_THEME_VINTAGE = 4710;
static const int ID_THEME_DIMIDIUM = 4711;
static const int ID_THEME_CGA = 4712;
static const int ID_THEME_IBM5153 = 4713;
static const int ID_THEME_ALMALINUX = 4714;
static const int ID_THEME_DARKPLUS = 4715;
static const int ID_THEME_ONE_HALF_LIGHT = 4716;
static const int ID_THEME_OTTOSSON = 4717;
static const int ID_THEME_SOLARIZED_LIGHT = 4718;
static const int ID_THEME_TANGO_LIGHT = 4719;
static const int ID_THEME_UBUNTU_2004 = 4720;
static const int ID_THEME_UBUNTU_2204 = 4721;

static const int ID_MENU_FIND_DIALOG = 4801;
static const int ID_FIND_EDIT = 4802;
static const int ID_FIND_MATCHCASE = 4803;
static const int ID_FIND_FROM_START = 4804;
static const int ID_FIND_UP = 4805;
static const int ID_FIND_DOWN = 4806;
static const int ID_FIND_BUTTON = 4807;
static const int ID_FIND_CANCEL = 4808;
static const int ID_MENU_EDIT_HIGHLIGHT = 4809;

static const int ID_MENU_EDIT_COPY_PAST = 4810;
static const int ID_MENU_EDIT_SAVE_PAST = 4811;
static const int ID_MENU_EDIT_COPY_CUR = 4812;
static const int ID_MENU_EDIT_SAVE_CUR = 4813;
static const int ID_MENU_OPTIONS_KEEPALIVE_TOGGLE = 4817;
static const int ID_MENU_OPTIONS_CHAT_TIME_TOGGLE = 4818;

static const int ID_MENU_EDIT_ABBREVIATION = 4814;
static const int ID_MENU_EDIT_VARIABLE = 4815;
static const int ID_MENU_EDIT_NUMPAD = 4816;
static const int ID_MENU_EDIT_MEMO = 4819;
static const int ID_MENU_EDIT_FUNCTION_SHORTCUT = 4820;
static const int ID_MEMO_EDIT = 4821;
static const int ID_MEMO_STATUS = 4822;

static const int ID_MEMO_FILE_OPEN = 4823;
static const int ID_MEMO_FILE_SAVE = 4824;
static const int ID_MEMO_FILE_SAVEAS = 4825;
static const int ID_MEMO_FILE_EXIT = 4826;
static const int ID_MEMO_EDIT_UNDO = 4827;
static const int ID_MEMO_EDIT_REDO = 4828;
static const int ID_MEMO_EDIT_CUT = 4829;
static const int ID_MEMO_EDIT_COPY = 4830;
static const int ID_MEMO_EDIT_PASTE = 4831;
static const int ID_MEMO_EDIT_DELETE = 4832;
static const int ID_MEMO_EDIT_SELECTALL = 4833;
// 메뉴바 호출용 ID
static const int ID_MENU_EDIT_TIMER = 4838;

static const int ID_MEMO_EDIT_FIND = 4880;
static const int ID_MEMO_EDIT_FIND_NEXT = 4881;
static const int ID_MEMO_EDIT_FIND_PREV = 4882;
static const int ID_MEMO_EDIT_REPLACE = 4883;
static const int ID_MEMO_EDIT_GOTO = 4884;
static const int ID_MEMO_FILE_LOAD_AUTOSAVE = 4890;
static const int ID_MEMO_VIEW_LINENUMBER = 4897;

#define IDC_MEMOFIND_QUERY 101
#define IDC_MEMOFIND_REPLACE 102
#define IDC_MEMOFIND_CASE 103
#define IDC_MEMOFIND_NEXT 104
#define IDC_MEMOFIND_PREV 105
#define IDC_MEMOREPLACE_DO 106
#define IDC_MEMOREPLACE_ALL 107
#define IDC_MEMOGOTO_EDIT 108

static const int ID_MEMO_DRAW_TOGGLE = 4834;
static const int ID_MEMO_AUTOSAVE_TOGGLE = 4835;
static const int ID_MEMO_REPEAT_SYMBOL = 4836;
static const int ID_MEMO_EXIT_TO_TINTIN = 4837;
static const int ID_MEMO_RECENT_BASE = 4840;
static const int ID_TIMER_MEMO_AUTOSAVE = 4849;

static const int ID_MEMO_FORMAT_TEXT_COLOR = 4860;
static const int ID_MEMO_FORMAT_BACK_COLOR = 4861;
static const int ID_MEMO_FORMAT_FONT = 4862;
static const int ID_MEMO_FORMAT_WRAP_WIDTH = 4863;
static const int ID_MEMO_ALIGN_LEFT = 4864;
static const int ID_MEMO_ALIGN_CENTER = 4865;
static const int ID_MEMO_ALIGN_RIGHT = 4866;

static const int ID_MEMO_EDIT_DOC_START = 4870;
static const int ID_MEMO_EDIT_DOC_END = 4871;
static const int ID_MEMO_EDIT_SCR_START = 4872;
static const int ID_MEMO_EDIT_SCR_END = 4873;
static const int ID_MEMO_EDIT_DEL_END = 4874;
static const int ID_MEMO_EDIT_DEL_WORD_LEFT = 4875;
static const int ID_MEMO_EDIT_DEL_WORD_RIGHT = 4876;
static const int ID_MEMO_EDIT_DEL_LINE = 4877;

static const int ID_MEMO_FORMAT_ENC_UTF8 = 4895;
static const int ID_MEMO_FORMAT_ENC_CP949 = 4896;
static const int ID_MEMO_VIEW_FORMATMARKS = 4898;

static const int ID_MEMO_FORMAT_SYNTAX_TINTIN = 4900;
static const int ID_HOTKEY_FIND_DIALOG = 4901;
static const int ID_HOTKEY_FIND_NEXT = 4902;
static const int ID_HOTKEY_FIND_PREV = 4903;

static const int ID_MEMO_SYNTAX_THEME_BASE = 4910;
static const int ID_MEMO_SYNTAX_THEME_CLASSIC = 4910;
static const int ID_MEMO_SYNTAX_THEME_ULTRAEDIT = 4911;
static const int ID_MEMO_SYNTAX_THEME_VSCODE = 4912;
static const int ID_MEMO_SYNTAX_THEME_MONOKAI = 4913;
static const int ID_MEMO_SYNTAX_THEME_DRACULA = 4914;
static const int ID_MEMO_SYNTAX_THEME_DEFAULT_DARK = 4915;
static const int ID_MEMO_SYNTAX_THEME_SOLAR_LIGHT = 4916;
static const int ID_MEMO_SYNTAX_THEME_SOLAR_DARK = 4917;
static const int ID_MEMO_SYNTAX_THEME_GITHUB_DARK = 4918;

static const int ID_MEMO_FORMAT_SYNTAX_LANG_TINTIN = 4950;
static const int ID_MEMO_FORMAT_SYNTAX_LANG_CPP = 4951;
static const int ID_MEMO_FORMAT_SYNTAX_LANG_CSHARP = 4952;

static const int ID_MENU_SETTINGS = 5000;
static const int ID_SETTING_LIST = 5001;
static const int ID_SETTING_GRP_BASE = 5100;

static const int ID_SET_EDIT_COLS = 5101;
static const int ID_SET_EDIT_ROWS = 5102;
static const int ID_SET_CHK_SMOOTH = 5103;
static const int ID_SET_ALIGN_LEFT = 5104;
static const int ID_SET_ALIGN_CENTER = 5105;
static const int ID_SET_ALIGN_RIGHT = 5106;
static const int ID_SET_EDIT_CHAT_LINES = 5107;
static const int ID_SET_CHK_CHAT_TIME = 5108;
static const int ID_SET_CHK_AUTOLOGIN = 5109;
static const int ID_SET_EDIT_AL_ID_PAT = 5110;
static const int ID_SET_EDIT_AL_ID = 5111;
static const int ID_SET_EDIT_AL_PW_PAT = 5112;
static const int ID_SET_EDIT_AL_PW = 5113;
static const int ID_SET_CHK_USE_MUD_FONT = 5114;
static const int ID_SET_EDIT_AL_SUCCESS1 = 5115;
static const int ID_SET_EDIT_AL_SUCCESS2 = 5116;
static const int ID_SET_EDIT_AL_SUCCESS3 = 5117;
static const int ID_SET_EDIT_AL_FAIL1 = 5118;
static const int ID_SET_EDIT_AL_FAIL2 = 5119;
static const int ID_SET_EDIT_AL_FAIL3 = 5120;
static const int ID_SET_MARGIN_LEFT = 5121;
static const int ID_SET_MARGIN_RIGHT = 5122;
static const int ID_SET_MARGIN_TOP = 5123;
static const int ID_SET_MARGIN_BOTTOM = 5124;
static const int ID_SET_CHK_MAIN_TOPMOST = 5125;
static const int ID_SET_CHK_TAIL_SNAP = 5126;


static const int ID_SET_BTN_LOG_FONT = 5201;
static const int ID_SET_BTN_LOG_COLOR = 5202;
static const int ID_SET_BTN_INP_FONT = 5203;
static const int ID_SET_BTN_INP_COLOR = 5204;
static const int ID_SET_BTN_MAIN_BG = 5205;
static const int ID_SET_BTN_INP_BG = 5206;

static const int ID_SET_BTN_CHAT_FONT = 5207;
static const int ID_SET_BTN_CHAT_COLOR = 5208;
static const int ID_SET_BTN_CHAT_BG = 5209;

static const int ID_SET_PREVIEW_LOG_INFO = 5210;
static const int ID_SET_PREVIEW_LOG_TEXT = 5211;
static const int ID_SET_PREVIEW_LOG_BACK = 5212;
static const int ID_SET_PREVIEW_INP_INFO = 5220;
static const int ID_SET_PREVIEW_INP_TEXT = 5221;
static const int ID_SET_PREVIEW_INP_BACK = 5222;
static const int ID_SET_PREVIEW_CHAT_INFO = 5230;
static const int ID_SET_PREVIEW_CHAT_TEXT = 5231;
static const int ID_SET_PREVIEW_CHAT_BACK = 5232;

static const int ID_SET_CHK_KA_ENABLE = 5301;
static const int ID_SET_EDIT_KA_INT = 5302;
static const int ID_SET_EDIT_KA_CMD = 5303;

static const int ID_SET_CHK_SAVE_INP = 5401;
static const int ID_SET_CHK_CAPTURE = 5402;
static const int ID_SET_BTN_APPLY = 5403;
static const int ID_SET_CHK_AUTO_QUICK = 5404;
static const int ID_SET_CHK_AUTO_ADDR = 5405;
static const int ID_SET_CHK_BACKSPACE_LIMIT = 5406;
static const int ID_SET_CHK_CLOSE_TRAY = 5407;
static const int ID_SET_CHK_SOUND_ENABLE = 5408;
static const int ID_SET_CHK_AMBIGUOUS_WIDE = 5409;

static const int ID_SET_EDIT_SHORTCUT_BASE = 5500;
static const int ID_SET_EDIT_SCLABEL_BASE = 5600;

static const int ID_QUICK_COMBO = 6001;
static const int ID_QUICK_CHARSET = 6002;

static const int ID_HI_EDIT_BASE = 7000;
static const int ID_HI_FG_BOX_BASE = 7100;
static const int ID_HI_BG_BOX_BASE = 7200;
static const int ID_HI_CHK_INV_BASE = 7300;
static const int ID_HI_BTN_RESET = 7400;
static const int ID_HI_BTN_BATCH_FG = 7401;
static const int ID_HI_BTN_BATCH_BG = 7402;
static const int ID_HI_BTN_APPLY = 7403;

#define ID_HI_LIST          7500
#define ID_HI_BTN_ADD       7501
#define ID_HI_BTN_DEL       7502
#define ID_HI_BTN_UP        7503
#define ID_HI_BTN_DOWN      7504
#define ID_HI_BTN_CLONE     7505

#define ID_HI_DET_ENABLE    7600
#define ID_HI_DET_PATTERN   7601
#define ID_HI_DET_INVERSE   7602
#define ID_HI_DET_FG        7603
#define ID_HI_DET_BG        7604
#define ID_HI_DET_USECMD    7605
#define ID_HI_DET_CMD       7606
#define ID_HI_DET_BEEP      7607
#define ID_HI_DET_USESOUND  7608
#define ID_HI_DET_PATH      7609
#define ID_HI_DET_BROWSE    7610
#define ID_HI_DET_NAME      7611
#define ID_HI_DET_PLAY_SOUND 7612

static const int ID_CAP_CHK_BASE = 8000;
static const int ID_CAP_PAT_BASE = 8100;
static const int ID_CAP_FMT_BASE = 8200;
static const int ID_CAP_BTN_APPLY = 8301;
static const int ID_CAP_BTN_RESET = 8302;

static const int ID_SYMBOL_LIST = 9000;
static const int ID_SYMBOL_BTN_BASE = 9100;

static const int ID_ABBR_LIST = 9500;
static const int ID_ABBR_ADD = 9501;
static const int ID_ABBR_DELETE = 9502;
static const int ID_ABBR_UP = 9503;
static const int ID_ABBR_DOWN = 9504;
static const int ID_ABBR_CHECK_ALL = 9505;
static const int ID_ABBR_UNCHECK_ALL = 9506;
static const int ID_ABBR_APPLY = 9507;
static const int ID_ABBR_GLOBAL_ENABLE = 9508;

#define ID_ABBR_EDIT_SHORT 9601
#define ID_ABBR_EDIT_REPLACE 9602

#define ID_VAR_LIST 9700
#define ID_VAR_ADD 9701
#define ID_VAR_DELETE 9702
#define ID_VAR_APPLY 9707
#define ID_VAR_GLOBAL_ENABLE 9708
#define ID_VAR_EDIT_NAME 9801
#define ID_VAR_EDIT_VALUE 9802
#define ID_VAR_EDIT_TYPE 9803

#define ID_LINE_LIST 11000
#define ID_NP_CHK_ENABLE 10100
#define ID_NP_LBL_CURRENT 10101
#define ID_NP_EDIT_CMD 10102
#define ID_NP_BTN_SAVE_CMD 10103
#define ID_NP_BTN_BASE 10200

// --- 타이머 UI 컨트롤 ID ---
#define ID_TIMER_LIST        12000
#define ID_TIMER_BTN_ADD     12001
#define ID_TIMER_BTN_DEL     12002
#define ID_TIMER_BTN_UP      12003
#define ID_TIMER_BTN_DOWN    12004
#define ID_TIMER_EDIT_NAME   12010
#define ID_TIMER_EDIT_GROUP  12011
#define ID_TIMER_CHK_ENABLE  12012
#define ID_TIMER_CHK_REPEAT  12013
#define ID_TIMER_CHK_AUTOSTART 12014
#define ID_TIMER_EDIT_H      12015
#define ID_TIMER_EDIT_M      12016
#define ID_TIMER_EDIT_S      12017
#define ID_TIMER_EDIT_MS     12018
#define ID_TIMER_EDIT_CMD    12019
#define ID_TIMER_BTN_APPLY   12020
#define ID_TIMER_BTN_START       12021
// --- 아래 2줄을 추가하세요 ---
#define ID_TIMER_BTN_ENABLE_ALL  12022 
#define ID_TIMER_BTN_DISABLE_ALL 12023

#define ID_CHAT_CUT          13001   // 잘라내기
#define ID_CHAT_COPY         13002   // 복사하기
#define ID_CHAT_PASTE        13003   // 붙여넣기
#define ID_CHAT_DELETE       13004   // 삭제
#define ID_CHAT_DELETE_LINE  13005   // 행 삭제
#define ID_CHAT_CLEAR_ALL    13006   // 내용 지우기 (히스토리 포함)
#define ID_CHAT_SELECT_ALL   13007   // 모두 선택

#define IDC_SC_LIST   20001
#define IDC_SC_EDIT   20002
#define IDC_SC_ENABLE 20003
#define IDC_SC_SAVE   20004
#define IDC_SC_TAB    20005
#define IDC_SC_CLOSE  20006
#define IDC_SC_LABEL  20007

#define ID_EDIT_STATUSBAR      30000
#define IDC_STATUS_COUNT       30002
#define IDC_STATUS_APPLY       30003
#define IDC_STATUS_EDIT_BASE   30010
#define IDC_STATUS_ALIGN_BASE  30020
// --- 안전 갈무리 / Tail 보기 메뉴 ID ---
static const int ID_MENU_CAPTURE_TOGGLE          = 13200;
static const int ID_MENU_CAPTURE_OPEN_FOLDER     = 13201;
static const int ID_MENU_CAPTURE_CLOSE_ALL       = 13202;
static const int ID_MENU_CAPTURE_TAIL_ALL        = 13210;
static const int ID_MENU_CAPTURE_TAIL_CHAT       = 13211;
static const int ID_MENU_CAPTURE_TAIL_AUCTION    = 13212;
static const int ID_MENU_CAPTURE_TAIL_ITEM       = 13213;
static const int ID_MENU_CAPTURE_TAIL_CUSTOM     = 13214;
static const int ID_MENU_CAPTURE_FILTER_SETTINGS = 13215;
static const int ID_MENU_CAPTURE_TAIL_TALK       = 13216;
static const int ID_MENU_CAPTURE_TAIL_EXP        = 13217;
static const int ID_MENU_CAPTURE_TAIL_USER1      = 13218;
static const int ID_MENU_CAPTURE_TAIL_USER2      = 13219;
static const int ID_MENU_CAPTURE_TAIL_USER3      = 13220;
static const int ID_TIMER_TAIL_REFRESH           = 13240;
static const int ID_TAIL_EDIT                    = 13221;
static const int ID_TAIL_STATUS                  = 13222;
static const int ID_TAIL_PATTERN_EDIT            = 13223;
static const int ID_TAIL_PATTERN_OK              = 13224;
static const int ID_TAIL_PATTERN_CANCEL          = 13225;
static const int ID_TAIL_FILTER_CHAT             = 13226;
static const int ID_TAIL_FILTER_AUCTION          = 13227;
static const int ID_TAIL_FILTER_ITEM             = 13228;
static const int ID_TAIL_FILTER_TALK             = 13229;
static const int ID_TAIL_FILTER_EXP              = 13233;
static const int ID_TAIL_FILTER_USER1_NAME       = 13234;
static const int ID_TAIL_FILTER_USER1_PATTERN    = 13235;
static const int ID_TAIL_FILTER_USER2_NAME       = 13236;
static const int ID_TAIL_FILTER_USER2_PATTERN    = 13237;
static const int ID_TAIL_FILTER_USER3_NAME       = 13238;
static const int ID_TAIL_FILTER_USER3_PATTERN    = 13239;
static const int ID_TAIL_FILTER_OK               = 13250;
static const int ID_TAIL_FILTER_CANCEL           = 13251;
static const int ID_TAIL_FILTER_RESET            = 13252;
static const int ID_TAIL_FILTER_ANSI_ALL         = 13253;
static const int ID_TAIL_FILTER_ANSI_CHAT        = 13254;
static const int ID_TAIL_FILTER_ANSI_AUCTION     = 13255;
static const int ID_TAIL_FILTER_ANSI_TALK        = 13256;
static const int ID_TAIL_FILTER_ANSI_ITEM        = 13257;
static const int ID_TAIL_FILTER_ANSI_EXP         = 13258;
static const int ID_TAIL_FILTER_ANSI_USER1       = 13259;
static const int ID_TAIL_FILTER_ANSI_USER2       = 13272;
static const int ID_TAIL_FILTER_ANSI_USER3       = 13273;
// --- Tail window internal command IDs (buildfix22) ---
static const int ID_TAIL_TABCTRL                 = 13260;
static const int ID_TAIL_MENU_TAB_SETTINGS       = 13261;
static const int ID_TAIL_MENU_HIDE_MENU          = 13262;
static const int ID_TAIL_MENU_SHOW_MENU          = 13263;
static const int ID_TAIL_MENU_COPY               = 13264;
static const int ID_TAIL_MENU_SELECT_ALL         = 13265;
static const int ID_TAIL_MENU_CLOSE              = 13266;
static const int ID_TAIL_MENU_TOGGLE_STATUS      = 13267;
static const int ID_TAIL_MENU_TOPMOST            = 13270;
static const int ID_TAIL_TAB_SET_OK              = 13268;
static const int ID_TAIL_TAB_SET_CANCEL          = 13269;
static const int ID_TAIL_TAB_CHECK_BASE          = 13300;

