#pragma once

#include <windows.h>

bool StartTinTinProcess();
void ReaderThreadProc(HWND hwndMain, HANDLE hRead);
void StopProcessAndThread();
