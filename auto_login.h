#pragma once
#include <string>
#include <windows.h>
#include "types.h"

void RunAutoLoginEngine(const std::wstring& text);
void StartAutoLoginWindowFromGlobal();
void StartAutoLoginWindowForAddressEntry(const AddressBookEntry& entry);
void StopAutoLoginWindow(bool allowKeepAlive);
bool IsAutoLoginKeepAliveBlocked();
void NotifyPossibleConnectionCommand(const std::wstring& text);
