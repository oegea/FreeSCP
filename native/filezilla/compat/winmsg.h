//---------------------------------------------------------------------------
// winmsg.h — minimal Win32 window-message + WSAAsyncSelect emulation for FileZilla's async socket
// layer. FileZilla drives sockets the Windows way: WSAAsyncSelect posts FD_* socket events as window
// messages to a hidden helper window, whose WndProc turns them into OnReceive/OnSend/... callbacks,
// pumped by a message loop. We emulate that on POSIX: a global message queue + window registry, plus
// a background select() thread that posts FD_* messages as sockets become ready.
//
// NOTE: WSAAsyncSelect is edge-ish (re-armed per operation); this first cut is level-triggered with
// per-(socket,event) one-shot latches to avoid busy-loops. Runtime semantics get tuned once the full
// FileZilla lib links and can be exercised against a real FTP server.
//---------------------------------------------------------------------------
#ifndef WINSCP_FZ_WINMSG_H
#define WINSCP_FZ_WINMSG_H

#include "afx.h"
#include "winsock2.h"

#define WM_USER     0x0400
#define WM_TIMER    0x0113
#define WM_DESTROY  0x0002
#define WM_PAINT    0x000F
#define PM_NOREMOVE 0x0000
#define PM_REMOVE   0x0001
#define GWL_USERDATA   (-21)
#define GWLP_USERDATA  (-21)
#define GWL_WNDPROC    (-4)

#define MAKELONG(lo, hi)  ((LONG)(((WORD)(lo)) | (((DWORD)((WORD)(hi))) << 16)))
#define WSAGETSELECTEVENT(l)  ((WORD)((l) & 0xFFFF))
#define WSAGETSELECTERROR(l)  ((WORD)(((l) >> 16) & 0xFFFF))
#define LOWORD_(l)  ((WORD)((l) & 0xFFFF))

typedef LRESULT (CALLBACK * WNDPROC)(HWND, UINT, WPARAM, LPARAM);

struct MSG { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; };
typedef MSG * LPMSG;

struct WNDCLASSEXW {
  UINT cbSize, style; WNDPROC lpfnWndProc; int cbClsExtra, cbWndExtra;
  HINSTANCE hInstance; void * hIcon; void * hCursor; void * hbrBackground;
  const wchar_t * lpszMenuName; const wchar_t * lpszClassName; void * hIconSm;
};
typedef WNDCLASSEXW WNDCLASSEX;

inline HINSTANCE GetModuleHandle(const wchar_t *) { return nullptr; }

unsigned short RegisterClassEx(const WNDCLASSEX * wc);
HWND  CreateWindow(const wchar_t * cls, const wchar_t * name, DWORD style, int x, int y, int w, int h,
                   HWND parent, void * menu, HINSTANCE inst, void * param);
BOOL  DestroyWindow(HWND hWnd);
INT_PTR GetWindowLongPtr(HWND hWnd, int index);
INT_PTR SetWindowLongPtr(HWND hWnd, int index, INT_PTR value);
inline LONG GetWindowLong(HWND h, int i) { return (LONG)GetWindowLongPtr(h, i); }
inline LONG SetWindowLong(HWND h, int i, LONG v) { return (LONG)SetWindowLongPtr(h, i, (INT_PTR)v); }

BOOL  PostMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL  PeekMessage(LPMSG msg, HWND hWnd, UINT filterMin, UINT filterMax, UINT removeFlag);
BOOL  GetMessage(LPMSG msg, HWND hWnd, UINT filterMin, UINT filterMax);
inline BOOL TranslateMessage(const MSG *) { return FALSE; }
LRESULT DispatchMessage(const MSG * msg);
LRESULT DefWindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Timers: stubbed for now (FileZilla uses WM_TIMER for keepalive/timeouts). SetTimer returns the id;
// no WM_TIMER is posted yet — wire a timer thread when keepalive/timeout behavior is needed.
typedef void (CALLBACK * TIMERPROC)(HWND, UINT, UINT_PTR, DWORD);
inline UINT_PTR SetTimer(HWND, UINT_PTR id, UINT, TIMERPROC) { return id ? id : 1; }
inline BOOL KillTimer(HWND, UINT_PTR) { return TRUE; }

// WSAAsyncSelect: register socket -> (hwnd, baseMsg, eventMask). lEvent==0 unregisters. A background
// select() thread posts baseMsg with wParam=socket, lParam=MAKELONG(FD_event, 0) on readiness.
int WSAAsyncSelect(SOCKET s, HWND hWnd, UINT msg, long lEvent);
// Async gethostbyname is not used on the native path (we resolve synchronously); stub to 0/handle.
typedef HANDLE HANDLE_WSAGHBN;
inline HANDLE WSAAsyncGetHostByName(HWND, UINT, const char *, char *, int) { return nullptr; }
#ifndef MAXGETHOSTSTRUCT
  #define MAXGETHOSTSTRUCT 1024
#endif

#endif
