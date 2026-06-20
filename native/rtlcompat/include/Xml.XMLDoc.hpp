#ifndef WINSCP_RTLCOMPAT_XMLDOC_HPP
#define WINSCP_RTLCOMPAT_XMLDOC_HPP
#include "Xml.XMLIntf.hpp"
_di_IXMLDocument __fastcall NewXMLDocument(const UnicodeString & Version = UnicodeString());
_di_IXMLDocument __fastcall LoadXMLDocument(const UnicodeString & FileName);
#endif
