//---------------------------------------------------------------------------
// Format.cpp — Delphi System.SysUtils.Format (subset) + FmtLoadStr/LoadStr.
//
// Supports  %[index:][-][width][.prec]<type>  with types s d u x X f g e p %.
// Width/precision may be '*' (take next arg). For d/u/x precision => min digits (zero pad).
//---------------------------------------------------------------------------
#include "SysUtils.hpp"
#include <string>
#include <cstdio>

static std::string NarrowAscii(const UnicodeString & s)
{
  const std::u16string & w = s.raw();
  std::string r; r.reserve(w.size());
  for (char16_t c : w) r.push_back(static_cast<char>(c & 0xFF));
  return r;
}

static UnicodeString FormatImpl(const UnicodeString & Fmt, const TVarRec * Args, int Count)
{
  UnicodeString Result;
  int argi = 0;
  int n = Fmt.Length();
  int i = 1;  // 1-based
  auto nextArg = [&]() -> const TVarRec * {
    return (argi < Count) ? &Args[argi++] : nullptr;
  };
  while (i <= n)
  {
    wchar_t c = static_cast<wchar_t>(Fmt[i]);
    if (c != L'%') { Result += UnicodeString(c, 1); ++i; continue; }
    ++i;
    if (i <= n && Fmt[i] == L'%') { Result += UnicodeString(L"%"); ++i; continue; }

    // [index:]
    int save = i; long idx = -1; std::string num;
    while (i <= n && Fmt[i] >= L'0' && Fmt[i] <= L'9') num += static_cast<char>(Fmt[i++]);
    if (i <= n && Fmt[i] == L':' && !num.empty()) { idx = std::strtol(num.c_str(), nullptr, 10); ++i; }
    else i = save;
    if (idx >= 0) argi = static_cast<int>(idx);

    bool left = false, zero = false;
    while (i <= n && (Fmt[i] == L'-' || Fmt[i] == L'0'))
    { if (Fmt[i] == L'-') left = true; else zero = true; ++i; }
    // width
    int width = -1;
    if (i <= n && Fmt[i] == L'*') { const TVarRec * a = nextArg(); width = a ? static_cast<int>(a->VI) : 0; ++i; }
    else { std::string w; while (i <= n && Fmt[i] >= L'0' && Fmt[i] <= L'9') w += static_cast<char>(Fmt[i++]);
           if (!w.empty()) width = static_cast<int>(std::strtol(w.c_str(), nullptr, 10)); }
    // .precision
    int prec = -1;
    if (i <= n && Fmt[i] == L'.') { ++i;
      if (i <= n && Fmt[i] == L'*') { const TVarRec * a = nextArg(); prec = a ? static_cast<int>(a->VI) : 0; ++i; }
      else { std::string p; while (i <= n && Fmt[i] >= L'0' && Fmt[i] <= L'9') p += static_cast<char>(Fmt[i++]);
             prec = p.empty() ? 0 : static_cast<int>(std::strtol(p.c_str(), nullptr, 10)); } }
    if (i > n) break;
    wchar_t type = static_cast<wchar_t>(Fmt[i++]);

    UnicodeString piece;
    const TVarRec * a = nextArg();
    switch (type)
    {
      case L'd': case L'D':
      {
        long long v = a ? a->VI : 0;
        char buf[32]; std::snprintf(buf, sizeof(buf), "%lld", v);
        std::string s = buf; bool neg = (!s.empty() && s[0] == '-'); if (neg) s.erase(0, 1);
        while (prec > 0 && static_cast<int>(s.size()) < prec) s.insert(s.begin(), '0');
        if (neg) s.insert(s.begin(), '-');
        piece = UnicodeString(s.c_str()); break;
      }
      case L'u': case L'U':
      {
        unsigned long long v = a ? static_cast<unsigned long long>(a->VI) : 0;
        char buf[32]; std::snprintf(buf, sizeof(buf), "%llu", v);
        std::string s = buf;
        while (prec > 0 && static_cast<int>(s.size()) < prec) s.insert(s.begin(), '0');
        piece = UnicodeString(s.c_str()); break;
      }
      case L'x': case L'X':
      {
        unsigned long long v = a ? static_cast<unsigned long long>(a->VI) : 0;
        char buf[32]; std::snprintf(buf, sizeof(buf), (type == L'x') ? "%llx" : "%llX", v);
        std::string s = buf;
        while (prec > 0 && static_cast<int>(s.size()) < prec) s.insert(s.begin(), '0');
        piece = UnicodeString(s.c_str()); break;
      }
      case L'f': case L'g': case L'e':
      {
        double v = a ? (a->VType == TVarRec::vtFloat ? a->VF : static_cast<double>(a->VI)) : 0.0;
        char fmt[16]; std::snprintf(fmt, sizeof(fmt), "%%.%d%c", prec < 0 ? 6 : prec, static_cast<char>(type));
        char buf[64]; std::snprintf(buf, sizeof(buf), fmt, v);
        piece = UnicodeString(buf); break;
      }
      case L'p': case L'P':
      {
        char buf[32]; std::snprintf(buf, sizeof(buf), "%p", a ? a->VP : nullptr);
        piece = UnicodeString(buf); break;
      }
      case L's': case L'S':
      default:
      {
        piece = a ? a->VS : UnicodeString();
        if (prec >= 0 && piece.Length() > prec) piece = piece.SubString(1, prec);
        break;
      }
    }
    // width padding
    if (width > piece.Length())
    {
      int padlen = width - piece.Length();
      if (zero && !left)
        piece = UnicodeString(L'0', padlen) + piece;  // zero-pad numeric to width
      else
      {
        UnicodeString pad(L' ', padlen);
        piece = left ? (piece + pad) : (pad + piece);
      }
    }
    Result += piece;
  }
  return Result;
}

UnicodeString __fastcall Format(const UnicodeString & Fmt, const TVarRecArray & Args)
{
  return FormatImpl(Fmt, Args.Items.empty() ? nullptr : Args.Items.data(),
                    static_cast<int>(Args.Items.size()));
}
UnicodeString __fastcall Format(const UnicodeString & Fmt, const TVarRec * Args, int Args_Size)
{
  return FormatImpl(Fmt, Args, Args_Size);
}

// Resource strings: placeholder until source/resource tables are loaded.
UnicodeString __fastcall LoadStr(int Ident) { return UnicodeString(L"str#") + UnicodeString(Ident); }

UnicodeString __fastcall FmtLoadStr(int Ident, const TVarRecArray & Args)
{
  return Format(LoadStr(Ident), Args);
}
