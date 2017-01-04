////////////////////////////////////////////////////////////////////////////////////////////////////////
// Part of Injectable Generic Camera System
// Copyright(c) 2016, Frans Bouma
// All rights reserved.
// https://github.com/FransBouma/InjectableGenericCameraSystem
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met :
//
//  * Redistributions of source code must retain the above copyright notice, this
//	  list of conditions and the following disclaimer.
//
//  * Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and / or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "stdafx.h"
#include "Utils.h"
#include "MinHook.h"
#include "Gamepad.h"

using namespace std;

//--------------------------------------------------------------------------------------------------------------------------------
// Externs
extern bool g_inputBlocked;
extern Gamepad* g_gamePad;
extern Console* g_consoleWrapper;

//--------------------------------------------------------------------------------------------------------------------------------
// Forward declarations
bool HandleMessage(LPMSG lpMsg);
void ProcessMessage(LPMSG lpMsg, bool removeIfRequired);

//--------------------------------------------------------------------------------------------------------------------------------
// Typedefs of functions to hook
typedef DWORD (WINAPI *XINPUTGETSTATE)(DWORD, XINPUT_STATE*);
typedef BOOL (WINAPI *GETMESSAGEA)(LPMSG, HWND, UINT, UINT);
typedef BOOL (WINAPI *GETMESSAGEW)(LPMSG, HWND, UINT, UINT);
typedef BOOL (WINAPI *PEEKMESSAGEA)(LPMSG, HWND, UINT, UINT, UINT);
typedef BOOL (WINAPI *PEEKMESSAGEW)(LPMSG, HWND, UINT, UINT, UINT);

//--------------------------------------------------------------------------------------------------------------------------------
// Pointers to the original hooked functions
static XINPUTGETSTATE hookedXInputGetState = nullptr;
static GETMESSAGEA hookedGetMessageA = nullptr;
static GETMESSAGEW hookedGetMessageW = nullptr;
static PEEKMESSAGEA hookedPeekMessageA = nullptr;
static PEEKMESSAGEW hookedPeekMessageW = nullptr;

//--------------------------------------------------------------------------------------------------------------------------------
// Implementations

// wrapper for easier setting up hooks for MinHook
template <typename T>
inline MH_STATUS MH_CreateHookEx(LPVOID pTarget, LPVOID pDetour, T** ppOriginal)
{
	return MH_CreateHook(pTarget, pDetour, reinterpret_cast<LPVOID*>(ppOriginal));
}

template <typename T>
inline MH_STATUS MH_CreateHookApiEx(
	LPCWSTR pszModule, LPCSTR pszProcName, LPVOID pDetour, T** ppOriginal)
{
	return MH_CreateHookApi(
		pszModule, pszProcName, pDetour, reinterpret_cast<LPVOID*>(ppOriginal));
}


// Our own version of XInputGetState
DWORD WINAPI DetourXInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState)
{
	// first call the original function
	DWORD toReturn = hookedXInputGetState(dwUserIndex, pState);
	// check if the passed in pState is equal to our gamestate. If so, always allow.
	if (pState != g_gamePad->getState())
	{
		// check if the camera is enabled. If so, zero the state
		if (g_inputBlocked)
		{
			ZeroMemory(pState, sizeof(XINPUT_STATE));
		}
	}
	return toReturn;
}


// Our own version of GetMessageA
BOOL WINAPI DetourGetMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
	// first call the original function
	if (!hookedGetMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax))
	{
		return FALSE;
	}
	ProcessMessage(lpMsg, true);
	return TRUE;
}


// Our own version of GetMessageW
BOOL WINAPI DetourGetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
	// first call the original function
	if (!hookedGetMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax))
	{
		return FALSE;
	}
	ProcessMessage(lpMsg, true);
	return TRUE;
}


// Our own version of PeekMessageA
BOOL WINAPI DetourPeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
	// first call the original function
	if (!hookedPeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg))
	{
		return FALSE;
	}
	ProcessMessage(lpMsg, wRemoveMsg & PM_REMOVE);
	return TRUE;
}

// Our own version of PeekMessageW
BOOL WINAPI DetourPeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
	// first call the original function
	if (!hookedPeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg))
	{
		return FALSE;
	}
	ProcessMessage(lpMsg, wRemoveMsg & PM_REMOVE);
	return TRUE;
}


void ProcessMessage(LPMSG lpMsg, bool removeIfRequired)
{
	if (lpMsg->hwnd != nullptr && removeIfRequired && HandleMessage(lpMsg))
	{
		// message was handled by our code. This means it's a message we want to block if input blocking is enabled. 
		if (g_inputBlocked)
		{
			lpMsg->message = WM_NULL;	// reset to WM_NULL so the host will receive a dummy message instead.
		}
	}
}


bool HandleMessage(LPMSG lpMsg)
{
	// only handle the message if the camera is enabled, otherwise ignore it as the camera isn't controllable 
	if (lpMsg == nullptr || lpMsg->hwnd == nullptr || !IsCameraEnabled())
	{
		return false;
	}
	bool toReturn = false;
	switch (lpMsg->message)
	{
		case WM_INPUT:
		{
			// handle mouse
			RAWINPUT *pRI = NULL;

			// Determine how big the buffer should be
			UINT iBuffer;
			GetRawInputData((HRAWINPUT)lpMsg->lParam, RID_INPUT, NULL, &iBuffer, sizeof(RAWINPUTHEADER));
			// Allocate a buffer with enough size to hold the raw input data
			LPBYTE lpb = new BYTE[iBuffer];
			if (lpb == NULL)
			{
				return 0;
			}
			// Get the raw input data
			UINT readSize = GetRawInputData((HRAWINPUT)lpMsg->lParam, RID_INPUT, lpb, &iBuffer, sizeof(RAWINPUTHEADER));
			if (readSize == iBuffer)
			{
				pRI = (RAWINPUT*)lpb;
				// Process the Mouse Messages
				if (pRI->header.dwType == RIM_TYPEMOUSE)
				{
#error CONTINUE HERE
					//ProcessRawMouseData(&pRI->data.mouse);
				}
			}
			delete lpb;
			toReturn = true;
		}
		break;
		// simply return 1 for all messages related to mouse / keyboard so they won't reach the message pump of the main window. 
		case WM_KEYDOWN:
		case WM_KEYUP:
		case WM_CAPTURECHANGED:
		case WM_LBUTTONDBLCLK:
		case WM_LBUTTONDOWN:
		case WM_MBUTTONDBLCLK:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MOUSEACTIVATE:
		case WM_MOUSEHOVER:
		case WM_MOUSEHWHEEL:
		case WM_MOUSEMOVE:
		case WM_MOUSELEAVE:
		case WM_MOUSEWHEEL:
		case WM_NCHITTEST:
		case WM_NCLBUTTONDBLCLK:
		case WM_NCLBUTTONDOWN:
		case WM_NCLBUTTONUP:
		case WM_NCMBUTTONDBLCLK:
		case WM_NCMBUTTONDOWN:
		case WM_NCMBUTTONUP:
		case WM_NCMOUSEHOVER:
		case WM_NCMOUSELEAVE:
		case WM_NCMOUSEMOVE:
		case WM_NCRBUTTONDBLCLK:
		case WM_NCRBUTTONDOWN:
		case WM_NCRBUTTONUP:
		case WM_NCXBUTTONDBLCLK:
		case WM_NCXBUTTONDOWN:
		case WM_NCXBUTTONUP:
		case WM_RBUTTONDBLCLK:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_XBUTTONDBLCLK:
		case WM_XBUTTONDOWN:
		case WM_XBUTTONUP:
		case WM_LBUTTONUP:
			// say we handled it, so the host won't see it
			toReturn = true;
			break;
	}
	return toReturn;
}


// Sets the input hooks for the various input related functions we defined own wrapper functions for. After a successful hook setup
// they're enabled. 
void SetInputHooks()
{
	MH_Initialize();
	if (MH_CreateHookApiEx(L"xinput9_1_0", "XInputGetState", &DetourXInputGetState, &hookedXInputGetState) != MH_OK)
	{
		g_consoleWrapper->WriteError("Hooking XInput 9_1_0 failed!");
	}
#ifdef _DEBUG
	g_consoleWrapper->WriteLine("Hook set to XInputSetState");
#endif



	// Enable all hooks
	if (MH_EnableHook(MH_ALL_HOOKS) == MH_OK)
	{
#ifdef _DEBUG
		g_consoleWrapper->WriteLine("All hooks enabled");
#endif
	}
	else
	{
		g_consoleWrapper->WriteError("Enabling hooks failed");
	}
}