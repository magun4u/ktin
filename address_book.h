#pragma once
#include <windows.h>
#include "types.h"
#include <vector>
#include <string>

struct AddressBookEntryEditorState
{
    AddressBookEntry* entry = nullptr;
    bool accepted = false;
    bool isEdit = false;
};

// 함수 선언
bool PromptAddressBook(HWND hwnd);
bool PromptAddressBookEntryEditor(HWND hwnd, AddressBookEntry& entry, bool isEdit);
bool ConfirmDeleteAddressBookEntry(HWND hwnd, const std::wstring& name);

void RefreshAddressBookList(HWND hList);
void SortAddressBook();

void BeginSwitchToAddressBookEntry(const AddressBookEntry& entry);
void ConnectAddressBookEntry(const AddressBookEntry& entry);