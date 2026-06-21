//---------------------------------------------------------------------------
// afx.h — minimal MFC shim for the FileZilla FTP backend (native port).
//
// FileZilla is written against MFC + Win32. This header (placed AHEAD of libs/mfc on the include
// path) replaces <afx.h> with just what FileZilla uses, over std + a 2-byte-wchar CString. The
// FileZilla lib is compiled with -fshort-wchar so its wchar_t/CString interop with the engine's
// UnicodeString. Do NOT pull rtlcompat here (keep the lib self-contained); FileZillaIntf.cpp does
// the CString<->UnicodeString bridging at the boundary.
//---------------------------------------------------------------------------
#ifndef WINSCP_FZ_AFX_H
#define WINSCP_FZ_AFX_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <climits>
#include <string>
#include <atomic>
#include <mutex>
#include <cwctype>
#include <cctype>
#include <pthread.h>
#include <unistd.h>

//=== Win32 scalar types ====================================================
typedef unsigned char       BYTE;
typedef unsigned short      WORD;
typedef unsigned int        UINT;
typedef unsigned long       DWORD;
typedef int                 BOOL;
typedef long                LONG;
typedef unsigned long       ULONG;
typedef long long           LONGLONG;
typedef unsigned long long  ULONGLONG;
typedef wchar_t             TCHAR;
typedef wchar_t             WCHAR;
typedef char                CHAR;
typedef const char *        LPCSTR;
typedef char *              LPSTR;
typedef const wchar_t *     LPCWSTR;
typedef wchar_t *           LPWSTR;
typedef const wchar_t *     LPCTSTR;
typedef wchar_t *           LPTSTR;
typedef void *              LPVOID;
typedef const void *        LPCVOID;
typedef void *              HANDLE;
typedef void *              HWND;
typedef void *              HINSTANCE;
typedef std::intptr_t       INT_PTR;
typedef std::uintptr_t      UINT_PTR;
typedef std::int64_t        __int64alias;
typedef std::uintptr_t      WPARAM;
typedef std::intptr_t       LPARAM;
typedef std::intptr_t       LRESULT;
typedef UINT                WedgeMsg;

#ifndef _WIN32
  #ifndef __int64
    #define __int64 long long
  #endif
#endif

#ifndef TRUE
  #define TRUE 1
  #define FALSE 0
#endif
#ifndef NULL
  #define NULL 0
#endif
#define _T(x)  L##x
#define TEXT(x) L##x
#define AFXAPI
#define PASCAL
#define FASTCALL
#define WINAPI
#define CALLBACK
#ifndef MAX_PATH
  #define MAX_PATH 1024
#endif

#define LOWORD(l)  ((WORD)((DWORD_PTR)(l) & 0xffff))
#define HIWORD(l)  ((WORD)(((DWORD_PTR)(l) >> 16) & 0xffff))
typedef std::uintptr_t DWORD_PTR;
#define AFX_COMDAT
#define AFX_DATADEF

//=== CRITICAL_SECTION (recursive mutex) ====================================
struct CRITICAL_SECTION { std::recursive_mutex m; };
inline void InitializeCriticalSection(CRITICAL_SECTION *) {}
inline void DeleteCriticalSection(CRITICAL_SECTION *) {}
inline void EnterCriticalSection(CRITICAL_SECTION * c) { if (c) c->m.lock(); }
inline void LeaveCriticalSection(CRITICAL_SECTION * c) { if (c) c->m.unlock(); }

//=== _sntprintf (wide, 2-byte; impl in afx_format.cpp) =====================
int fz_sntprintf(wchar_t * buf, size_t n, const wchar_t * fmt, ...);
#define _sntprintf fz_sntprintf
#define _vsntprintf(b, n, f, a) fz_vsntprintf(b, n, f, a)
int fz_vsntprintf(wchar_t * buf, size_t n, const wchar_t * fmt, va_list ap);

//=== Debug / assert macros (no-ops in the port) ============================
#ifndef ASSERT
  #define ASSERT(x)        ((void)0)
#endif
#define ASSERT_VALID(x)    ((void)0)
#define VERIFY(x)          ((void)(x))
#define TRACE(...)         ((void)0)
#define DebugAssert(x)     ((void)0)
#define DebugFail()        ((void)0)
#define DebugCheck(x)      ((void)(x))
#define AfxIsValidString(p, ...) (true)
#define AfxIsValidAddress(p, ...) (true)

//=== Interlocked (atomics) =================================================
inline LONG InterlockedIncrement(LONG * p) { return ++*reinterpret_cast<std::atomic<LONG>*>(p); }
inline LONG InterlockedDecrement(LONG * p) { return --*reinterpret_cast<std::atomic<LONG>*>(p); }

//=== narrow case helpers (stdafx.h does #define _strlwr strlwr) =============
inline char * strlwr(char * s) { for (char * p = s; p && *p; ++p) *p = (char)tolower((unsigned char)*p); return s; }
inline char * strupr(char * s) { for (char * p = s; p && *p; ++p) *p = (char)toupper((unsigned char)*p); return s; }

//=== TCHAR char-class + conversions (MFC _t* names) ========================
inline int _istalpha(wchar_t c) { return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z'); }
inline int _istdigit(wchar_t c) { return c >= L'0' && c <= L'9'; }
inline int _istalnum(wchar_t c) { return _istalpha(c) || _istdigit(c); }
inline int _istspace(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; }
inline int _ttoi(const wchar_t * s) { int v = 0, sign = 1; if (!s) return 0; while (_istspace(*s)) ++s; if (*s == L'-') { sign = -1; ++s; } else if (*s == L'+') ++s; while (_istdigit(*s)) v = v * 10 + (*s++ - L'0'); return v * sign; }
inline long long _ttoi64(const wchar_t * s) { long long v = 0, sign = 1; if (!s) return 0; while (_istspace(*s)) ++s; if (*s == L'-') { sign = -1; ++s; } else if (*s == L'+') ++s; while (_istdigit(*s)) v = v * 10 + (*s++ - L'0'); return v * sign; }
inline int strnicmp(const char * a, const char * b, size_t n) { return strncasecmp(a, b, n); }
// TCHAR string fns (TCHAR == 2-byte wchar; the wcs* names resolve to WcsCompat shims).
#define _tcslen   wcslen
#define _tcscpy   wcscpy
#define _tcsncpy  wcsncpy
#define _tcscmp   wcscmp
#define _tcsncmp  wcsncmp
#define _tcschr   wcschr
#define _tcsrchr  wcsrchr
#define _tcsstr   wcsstr
#define _tcsdup   wcsdup
#define _snprintf snprintf
#define _tcsicmp(a, b)  fz_tcsicmp(a, b)
inline int fz_tcsicmp(const wchar_t * a, const wchar_t * b)
{ for (; *a && *b; ++a, ++b) { wchar_t ca = (*a >= L'A' && *a <= L'Z') ? *a + 32 : *a, cb = (*b >= L'A' && *b <= L'Z') ? *b + 32 : *b; if (ca != cb) return ca - cb; } return *a - *b; }
inline DWORD GetCurrentThreadId() { return (DWORD)(uintptr_t)pthread_self(); }
inline DWORD GetCurrentProcessId() { return (DWORD)getpid(); }

//=== misc Win32 stubs FileZilla references =================================
int fz_stprintf(wchar_t * buf, const wchar_t * fmt, ...);   // impl in afx_format.cpp (4K buffer)
#define _stprintf fz_stprintf

union LARGE_INTEGER { struct { DWORD LowPart; LONG HighPart; }; long long QuadPart; };
typedef union LARGE_INTEGER * PLARGE_INTEGER;
inline BOOL QueryPerformanceCounter(LARGE_INTEGER * p)
{ if (p) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); p->QuadPart = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec; } return TRUE; }
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER * p) { if (p) p->QuadPart = 1000000000LL; return TRUE; }

#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x0100
#define FORMAT_MESSAGE_FROM_SYSTEM     0x1000
#define FORMAT_MESSAGE_IGNORE_INSERTS  0x0200
#define FORMAT_MESSAGE_ARGUMENT_ARRAY  0x2000
inline DWORD FormatMessage(DWORD, const void *, DWORD, DWORD, wchar_t * buf, DWORD, va_list *)
{ if (buf) buf[0] = 0; return 0; }
inline void LocalFree(void * p) { free(p); }
inline void GlobalFree(void * p) { free(p); }
inline BOOL IsBadWritePtr(void *, UINT_PTR) { return FALSE; }

#define MB_OK              0x0000
#define MB_ICONEXCLAMATION 0x0030
#define MB_ICONERROR       0x0010
inline int MessageBox(HWND, const wchar_t *, const wchar_t *, UINT) { return 0; }

inline HANDLE GlobalAlloc(UINT, size_t n) { return (HANDLE)calloc(1, n); }
inline void * GlobalLock(HANDLE h) { return h; }
inline BOOL GlobalUnlock(HANDLE) { return TRUE; }

//=== file enumeration (WIN32_FIND_DATA) — POSIX-backed, single-file ========
struct FZ_FILETIME { DWORD dwLowDateTime, dwHighDateTime; };
#ifndef FILE_ATTRIBUTE_NORMAL
  #define FILE_ATTRIBUTE_NORMAL    0x80
#endif
#ifndef FILE_ATTRIBUTE_DIRECTORY
  #define FILE_ATTRIBUTE_DIRECTORY 0x10
#endif
#ifndef INVALID_HANDLE_VALUE
  #define INVALID_HANDLE_VALUE     ((HANDLE)(intptr_t)-1)
#endif
struct WIN32_FIND_DATAW {
  DWORD dwFileAttributes;
  FZ_FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
  DWORD nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1;
  wchar_t cFileName[MAX_PATH]; wchar_t cAlternateFileName[16];
};
typedef WIN32_FIND_DATAW WIN32_FIND_DATA;
HANDLE FindFirstFile(const wchar_t * name, WIN32_FIND_DATAW * data);   // impl in afx_format.cpp
inline BOOL FindNextFile(HANDLE, WIN32_FIND_DATAW *) { return FALSE; }
inline BOOL FindClose(HANDLE h) { (void)h; return TRUE; }
// GetFileAttributes is declared by rtlcompat (SysExtra.h) — don't redeclare here.
#ifndef CP_UTF8
  #define CP_UTF8 65001
  #define CP_ACP  0
#endif

//=== MultiByteToWideChar / WideCharToMultiByte (UTF-8 <-> UTF-16) ==========
// Minimal: codepage ignored except that UTF-8 is decoded/encoded; dst==NULL or dstLen==0 returns
// the required length. srcLen==-1 means NUL-terminated (the returned/written count includes NUL).
inline int MultiByteToWideChar(UINT, DWORD, const char * src, int srcLen, wchar_t * dst, int dstLen)
{
  std::u16string out; const unsigned char * p = (const unsigned char *)src;   // u16: std::wstring is 4-byte-ABI in libc++
  int rem = srcLen; bool nul = (srcLen < 0);
  while (nul ? (*p != 0) : (rem > 0))
  {
    unsigned cp = *p; int adv = 1;
    if (cp < 0x80) {}
    else if ((cp >> 5) == 0x6 && (!nul ? rem >= 2 : p[1])) { cp = ((cp & 0x1F) << 6) | (p[1] & 0x3F); adv = 2; }
    else if ((cp >> 4) == 0xE) { cp = ((cp & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); adv = 3; }
    else if ((cp >> 3) == 0x1E) { cp = ((cp & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); adv = 4; }
    if (cp >= 0x10000) { cp -= 0x10000; out.push_back((char16_t)(0xD800 + (cp >> 10))); out.push_back((char16_t)(0xDC00 + (cp & 0x3FF))); }
    else out.push_back((char16_t)cp);
    p += adv; if (!nul) rem -= adv;
  }
  if (nul) out.push_back(0);
  int need = (int)out.size();
  if (dst && dstLen > 0) { int c = need < dstLen ? need : dstLen; for (int i = 0; i < c; ++i) dst[i] = out[i]; return c; }
  return need;
}
inline int WideCharToMultiByte(UINT, DWORD, const wchar_t * src, int srcLen, char * dst, int dstLen, const char *, BOOL *)
{
  std::string out; int rem = srcLen; bool nul = (srcLen < 0);
  for (const wchar_t * w = src; nul ? (*w != 0) : (rem > 0); ++w, --rem)
  {
    unsigned cp = (unsigned short)*w;
    if (cp >= 0xD800 && cp <= 0xDBFF) { unsigned lo = (unsigned short)w[1]; cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); ++w; if (!nul) --rem; }
    if (cp < 0x80) out.push_back((char)cp);
    else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) { out.push_back((char)(0xE0 | (cp >> 12))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
    else { out.push_back((char)(0xF0 | (cp >> 18))); out.push_back((char)(0x80 | ((cp >> 12) & 0x3F))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
  }
  if (nul) out.push_back('\0');
  int need = (int)out.size();
  if (dst && dstLen > 0) { int c = need < dstLen ? need : dstLen; for (int i = 0; i < c; ++i) dst[i] = out[i]; return c; }
  return need;
}

//=== CObject ===============================================================
class CObject
{
public:
  virtual ~CObject() {}
};

//=== CString (wide, 2-byte wchar) ==========================================
// std::wstring-backed; MFC-ish API limited to what FileZilla uses (see FTP-SCOPE / grep).
class CString
{
  // Backed by std::u16string, NOT std::wstring: libc++'s char_traits<wchar_t> assumes a 4-byte
  // wchar_t and corrupts strings under -fshort-wchar (e.g. a host "127.0.0.1" came back as "1").
  // char16_t is reliably 2-byte and its char_traits are correct; wchar_t (2-byte here) is
  // reinterpreted to/from char16_t at the API boundary. Mutable buffer mirrors s_ for LPTSTR returns.
public:
  CString() {}
  CString(const CString & o) : s_(o.s_) {}
  CString(const wchar_t * p) { s_ = fromW(p); }
  CString(const wchar_t * p, int n) { if (p && n > 0) s_.assign(reinterpret_cast<const char16_t *>(p), (size_t)n); }
  CString(wchar_t c, int n = 1) { if (n > 0) s_.assign((size_t)n, (char16_t)c); }
  CString(const std::u16string & w) : s_(w) {}
  // narrow -> wide (Latin-1/ASCII); FileZilla passes char* literals in a few spots
  CString(const char * p) { if (p) while (*p) s_.push_back((char16_t)(unsigned char)*p++); }
  CString(const char * p, int n) { if (p && n > 0) for (int i = 0; i < n; ++i) s_.push_back((char16_t)(unsigned char)p[i]); }

  CString & operator=(const CString & o) { s_ = o.s_; return *this; }
  CString & operator=(const wchar_t * p) { s_ = fromW(p); return *this; }
  CString & operator=(wchar_t c) { s_.assign(1, (char16_t)c); return *this; }

  operator LPCTSTR() const { return reinterpret_cast<const wchar_t *>(s_.c_str()); }
  const wchar_t * GetString() const { return reinterpret_cast<const wchar_t *>(s_.c_str()); }

  int GetLength() const { return (int)s_.size(); }
  bool IsEmpty() const { return s_.empty(); }
  void Empty() { s_.clear(); }
  wchar_t GetAt(int i) const { return (i >= 0 && i < (int)s_.size()) ? (wchar_t)s_[i] : 0; }
  wchar_t operator[](int i) const { return GetAt(i); }
  void SetAt(int i, wchar_t c) { if (i >= 0 && i < (int)s_.size()) s_[i] = (char16_t)c; }

  CString & operator+=(const CString & o) { s_ += o.s_; return *this; }
  CString & operator+=(const wchar_t * p) { s_ += fromW(p); return *this; }
  CString & operator+=(wchar_t c) { s_ += (char16_t)c; return *this; }

  friend CString operator+(const CString & a, const CString & b) { CString r(a); r.s_ += b.s_; return r; }
  friend CString operator+(const CString & a, const wchar_t * b) { CString r(a); r.s_ += fromW(b); return r; }
  friend CString operator+(const wchar_t * a, const CString & b) { CString r(a); r.s_ += b.s_; return r; }
  friend CString operator+(const CString & a, wchar_t b) { CString r(a); r.s_ += (char16_t)b; return r; }

  bool operator==(const CString & o) const { return s_ == o.s_; }
  bool operator!=(const CString & o) const { return s_ != o.s_; }
  bool operator==(const wchar_t * p) const { return s_ == fromW(p); }
  bool operator!=(const wchar_t * p) const { return s_ != fromW(p); }
  bool operator<(const CString & o) const { return s_ < o.s_; }

  CString Left(int n) const { if (n < 0) n = 0; return CString(s_.substr(0, (size_t)n)); }
  CString Right(int n) const { if (n < 0) n = 0; if (n > (int)s_.size()) n = (int)s_.size(); return CString(s_.substr(s_.size() - (size_t)n)); }
  CString Mid(int first) const { if (first < 0) first = 0; if (first > (int)s_.size()) return CString(); return CString(s_.substr((size_t)first)); }
  CString Mid(int first, int n) const {
    if (first < 0) first = 0; if (n < 0) n = 0;
    if (first > (int)s_.size()) return CString();
    return CString(s_.substr((size_t)first, (size_t)n));
  }

  int Find(wchar_t c, int start = 0) const { auto p = s_.find((char16_t)c, (size_t)(start < 0 ? 0 : start)); return p == npos() ? -1 : (int)p; }
  int Find(const wchar_t * sub, int start = 0) const { if (!sub) return -1; auto p = s_.find(fromW(sub), (size_t)(start < 0 ? 0 : start)); return p == npos() ? -1 : (int)p; }
  int ReverseFind(wchar_t c) const { auto p = s_.rfind((char16_t)c); return p == npos() ? -1 : (int)p; }
  int FindOneOf(const wchar_t * set) const { if (!set) return -1; auto p = s_.find_first_of(fromW(set)); return p == npos() ? -1 : (int)p; }

  void MakeLower() { for (auto & c : s_) c = towlower2(c); }
  void MakeUpper() { for (auto & c : s_) c = towupper2(c); }

  int Compare(const wchar_t * p) const { return s_.compare(fromW(p)); }
  int CompareNoCase(const wchar_t * p) const {
    std::u16string a = s_, b = fromW(p);
    for (auto & c : a) c = towlower2(c); for (auto & c : b) c = towlower2(c);
    return a.compare(b);
  }

  void TrimLeft()  { size_t i = 0; while (i < s_.size() && iswsp(s_[i])) ++i; s_.erase(0, i); }
  void TrimRight() { size_t i = s_.size(); while (i > 0 && iswsp(s_[i - 1])) --i; s_.erase(i); }
  void TrimLeft(wchar_t c)  { size_t i = 0; while (i < s_.size() && s_[i] == (char16_t)c) ++i; s_.erase(0, i); }
  void TrimRight(wchar_t c) { size_t i = s_.size(); while (i > 0 && s_[i - 1] == (char16_t)c) --i; s_.erase(i); }
  // MFC set-trim: remove leading/trailing chars that appear in the target set.
  void TrimLeft(const wchar_t * set)  { if (!set) return; std::u16string t = fromW(set); size_t i = 0; while (i < s_.size() && t.find(s_[i]) != npos()) ++i; s_.erase(0, i); }
  void TrimRight(const wchar_t * set) { if (!set) return; std::u16string t = fromW(set); size_t i = s_.size(); while (i > 0 && t.find(s_[i - 1]) != npos()) --i; s_.erase(i); }

  int Replace(wchar_t from, wchar_t to) { int n = 0; for (auto & c : s_) if (c == (char16_t)from) { c = (char16_t)to; ++n; } return n; }
  int Replace(const wchar_t * from, const wchar_t * to) {
    if (!from || !*from) return 0; std::u16string f = fromW(from), t = fromW(to); int n = 0;
    size_t p = 0; while ((p = s_.find(f, p)) != npos()) { s_.replace(p, f.size(), t); p += t.size(); ++n; }
    return n;
  }
  void Delete(int i, int n = 1) { if (i >= 0 && i < (int)s_.size() && n > 0) s_.erase((size_t)i, (size_t)n); }
  void Insert(int i, const wchar_t * p) { if (p && i >= 0 && i <= (int)s_.size()) s_.insert((size_t)i, fromW(p)); }

  wchar_t * GetBuffer(int minLen) { if (minLen > (int)s_.size()) s_.resize((size_t)minLen); return reinterpret_cast<wchar_t *>(&s_[0]); }
  void ReleaseBuffer(int newLen = -1) { if (newLen < 0) { newLen = 0; while (newLen < (int)s_.size() && s_[newLen]) ++newLen; } s_.resize((size_t)newLen); }

  void Format(const wchar_t * fmt, ...) { va_list ap; va_start(ap, fmt); FormatV(fmt, ap); va_end(ap); }
  // MFC Format(UINT nFormatID, ...) loads the resource then formats; resources absent -> empty fmt.
  void Format(unsigned id, ...) { (void)id; s_.clear(); }
  void FormatV(const wchar_t * fmt, va_list ap);
  BOOL LoadString(UINT) { s_.clear(); return FALSE; }   // resource strings not present in the port

  const std::u16string & str() const { return s_; }

private:
  std::u16string s_;
  static size_t npos() { return std::u16string::npos; }
  // Build a u16string from a 2-byte wchar_t* WITHOUT char_traits<wchar_t> (which assumes 4-byte).
  static std::u16string fromW(const wchar_t * p) { std::u16string r; if (p) while (*p) r.push_back((char16_t)*p++); return r; }
  static bool iswsp(char16_t c) { return c == u' ' || c == u'\t' || c == u'\r' || c == u'\n'; }
  static char16_t towlower2(char16_t c) { return (c >= u'A' && c <= u'Z') ? (char16_t)(c + 32) : c; }
  static char16_t towupper2(char16_t c) { return (c >= u'a' && c <= u'z') ? (char16_t)(c - 32) : c; }
};

inline bool operator==(const CString & s1, const char * s2) { return s1 == CString(s2); }

typedef CString CStringW;

//=== Win32 file shims used by CFile / CFileFix =============================
#include <ctime>
inline DWORD GetLastError() { return 0; }
inline BOOL  ReadFile(HANDLE h, void * buf, DWORD n, DWORD * read, void *)
{ size_t r = h ? fread(buf, 1, n, (FILE *)h) : 0; if (read) *read = (DWORD)r; return TRUE; }
inline BOOL  WriteFile(HANDLE h, const void * buf, DWORD n, DWORD * wrote, void *)
{ size_t w = h ? fwrite(buf, 1, n, (FILE *)h) : 0; if (wrote) *wrote = (DWORD)w; return TRUE; }

//=== CException / CFileException ============================================
class CException : public CObject
{
public:
  virtual BOOL GetErrorMessage(wchar_t * buf, UINT n, UINT * help = nullptr) const
  { if (help) *help = 0; if (buf && n) buf[0] = 0; return FALSE; }
  void Delete() { delete this; }
};
class CFileException : public CException
{
public:
  LONG m_lOsError = 0;
  CString m_strFileName;
  CFileException(LONG e = 0, const CString & n = CString()) : m_lOsError(e), m_strFileName(n) {}
  static void PASCAL ThrowOsError(LONG err, const CString & name = CString()) { throw CFileException(err, name); }
  BOOL GetErrorMessage(wchar_t * buf, UINT n, UINT * help = nullptr) const override
  { if (help) *help = 0; const wchar_t * s = (const wchar_t *)m_strFileName; UINT i = 0; if (buf) { for (; s && s[i] && i + 1 < n; ++i) buf[i] = s[i]; buf[i] = 0; } return TRUE; }
};
typedef CException * LPCEXCEPTION;

//=== CFileStatus ===========================================================
struct CFileStatus
{
  __int64 m_size = 0;
  std::time_t m_mtime = 0;
  CString m_szFullName;
};

//=== CFile (POSIX FILE*-backed) ============================================
class CFile : public CObject
{
public:
  enum OpenFlags {
    modeRead = 0x0000, modeWrite = 0x0001, modeReadWrite = 0x0002, modeCreate = 0x1000,
    modeNoTruncate = 0x2000, shareDenyNone = 0x0040, shareDenyWrite = 0x0020, shareDenyRead = 0x0030,
    typeBinary = 0x8000, typeText = 0x4000,
  };
  enum SeekPosition { begin = 0, current = 1, end = 2 };
  static const HANDLE hFileNull;

  HANDLE  m_hFile = nullptr;
  CString m_strFileName;

  CFile() {}
  virtual ~CFile() { Close(); }

  BOOL Open(LPCTSTR name, UINT flags, CFileException * ex = nullptr)
  {
    m_strFileName = name;
    const char * mode;
    if (flags & modeCreate) mode = (flags & modeNoTruncate) ? "rb+" : "wb";
    else if (flags & (modeWrite | modeReadWrite)) mode = "rb+";
    else mode = "rb";
    // narrow the (wide) path
    std::string p; for (const wchar_t * w = name; w && *w; ++w) p.push_back((char)*w);
    FILE * f = fopen(p.c_str(), mode);
    if (!f && (flags & modeCreate)) f = fopen(p.c_str(), "wb+");
    if (!f) { if (ex) { ex->m_lOsError = errno; ex->m_strFileName = name; } return FALSE; }
    m_hFile = f;
    return TRUE;
  }
  UINT Read(void * buf, UINT n) { DWORD r = 0; ReadFile(m_hFile, buf, n, &r, nullptr); return r; }
  void Write(const void * buf, UINT n) { DWORD w = 0; WriteFile(m_hFile, buf, n, &w, nullptr); }
  void Close() { if (m_hFile) { fclose((FILE *)m_hFile); m_hFile = nullptr; } }
  __int64 Seek(__int64 off, UINT from) { if (m_hFile) fseeko((FILE *)m_hFile, (off_t)off, (int)from); return GetPosition(); }
  void SeekToBegin() { Seek(0, begin); }
  __int64 SeekToEnd() { return Seek(0, end); }
  __int64 GetPosition() const { return m_hFile ? (long long)ftello((FILE *)m_hFile) : 0; }
  __int64 GetLength() const {
    if (!m_hFile) return 0; off_t cur = ftello((FILE *)m_hFile);
    fseeko((FILE *)m_hFile, 0, SEEK_END); off_t len = ftello((FILE *)m_hFile);
    fseeko((FILE *)m_hFile, cur, SEEK_SET); return (long long)len;
  }
  void Flush() { if (m_hFile) fflush((FILE *)m_hFile); }
  static BOOL PASCAL Rename(LPCTSTR from, LPCTSTR to) {
    std::string a, b; for (const wchar_t * w = from; w && *w; ++w) a.push_back((char)*w);
    for (const wchar_t * w = to; w && *w; ++w) b.push_back((char)*w);
    return ::rename(a.c_str(), b.c_str()) == 0;
  }
  static void PASCAL Remove(LPCTSTR name) {
    std::string a; for (const wchar_t * w = name; w && *w; ++w) a.push_back((char)*w); ::remove(a.c_str());
  }
  static BOOL PASCAL GetStatus(LPCTSTR, CFileStatus &) { return FALSE; }
};

//=== CTimeSpan / CTime =====================================================
class CTimeSpan
{
public:
  __int64 m_span = 0;
  CTimeSpan() {}
  CTimeSpan(__int64 s) : m_span(s) {}
  __int64 GetTotalSeconds() const { return m_span; }
};

struct FZ_FILETIME;
class CTime
{
public:
  std::time_t m_time = 0;
  CTime() {}
  CTime(std::time_t t) : m_time(t) {}
  CTime(const FZ_FILETIME & ft);   // defined after FZ_FILETIME below
  CTime(int y, int mon, int d, int h, int mi, int s) {
    std::tm tmv; std::memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = y - 1900; tmv.tm_mon = mon - 1; tmv.tm_mday = d;
    tmv.tm_hour = h; tmv.tm_min = mi; tmv.tm_sec = s; tmv.tm_isdst = -1;
    m_time = ::mktime(&tmv);
  }
  static CTime GetCurrentTime() { return CTime(::time(nullptr)); }
  std::time_t GetTime() const { return m_time; }
  int GetYear() const  { std::tm t; localtime_r(&m_time, &t); return t.tm_year + 1900; }
  int GetMonth() const { std::tm t; localtime_r(&m_time, &t); return t.tm_mon + 1; }
  int GetDay() const   { std::tm t; localtime_r(&m_time, &t); return t.tm_mday; }
  int GetHour() const  { std::tm t; localtime_r(&m_time, &t); return t.tm_hour; }
  int GetMinute() const{ std::tm t; localtime_r(&m_time, &t); return t.tm_min; }
  int GetSecond() const{ std::tm t; localtime_r(&m_time, &t); return t.tm_sec; }
  CTimeSpan operator-(const CTime & o) const { return CTimeSpan(m_time - o.m_time); }
  bool operator==(const CTime & o) const { return m_time == o.m_time; }
  bool operator!=(const CTime & o) const { return m_time != o.m_time; }
  bool operator<(const CTime & o) const { return m_time < o.m_time; }
  bool operator==(int v) const { return m_time == (std::time_t)v; }   // sentinel checks (== -1)
  bool operator!=(int v) const { return m_time != (std::time_t)v; }
};
// FILETIME (100ns ticks since 1601) -> CTime (unix time_t).
inline CTime::CTime(const FZ_FILETIME & ft)
{
  unsigned long long ticks = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
  m_time = (std::time_t)(ticks / 10000000ULL) - 11644473600LL;
}
#include <sys/stat.h>
inline BOOL CreateDirectory(const wchar_t * name, void *)
{ std::string p; for (const wchar_t * w = name; w && *w; ++w) p.push_back((char)*w); return ::mkdir(p.c_str(), 0755) == 0; }

#include "afxconv.h"
#include "winmsg.h"

#endif // WINSCP_FZ_AFX_H
