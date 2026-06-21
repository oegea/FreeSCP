//---------------------------------------------------------------------------
// afxconv.h — MFC ANSI<->TCHAR conversion macros (USES_CONVERSION / A2T / T2A / ...).
// TCHAR == wchar_t (2-byte) in this build. MFC allocates per-call via alloca; we use a small
// rotating pool of thread_local buffers so several conversions in one expression don't clobber.
//---------------------------------------------------------------------------
#ifndef WINSCP_FZ_AFXCONV_H
#define WINSCP_FZ_AFXCONV_H

#include <string>

// Return non-const (MFC's A2T/T2A yield writable LPTSTR/LPSTR; some FileZilla code assigns to char*).
// Use std::u16string, NOT std::wstring: libc++ explicitly instantiates basic_string<wchar_t> with a
// 4-byte wchar_t in the dylib, so std::wstring under -fshort-wchar uses the wrong ABI and corrupts
// data (A2T("127.0.0.1") came back "1"). char16_t is consistent; wchar_t (2-byte) reinterprets.
inline wchar_t * fz_a2w(const char * a)
{
  static thread_local std::u16string pool[16];
  static thread_local int idx = 0;
  std::u16string & b = pool[idx = (idx + 1) & 15];
  b.clear(); if (a) while (*a) b.push_back((char16_t)(unsigned char)*a++);
  b.push_back(0); b.pop_back();   // ensure a null terminator follows the data
  return reinterpret_cast<wchar_t *>(&b[0]);
}

inline char * fz_w2a(const wchar_t * w)
{
  static thread_local std::string pool[16];
  static thread_local int idx = 0;
  std::string & b = pool[idx = (idx + 1) & 15];
  b.clear(); if (w) while (*w) { b.push_back((char)(*w & 0xFF)); ++w; }
  return &b[0];
}

#define USES_CONVERSION ((void)0)
#define A2W(a)   fz_a2w(a)
#define A2T(a)   fz_a2w(a)
#define A2CT(a)  fz_a2w(a)
#define W2A(w)   fz_w2a(w)
#define T2A(t)   fz_w2a(t)
#define T2CA(t)  fz_w2a(t)
#define T2W(t)   (t)
#define T2CW(t)  (t)
#define W2T(w)   (w)
#define W2CT(w)  (w)
#define T2OLE(t) (t)
#define CT2A(t)  fz_w2a(t)
#define CT2W(t)  (t)

#endif
