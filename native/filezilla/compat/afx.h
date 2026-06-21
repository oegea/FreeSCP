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
public:
  CString() {}
  CString(const CString & o) : s_(o.s_) {}
  CString(const wchar_t * p) { if (p) s_ = p; }
  CString(const wchar_t * p, int n) { if (p && n > 0) s_.assign(p, (size_t)n); }
  CString(wchar_t c, int n = 1) { if (n > 0) s_.assign((size_t)n, c); }
  CString(const std::wstring & w) : s_(w) {}
  // narrow -> wide (Latin-1/ASCII); FileZilla passes char* literals in a few spots
  CString(const char * p) { if (p) while (*p) s_.push_back((wchar_t)(unsigned char)*p++); }

  CString & operator=(const CString & o) { s_ = o.s_; return *this; }
  CString & operator=(const wchar_t * p) { s_ = p ? p : L""; return *this; }
  CString & operator=(wchar_t c) { s_.assign(1, c); return *this; }

  operator LPCTSTR() const { return s_.c_str(); }
  const wchar_t * GetString() const { return s_.c_str(); }

  int GetLength() const { return (int)s_.size(); }
  bool IsEmpty() const { return s_.empty(); }
  void Empty() { s_.clear(); }
  wchar_t GetAt(int i) const { return (i >= 0 && i < (int)s_.size()) ? s_[i] : 0; }
  wchar_t operator[](int i) const { return GetAt(i); }
  void SetAt(int i, wchar_t c) { if (i >= 0 && i < (int)s_.size()) s_[i] = c; }

  CString & operator+=(const CString & o) { s_ += o.s_; return *this; }
  CString & operator+=(const wchar_t * p) { if (p) s_ += p; return *this; }
  CString & operator+=(wchar_t c) { s_ += c; return *this; }

  friend CString operator+(const CString & a, const CString & b) { CString r(a); r.s_ += b.s_; return r; }
  friend CString operator+(const CString & a, const wchar_t * b) { CString r(a); if (b) r.s_ += b; return r; }
  friend CString operator+(const wchar_t * a, const CString & b) { CString r(a); r.s_ += b.s_; return r; }
  friend CString operator+(const CString & a, wchar_t b) { CString r(a); r.s_ += b; return r; }

  bool operator==(const CString & o) const { return s_ == o.s_; }
  bool operator!=(const CString & o) const { return s_ != o.s_; }
  bool operator==(const wchar_t * p) const { return s_ == (p ? p : L""); }
  bool operator!=(const wchar_t * p) const { return s_ != (p ? p : L""); }
  bool operator<(const CString & o) const { return s_ < o.s_; }

  CString Left(int n) const { if (n < 0) n = 0; return CString(s_.substr(0, (size_t)n)); }
  CString Right(int n) const { if (n < 0) n = 0; if (n > (int)s_.size()) n = (int)s_.size(); return CString(s_.substr(s_.size() - (size_t)n)); }
  CString Mid(int first) const { if (first < 0) first = 0; if (first > (int)s_.size()) return CString(); return CString(s_.substr((size_t)first)); }
  CString Mid(int first, int n) const {
    if (first < 0) first = 0; if (n < 0) n = 0;
    if (first > (int)s_.size()) return CString();
    return CString(s_.substr((size_t)first, (size_t)n));
  }

  int Find(wchar_t c, int start = 0) const { auto p = s_.find(c, (size_t)(start < 0 ? 0 : start)); return p == std::wstring::npos ? -1 : (int)p; }
  int Find(const wchar_t * sub, int start = 0) const { if (!sub) return -1; auto p = s_.find(sub, (size_t)(start < 0 ? 0 : start)); return p == std::wstring::npos ? -1 : (int)p; }
  int ReverseFind(wchar_t c) const { auto p = s_.rfind(c); return p == std::wstring::npos ? -1 : (int)p; }

  void MakeLower() { for (auto & c : s_) c = (wchar_t)towlower2(c); }
  void MakeUpper() { for (auto & c : s_) c = (wchar_t)towupper2(c); }

  int Compare(const wchar_t * p) const { return s_.compare(p ? p : L""); }
  int CompareNoCase(const wchar_t * p) const {
    std::wstring a = s_, b = p ? p : L"";
    for (auto & c : a) c = towlower2(c); for (auto & c : b) c = towlower2(c);
    return a.compare(b);
  }

  void TrimLeft()  { size_t i = 0; while (i < s_.size() && iswsp(s_[i])) ++i; s_.erase(0, i); }
  void TrimRight() { size_t i = s_.size(); while (i > 0 && iswsp(s_[i - 1])) --i; s_.erase(i); }
  void TrimLeft(wchar_t c)  { size_t i = 0; while (i < s_.size() && s_[i] == c) ++i; s_.erase(0, i); }
  void TrimRight(wchar_t c) { size_t i = s_.size(); while (i > 0 && s_[i - 1] == c) --i; s_.erase(i); }

  int Replace(wchar_t from, wchar_t to) { int n = 0; for (auto & c : s_) if (c == from) { c = to; ++n; } return n; }
  int Replace(const wchar_t * from, const wchar_t * to) {
    if (!from || !*from) return 0; std::wstring f = from, t = to ? to : L""; int n = 0;
    size_t p = 0; while ((p = s_.find(f, p)) != std::wstring::npos) { s_.replace(p, f.size(), t); p += t.size(); ++n; }
    return n;
  }
  void Delete(int i, int n = 1) { if (i >= 0 && i < (int)s_.size() && n > 0) s_.erase((size_t)i, (size_t)n); }
  void Insert(int i, const wchar_t * p) { if (p && i >= 0 && i <= (int)s_.size()) s_.insert((size_t)i, p); }

  wchar_t * GetBuffer(int minLen) { if (minLen > (int)s_.size()) s_.resize((size_t)minLen); return &s_[0]; }
  void ReleaseBuffer(int newLen = -1) { if (newLen < 0) newLen = (int)wcslen2(s_.c_str()); s_.resize((size_t)newLen); }

  void Format(const wchar_t * fmt, ...) { va_list ap; va_start(ap, fmt); FormatV(fmt, ap); va_end(ap); }
  void FormatV(const wchar_t * fmt, va_list ap);
  BOOL LoadString(UINT) { s_.clear(); return FALSE; }   // resource strings not present in the port

  const std::wstring & str() const { return s_; }

private:
  std::wstring s_;
  static size_t wcslen2(const wchar_t * p) { size_t n = 0; if (p) while (p[n]) ++n; return n; }
  static bool iswsp(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; }
  static wchar_t towlower2(wchar_t c) { return (c >= L'A' && c <= L'Z') ? (wchar_t)(c + 32) : c; }
  static wchar_t towupper2(wchar_t c) { return (c >= L'a' && c <= L'z') ? (wchar_t)(c - 32) : c; }
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

//=== CFileException ========================================================
class CFileException : public CObject
{
public:
  LONG m_lOsError = 0;
  CString m_strFileName;
  CFileException(LONG e = 0, const CString & n = CString()) : m_lOsError(e), m_strFileName(n) {}
  static void PASCAL ThrowOsError(LONG err, const CString & name = CString()) { throw CFileException(err, name); }
};

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

class CTime
{
public:
  std::time_t m_time = 0;
  CTime() {}
  CTime(std::time_t t) : m_time(t) {}
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
};

#include "afxconv.h"

#endif // WINSCP_FZ_AFX_H
