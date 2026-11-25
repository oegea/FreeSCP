//---------------------------------------------------------------------------
// editorlogic.h — pure logic of the internal editor (no Qt, no engine), unit-tested: what a file's
// bytes are (encoding, line endings, text or not), so that saving gives back the same kind of file.
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_EDITORLOGIC_H
#define WINSCP_UIQT_EDITORLOGIC_H

#include <cstddef>
#include <string>

namespace editorlogic {

enum class Encoding { Utf8, Utf8Bom, Utf16LE, Utf16BE, Latin1 };
enum class Eol { Lf, CrLf, Cr };

struct FileKind
{
  Encoding encoding = Encoding::Utf8;
  Eol eol = Eol::Lf;
  bool binary = false;        // not a text file: the editor must not open (and rewrite) it
  bool mixedEol = false;      // more than one kind of line ending: saving writes `eol` everywhere
  size_t bomSize = 0;         // bytes of byte-order mark at the start
};

inline const char * encodingName(Encoding e)
{
  switch (e)
  {
    case Encoding::Utf8: return "UTF-8";
    case Encoding::Utf8Bom: return "UTF-8 with BOM";
    case Encoding::Utf16LE: return "UTF-16 LE";
    case Encoding::Utf16BE: return "UTF-16 BE";
    case Encoding::Latin1: return "ISO-8859-1";
  }
  return "";
}
inline const char * eolName(Eol e) { return e == Eol::CrLf ? "CR/LF" : e == Eol::Cr ? "CR" : "LF"; }

// Well-formed UTF-8: no stray or missing continuation bytes, no overlong forms, no surrogates,
// nothing above U+10FFFF.
inline bool validUtf8(const std::string & s)
{
  const size_t n = s.size();
  for (size_t i = 0; i < n; )
  {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) { ++i; continue; }
    size_t len; unsigned int cp;
    if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
    else return false;
    if (i + len > n) return false;
    for (size_t k = 1; k < len; ++k)
    {
      const unsigned char cc = static_cast<unsigned char>(s[i + k]);
      if ((cc & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (cc & 0x3F);
    }
    if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000)) return false;
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    i += len;
  }
  return true;
}

// The line ending used most ("\r\n" counted once, not as a CR and an LF); LF for a file without any.
// `mixed` (optional): the file has more than one kind.
inline Eol dominantEol(const std::string & s, size_t from = 0, size_t unit = 1, bool bigEndian = false, bool * mixed = nullptr)
{
  size_t lf = 0, crlf = 0, cr = 0;
  auto at = [&](size_t i) -> unsigned int {       // code unit i (bytes for 8-bit text, 16-bit units otherwise)
    if (unit == 1) return static_cast<unsigned char>(s[i]);
    const unsigned int a = static_cast<unsigned char>(s[i]), b = static_cast<unsigned char>(s[i + 1]);
    return bigEndian ? ((a << 8) | b) : ((b << 8) | a);
  };
  for (size_t i = from; i + unit <= s.size(); i += unit)
  {
    const unsigned int c = at(i);
    if (c == '\r')
    {
      if (i + 2 * unit <= s.size() && at(i + unit) == '\n') { ++crlf; i += unit; }
      else ++cr;
    }
    else if (c == '\n') ++lf;
  }
  if (mixed) *mixed = ((lf > 0) + (crlf > 0) + (cr > 0)) > 1;
  if (crlf > lf && crlf >= cr) return Eol::CrLf;
  if (cr > lf && cr > crlf) return Eol::Cr;
  return Eol::Lf;
}

// What kind of file these bytes are. A byte-order mark decides; otherwise valid UTF-8 is UTF-8 and
// anything else is taken as ISO-8859-1 (every byte maps to one character and back, so nothing is
// lost on save). A NUL byte (without a UTF-16 mark) or many control characters = not text.
inline FileKind detect(const std::string & s)
{
  FileKind k;
  const size_t n = s.size();
  auto b = [&](size_t i) { return static_cast<unsigned char>(s[i]); };
  if (n >= 3 && b(0) == 0xEF && b(1) == 0xBB && b(2) == 0xBF) { k.encoding = Encoding::Utf8Bom; k.bomSize = 3; }
  else if (n >= 2 && b(0) == 0xFF && b(1) == 0xFE) { k.encoding = Encoding::Utf16LE; k.bomSize = 2; }
  else if (n >= 2 && b(0) == 0xFE && b(1) == 0xFF) { k.encoding = Encoding::Utf16BE; k.bomSize = 2; }

  if (k.encoding == Encoding::Utf16LE || k.encoding == Encoding::Utf16BE)
  {
    if ((n - k.bomSize) % 2 != 0) { k.binary = true; return k; }
    // well-formed UTF-16 text: no NUL (also rules out UTF-32, whose mark starts the same way), every
    // surrogate paired - a decoder would turn anything else into U+FFFD and the save would change it
    const bool be = (k.encoding == Encoding::Utf16BE);
    auto unit = [&](size_t i) -> unsigned int { const unsigned int a = b(i), c = b(i + 1); return be ? ((a << 8) | c) : ((c << 8) | a); };
    for (size_t i = k.bomSize; i + 2 <= n; i += 2)
    {
      const unsigned int u = unit(i);
      if (u == 0) { k.binary = true; return k; }
      if (u >= 0xD800 && u <= 0xDBFF)
      {
        if (i + 4 > n) { k.binary = true; return k; }
        const unsigned int lo = unit(i + 2);
        if (lo < 0xDC00 || lo > 0xDFFF) { k.binary = true; return k; }
        i += 2;
      }
      else if (u >= 0xDC00 && u <= 0xDFFF) { k.binary = true; return k; }
    }
    k.eol = dominantEol(s, k.bomSize, 2, be, &k.mixedEol);
    return k;
  }
  size_t control = 0;
  for (size_t i = k.bomSize; i < n; ++i)
  {
    const unsigned char c = b(i);
    if (c == 0) { k.binary = true; return k; }
    if (c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != 0x1B) ++control;
  }
  if (n - k.bomSize > 0 && control * 20 > (n - k.bomSize)) { k.binary = true; return k; }     // more than 5 %
  if (k.encoding == Encoding::Utf8Bom) { if (!validUtf8(s.substr(3))) { k.binary = true; return k; } }
  else k.encoding = validUtf8(s) ? Encoding::Utf8 : Encoding::Latin1;
  k.eol = dominantEol(s, k.bomSize, 1, false, &k.mixedEol);
  return k;
}

// Text with "\n" line ends -> the file's own line ends.
inline std::string withEol(const std::string & lfText, Eol eol)
{
  if (eol == Eol::Lf) return lfText;
  std::string out; out.reserve(lfText.size() + lfText.size() / 16);
  for (char c : lfText)
  {
    if (c == '\n') { out += (eol == Eol::CrLf) ? "\r\n" : "\r"; }
    else out += c;
  }
  return out;
}

// Any of "\r\n", "\r", "\n" -> "\n".
inline std::string toLf(const std::string & text)
{
  std::string out; out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i)
  {
    if (text[i] == '\r') { out += '\n'; if (i + 1 < text.size() && text[i + 1] == '\n') ++i; }
    else out += text[i];
  }
  return out;
}

} // namespace editorlogic

#endif
