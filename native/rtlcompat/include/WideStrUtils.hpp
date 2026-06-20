#ifndef WINSCP_RTLCOMPAT_WIDESTRUTILS_HPP
#define WINSCP_RTLCOMPAT_WIDESTRUTILS_HPP
#include "StrUtils.hpp"
#include "winscp/AnsiStrings.h"

// System.WideStrUtils: byte-string encoding sniff. etUSASCII = all 7-bit; etUTF8 = valid
// multibyte UTF-8 present; etANSI = contains bytes that aren't valid UTF-8.
enum TEncodeType { etUSASCII, etUTF8, etANSI };
TEncodeType __fastcall DetectUTF8Encoding(const RawByteString & S);

#endif
