/* Copyright (C) 2003-2013 Runtime Revolution Ltd.

This file is part of LiveCode.

LiveCode is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License v3 as published by the Free
Software Foundation.

LiveCode is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with LiveCode.  If not see <http://www.gnu.org/licenses/>.  */

#include "w32prefix.h"

#include "globdefs.h"
#include "filedefs.h"
#include "objdefs.h"
#include "parsedef.h"

#include "globals.h"

#include "w32dc.h"

#include <psapi.h>

////////////////////////////////////////////////////////////////////////////////

typedef bool (*MCSystemListProcessesCallback)(void *context, uint32_t id, const char *path, const char *description);
typedef bool (*MCSystemListProcessModulesCallback)(void *context, const char *path);

////////////////////////////////////////////////////////////////////////////////

static bool WindowsGetModuleFileNameEx(HANDLE p_process, HMODULE p_module, char*& r_path)
{
	bool t_success;
	t_success = true;

	// For some unfathomable reason, it is not possible find out how big a
	// buffer you might need for a module file name. Instead we loop until
	// we are sure we have the whole thing.
	char *t_path;
	uint32_t t_path_length;
	t_path_length = 0;
	t_path = nil;
	while(t_success)
	{
		t_path_length += 256;
		MCMemoryDeleteArray(t_path);

		if (t_success)
			t_success = MCMemoryNewArray(t_path_length, t_path);

		DWORD t_result;
		t_result = 0;
		if (t_success)
		{
			// If the buffer is too small, the result will equal the input
			// buffer size.
			t_result = GetModuleFileNameExA(p_process, p_module, t_path, t_path_length);
			if (t_result == 0)
				t_success = false;
			else if (t_result == t_path_length)
				continue;
		}

		if (t_success)
			break;
	}

	if (t_success)
		r_path = t_path;
	else
		MCMemoryDeleteArray(t_path);

	return t_success;
}

static bool WindowsGetModuleDescription(const char *p_path, char*& r_description)
{
	bool t_success;
	t_success = true;

	DWORD t_size;
	t_size = 0;
	if (t_success)
	{
		t_size = GetFileVersionInfoSizeA(p_path, nil);
		if (t_size == 0)
			t_success = false;
	}

	void *t_data;
	t_data = nil;
	if (t_success)
		t_success = MCMemoryAllocate(t_size, t_data);

	if (t_success &&
		!GetFileVersionInfoA(p_path, 0, t_size, t_data))
		t_success = false;

	UINT t_desc_length;
	char *t_desc;
	t_desc_length = 0;
	t_desc = nil;
	if (t_success &&
		!VerQueryValueA(t_data, "\\StringFileInfo\\040904b0\\FileDescription", (void **)&t_desc, &t_desc_length))
		t_success = false;

	if (t_success)
		t_success = MCCStringCloneSubstring(t_desc, t_desc_length, r_description);

	MCMemoryDeallocate(t_data);
	
	return t_success;
}

////////////////////////////////////////////////////////////////////////////////

bool MCSystemListProcesses(MCSystemListProcessesCallback p_callback, void* p_context)
{
	bool t_success;
	t_success = true;

	uint32_t t_process_count;
	DWORD *t_process_ids;
	t_process_count = 0;
	t_process_ids = NULL;
	while(t_success)
	{
		if (t_success)
			t_success = MCMemoryResizeArray(t_process_count + 1024, t_process_ids, t_process_count);

		DWORD t_bytes_used;
		if (t_success &&
			!EnumProcesses(t_process_ids, t_process_count * sizeof(DWORD), &t_bytes_used))
			t_success = false;

		if (t_bytes_used < t_process_count * sizeof(DWORD))
		{
			t_process_count = t_bytes_used / sizeof(DWORD);
			break;
		}
	}

	for(uint32_t i = 0; i < t_process_count && t_success; i++)
	{
		HANDLE t_process;
		t_process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, t_process_ids[i]);
		if (t_process != NULL)
		{
			HMODULE t_module;
			DWORD t_bytes_used;
			if (EnumProcessModules(t_process, &t_module, sizeof(HMODULE), &t_bytes_used))
			{
				char *t_process_path;
				t_process_path = nil;
				if (WindowsGetModuleFileNameEx(t_process, t_module, t_process_path))
				{
					char *t_process_description;
					t_process_description = nil;
					WindowsGetModuleDescription(t_process_path, t_process_description);

					t_success = p_callback(p_context, t_process_ids[i], t_process_path, t_process_description);

					MCCStringFree(t_process_description);
					MCCStringFree(t_process_path);
				}
			}
			CloseHandle(t_process);
		}
	}

	MCMemoryDeleteArray(t_process_ids);

	return t_success;
}

////////////////////////////////////////////////////////////////////////////////

bool MCSystemListProcessModules(uint32_t p_process_id, MCSystemListProcessModulesCallback p_callback, void *p_context)
{
	bool t_success;
	t_success = true;

	HANDLE t_process;
	t_process = nil;
	if (t_success)
	{
		t_process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, p_process_id);
		if (t_process == nil)
			t_success = false;
	}

	uint32_t t_module_count;
	HMODULE *t_modules;
	t_module_count = 0;
	t_modules = NULL;
	while(t_success)
	{
		if (t_success)
			t_success = MCMemoryResizeArray(t_module_count + 1024, t_modules, t_module_count);

		DWORD t_bytes_used;
		if (t_success &&
			!EnumProcessModules(t_process, t_modules, t_module_count * sizeof(HMODULE), &t_bytes_used))
			t_success = false;

		if (t_bytes_used < t_module_count * sizeof(DWORD))
		{
			t_module_count = t_bytes_used / sizeof(DWORD);
			break;
		}
	}

	for(uint32_t i = 0; i < t_module_count && t_success; i++)
	{
		char *t_module_path;
		t_module_path = nil;
		if (WindowsGetModuleFileNameEx(t_process, t_modules[i], t_module_path))
		{
			t_success = p_callback(p_context, t_module_path);
			MCCStringFree(t_module_path);
		}
	}

	MCMemoryDeleteArray(t_modules);

	if (t_process != nil)
		CloseHandle(t_process);

	return t_success;
}

////////////////////////////////////////////////////////////////////////////////

bool MCSystemCanDeleteKey(const char *p_key)
{
	HKEY t_key;
	if (MCCStringBeginsWith(p_key, "HKCU\\"))
		t_key = HKEY_CURRENT_USER;
	else if (MCCStringBeginsWith(p_key, "HKCR\\"))
		t_key = HKEY_CLASSES_ROOT;
	else if (MCCStringBeginsWith(p_key, "HKLM\\"))
		t_key = HKEY_LOCAL_MACHINE;
	else
		return false;

	HKEY t_subkey;
	t_subkey = nil;

	LONG t_result;
	t_result = RegOpenKeyExA(t_key, p_key + 5, 0, DELETE, &t_subkey);

	if (t_result == ERROR_FILE_NOT_FOUND || t_subkey != nil)
	{
		RegCloseKey(t_subkey);
		return true;
	}

	return false;
}

bool MCSystemCanDeleteFile(const char *p_file)
{
	HANDLE t_file;
	t_file = CreateFileA(p_file, DELETE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (t_file != INVALID_HANDLE_VALUE)
	{
		CloseHandle(t_file);
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////

// MM-2011-03-16: Added prototype but not yet implemented.
void MCSystemRequestUserAttention(void)
{
}

void MCSystemCancelRequestUserAttention(void)
{
}

// MM-2011-03-24: Extended the standard NOTIFYICONDATAA struct to include 
// extra fields for balloon title, icon and message.  These are (currently) ifdeffed out.
typedef struct _NOTIFYICONDATA500A {
    DWORD cbSize;
    HWND hWnd;
    UINT uID;
    UINT uFlags;
    UINT uCallbackMessage;
    HICON hIcon;
    CHAR   szTip[128];
    DWORD dwState;
    DWORD dwStateMask;
    WCHAR   szInfo[256];
    union {
        UINT  uTimeout;
        UINT  uVersion;  // used with NIM_SETVERSION, values 0, 3 and 4
    } DUMMYUNIONNAME;
    WCHAR   szInfoTitle[64];
    DWORD dwInfoFlags;
} NOTIFYICONDATA500A, *PNOTIFYICONDATA500A;

// MM-2011-03-24: Added.  Displays a balloon popup above the taskbar icon containing the given message and title.
void MCSystemBalloonNotification(MCStringRef p_title, MCStringRef p_message)
{
	// Shell_NotifyIconA uses the cbSize of the struct passed to determine what fields have been set
	// allowing us to use the extended NOTIFYICONDATA500A struct with the extra fields for balloons.
	NOTIFYICONDATA500A t_nidata;
	MCMemoryClear(&t_nidata, sizeof(NOTIFYICONDATA500A));
	t_nidata . cbSize = sizeof(NOTIFYICONDATA500A);

	// Fecth the window handle that we have bound the taskbar icon to.
	// Only one task bar icon has been created within this window hanlde, with ID 1.
	t_nidata . hWnd = ((MCScreenDC *)MCscreen) -> getinvisiblewindow();
	t_nidata . uID = 1;

	// The NIF_INFO flag determines that the popup should be a balloon rather than a tooltip.
	// The NIIF_INFO flag determines that the info (little i) icon should be used in the balloon.
	t_nidata . uFlags = NIF_INFO;
	t_nidata . dwInfoFlags = NIIF_INFO;

    MCAutoStringRefAsWString t_title, t_message;
    t_title . Lock(p_title);
    t_message . Lock(r_message);
    
	// We can specify the title (appears in bold next to the icon) and the body of the balloon.
	if (p_title != nil)
		MCMemoryCopy(t_nidata . szInfoTitle, *t_title, 63);
	else
		t_nidata . szInfoTitle[0] = '\0';
	if (p_message != nil)
		MCMemoryCopy(t_nidata . szInfo, *t_message, 255);
	else
		t_nidata . szInfo[0] = '\0';

	// Call with NIM_MODIFY to flag that we want to update an existing taskbar icon.
	Shell_NotifyIconW(NIM_MODIFY, (PNOTIFYICONDATAA) &t_nidata);
}

////////////////////////////////////////////////////////////////////////////////
