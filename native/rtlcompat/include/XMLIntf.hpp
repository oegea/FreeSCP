//---------------------------------------------------------------------------
// Xml.XMLIntf.hpp — Embarcadero Xml.XMLIntf subset (interface smart pointers used by
// SessionData for FileZilla/site XML import). Declarations only; a real XML backend
// (e.g. over expat) is wired in when SessionData.cpp is ported.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_XML_XMLINTF_HPP
#define WINSCP_RTLCOMPAT_XML_XMLINTF_HPP

#include "winscp/rtldefs.h"
#include "winscp/UnicodeString.h"

namespace System {
  // Delphi interface reference (COM-like smart pointer). Minimal: enough to declare
  // signatures and test for nil. Real ref-counting added with the XML backend.
  template <class Intf>
  class DelphiInterface
  {
  public:
    DelphiInterface() = default;
    DelphiInterface(Intf * p) : FIntf(p) {}
    Intf * operator->() const { return FIntf; }
    operator bool() const { return FIntf != nullptr; }
    bool operator==(const DelphiInterface & o) const { return FIntf == o.FIntf; }
  private:
    Intf * FIntf = nullptr;
  };
}

struct IXMLNode;
struct IXMLNodeList;
struct IXMLDocument;

typedef System::DelphiInterface<IXMLNode>     _di_IXMLNode;
typedef System::DelphiInterface<IXMLNodeList> _di_IXMLNodeList;
typedef System::DelphiInterface<IXMLDocument> _di_IXMLDocument;

struct IXMLNode
{
  virtual UnicodeString __fastcall GetText() = 0;
  virtual _di_IXMLNode __fastcall ChildNodes(const UnicodeString & Name) = 0;
};
struct IXMLNodeList { virtual int __fastcall Count() = 0; };
struct IXMLDocument { virtual _di_IXMLNode __fastcall DocumentElement() = 0; };

#endif
