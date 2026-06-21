//---------------------------------------------------------------------------
// XMLIntf.hpp — Embarcadero Xml.XMLIntf subset: a small DOM used by SessionData for
// FileZilla/PuTTY/site XML import. Interfaces carry Delphi __property accessors (compiled with
// -fms-extensions); the concrete document (TXMLDocument) + an expat-backed parser live in
// XmlDoc.cpp. DelphiInterface is a non-owning smart pointer here — the document owns all nodes.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_XMLINTF_HPP
#define WINSCP_RTLCOMPAT_XMLINTF_HPP

#include "winscp/rtldefs.h"
#include "winscp/UnicodeString.h"
#include "SysUtils.hpp"   // OleVariant (Variant)
#include "winscp/DelphiSet.h"   // Set<> (TParseOptions)

namespace System {
  template <class Intf>
  class DelphiInterface
  {
  public:
    DelphiInterface() = default;
    DelphiInterface(Intf * p) : FIntf(p) {}
    template <class U> DelphiInterface(const DelphiInterface<U> & o) : FIntf(o.get()) {}
    Intf * operator->() const { return FIntf; }
    Intf * get() const { return FIntf; }
    explicit operator bool() const { return FIntf != nullptr; }  // explicit: avoid bool!=NULL ambiguity
    bool operator==(const DelphiInterface & o) const { return FIntf == o.FIntf; }
    bool operator!=(const DelphiInterface & o) const { return FIntf != o.FIntf; }
  private:
    Intf * FIntf = nullptr;
  };
}

namespace Xmlintf {
  enum TNodeType { ntReserved, ntElement, ntAttribute, ntText, ntCData, ntEntityRef, ntEntity,
    ntProcessingInstr, ntComment, ntDocument, ntDocType, ntDocFragment, ntNotation };
  struct IXMLNode;
  struct IXMLNodeList;
  struct IXMLDocument;
}

typedef System::DelphiInterface<Xmlintf::IXMLNode>     _di_IXMLNode;
typedef System::DelphiInterface<Xmlintf::IXMLNodeList> _di_IXMLNodeList;
typedef System::DelphiInterface<Xmlintf::IXMLDocument> _di_IXMLDocument;

namespace Xmlintf {

struct IXMLNodeList
{
  virtual int __fastcall GetCount() = 0;
  __declspec(property(get=GetCount)) int Count;
  virtual _di_IXMLNode __fastcall Get(int Index) = 0;
  virtual _di_IXMLNode __fastcall FindNode(const UnicodeString & Name) = 0;
  // 2-arg overload (Name, Namespace) — namespace ignored by this DOM; delegates to the 1-arg.
  _di_IXMLNode __fastcall FindNode(const UnicodeString & Name, const UnicodeString &) { return FindNode(Name); }
  virtual __fastcall ~IXMLNodeList() {}
};

struct IXMLNode
{
  virtual UnicodeString __fastcall GetText() = 0;
  virtual void __fastcall SetText(const UnicodeString & Value) = 0;
  __declspec(property(get=GetText, put=SetText)) UnicodeString Text;
  virtual UnicodeString __fastcall GetNodeName() = 0;
  __declspec(property(get=GetNodeName)) UnicodeString NodeName;
  virtual OleVariant __fastcall GetNodeValue() = 0;
  __declspec(property(get=GetNodeValue)) OleVariant NodeValue;
  virtual TNodeType __fastcall GetNodeType() = 0;
  __declspec(property(get=GetNodeType)) TNodeType NodeType;
  virtual _di_IXMLNodeList __fastcall GetChildNodes() = 0;
  __declspec(property(get=GetChildNodes)) _di_IXMLNodeList ChildNodes;
  virtual OleVariant __fastcall GetAttribute(const UnicodeString & Name) = 0;
  __declspec(property(get=GetAttribute)) OleVariant Attributes[];
  virtual __fastcall ~IXMLNode() {}
};

// Xml.XMLDoc parse options (Delphi TXMLDocument.ParseOptions). Only the names the engine uses;
// this DOM ignores them (it always preserves text as parsed).
enum TParseOption { poParseUnknown, poPreserveWhiteSpace, poAsyncLoad, poAutoPrefix,
  poNamespaceDecl, poAutoSave };
typedef Set<TParseOption, poParseUnknown, poAutoSave> TParseOptions;

struct IXMLDocument
{
  virtual void __fastcall LoadFromFile(const UnicodeString & FileName) = 0;
  virtual void __fastcall LoadFromXML(const UnicodeString & XML) = 0;
  virtual TParseOptions __fastcall GetParseOptions() = 0;
  virtual void __fastcall SetParseOptions(TParseOptions Value) = 0;
  __declspec(property(get=GetParseOptions, put=SetParseOptions)) TParseOptions ParseOptions;
  virtual _di_IXMLNode __fastcall GetDocumentElement() = 0;
  __declspec(property(get=GetDocumentElement)) _di_IXMLNode DocumentElement;
  virtual _di_IXMLNodeList __fastcall GetChildNodes() = 0;
  __declspec(property(get=GetChildNodes)) _di_IXMLNodeList ChildNodes;
  virtual __fastcall ~IXMLDocument() {}
};

}  // namespace Xmlintf

// The engine names the node-type enum values unqualified.
using Xmlintf::TNodeType;
using Xmlintf::ntReserved;  using Xmlintf::ntElement; using Xmlintf::ntAttribute;
using Xmlintf::ntText;      using Xmlintf::ntCData;   using Xmlintf::ntEntityRef;
using Xmlintf::ntComment;   using Xmlintf::ntDocument;
using Xmlintf::TParseOptions; using Xmlintf::TParseOption; using Xmlintf::poPreserveWhiteSpace;

// Delphi interface_cast<I>(obj) — reinterpret a concrete object as one of its interfaces.
template <class I, class T>
System::DelphiInterface<I> interface_cast(T * p) { return System::DelphiInterface<I>(static_cast<I *>(p)); }

// Concrete document (Xml.XMLDoc.TXMLDocument). ctor arg is the Owner (ignored). Implements
// IXMLDocument; parses via expat in LoadFromFile. Defined in XmlDoc.cpp.
class TXMLDocument : public Xmlintf::IXMLDocument
{
public:
  __fastcall TXMLDocument(void * Owner = nullptr);
  virtual __fastcall ~TXMLDocument();
  virtual void __fastcall LoadFromFile(const UnicodeString & FileName);
  virtual void __fastcall LoadFromXML(const UnicodeString & XML);
  virtual TParseOptions __fastcall GetParseOptions() { return FParseOptions; }
  virtual void __fastcall SetParseOptions(TParseOptions Value) { FParseOptions = Value; }
  virtual _di_IXMLNode __fastcall GetDocumentElement();
  virtual _di_IXMLNodeList __fastcall GetChildNodes();
private:
  void * FImpl;   // -> XmlDocImpl (owns the node tree)
  TParseOptions FParseOptions;
};

#endif
