//---------------------------------------------------------------------------
// XmlDoc.cpp — concrete DOM + a small self-contained XML parser behind XMLIntf.hpp's
// TXMLDocument. Scope is WinSCP's session-import XML (elements, attributes, text, comments,
// the <?xml?> decl, the five predefined entities) — enough for FileZilla/PuTTY/site import.
// The document owns every node and list; the DelphiInterface smart pointers are non-owning.
//---------------------------------------------------------------------------
#include "XMLDoc.hpp"
#include "winscp/SysExtra.h"   // UTF8ToString / TFile
#include <vector>
#include <memory>
#include <string>
#include <cstdio>

using namespace Xmlintf;

namespace {

struct NodeImpl;

struct ListImpl : IXMLNodeList
{
  std::vector<NodeImpl *> Items;
  int __fastcall GetCount() override { return (int)Items.size(); }
  _di_IXMLNode __fastcall Get(int Index) override;
  _di_IXMLNode __fastcall FindNode(const UnicodeString & Name) override;
};

struct NodeImpl : IXMLNode
{
  UnicodeString Name;
  UnicodeString TextValue;       // for ntText nodes
  TNodeType Type = ntElement;
  std::vector<std::pair<UnicodeString, UnicodeString>> Attrs;
  ListImpl Children;             // child elements + text nodes

  UnicodeString __fastcall GetText() override
  {
    if (Type == ntText) return TextValue;
    UnicodeString r;             // element Text = concatenation of its text children
    for (NodeImpl * c : Children.Items) if (c->Type == ntText) r += c->TextValue;
    return r;
  }
  void __fastcall SetText(const UnicodeString & Value) override { TextValue = Value; }
  UnicodeString __fastcall GetNodeName() override { return Name; }
  OleVariant __fastcall GetNodeValue() override
  { return (Type == ntText) ? OleVariant(TextValue) : OleVariant(); }
  TNodeType __fastcall GetNodeType() override { return Type; }
  _di_IXMLNodeList __fastcall GetChildNodes() override { return _di_IXMLNodeList(&Children); }
  OleVariant __fastcall GetAttribute(const UnicodeString & AName) override
  {
    for (auto & kv : Attrs) if (kv.first == AName) return OleVariant(kv.second);
    return OleVariant();         // Null when absent
  }
};

_di_IXMLNode __fastcall ListImpl::Get(int Index)
{ return (Index >= 0 && Index < (int)Items.size()) ? _di_IXMLNode(Items[Index]) : _di_IXMLNode(); }
_di_IXMLNode __fastcall ListImpl::FindNode(const UnicodeString & Name)
{ for (NodeImpl * n : Items) if (n->Name == Name) return _di_IXMLNode(n); return _di_IXMLNode(); }

// --- minimal XML parser over a UTF-8 byte string ---
struct Parser
{
  const std::string & s;
  size_t i = 0;
  std::vector<std::unique_ptr<NodeImpl>> & pool;
  Parser(const std::string & str, std::vector<std::unique_ptr<NodeImpl>> & p) : s(str), pool(p) {}

  NodeImpl * make() { pool.push_back(std::make_unique<NodeImpl>()); return pool.back().get(); }
  bool eof() const { return i >= s.size(); }
  void skipWs() { while (i < s.size() && (unsigned char)s[i] <= ' ') ++i; }

  static UnicodeString decode(const std::string & in)
  {
    std::string out; out.reserve(in.size());
    for (size_t k = 0; k < in.size(); )
    {
      if (in[k] == '&')
      {
        size_t semi = in.find(';', k);
        if (semi != std::string::npos)
        {
          std::string e = in.substr(k + 1, semi - k - 1);
          if (e == "lt") out += '<'; else if (e == "gt") out += '>';
          else if (e == "amp") out += '&'; else if (e == "quot") out += '"';
          else if (e == "apos") out += '\''; else if (!e.empty() && e[0] == '#')
          { long cp = (e.size() > 1 && (e[1] == 'x' || e[1] == 'X')) ? strtol(e.c_str() + 2, nullptr, 16)
                                                                     : strtol(e.c_str() + 1, nullptr, 10);
            if (cp < 0x80) out += (char)cp;                                  // ASCII fast path
            else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
            else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); } }
          else out += e;          // unknown entity: keep raw
          k = semi + 1; continue;
        }
      }
      out += in[k++];
    }
    return UTF8ToString(RawByteString(out.c_str(), (int)out.size()));
  }

  void skipMisc()   // comments, <?...?>, <!...> (doctype) between/around elements
  {
    for (;;)
    {
      skipWs();
      if (i + 3 < s.size() && s.compare(i, 4, "<!--") == 0)
      { size_t e = s.find("-->", i + 4); i = (e == std::string::npos) ? s.size() : e + 3; }
      else if (i + 1 < s.size() && s.compare(i, 2, "<?") == 0)
      { size_t e = s.find("?>", i + 2); i = (e == std::string::npos) ? s.size() : e + 2; }
      else if (i + 1 < s.size() && s.compare(i, 2, "<!") == 0)
      { size_t e = s.find('>', i + 2); i = (e == std::string::npos) ? s.size() : e + 1; }
      else break;
    }
  }

  NodeImpl * parseElement()
  {
    skipMisc();
    if (eof() || s[i] != '<') return nullptr;
    ++i;                                   // '<'
    size_t start = i;
    while (i < s.size() && s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r'
           && s[i] != '>' && s[i] != '/') ++i;
    NodeImpl * node = make();
    node->Type = ntElement;
    node->Name = decode(s.substr(start, i - start));
    // attributes
    for (;;)
    {
      skipWs();
      if (eof() || s[i] == '>' || s[i] == '/') break;
      size_t as = i;
      while (i < s.size() && s[i] != '=' && s[i] != ' ' && s[i] != '>' && s[i] != '/') ++i;
      std::string aname = s.substr(as, i - as);
      skipWs();
      std::string aval;
      if (i < s.size() && s[i] == '=')
      {
        ++i; skipWs();
        if (i < s.size() && (s[i] == '"' || s[i] == '\''))
        { char q = s[i++]; size_t vs = i; while (i < s.size() && s[i] != q) ++i;
          aval = s.substr(vs, i - vs); if (i < s.size()) ++i; }
      }
      node->Attrs.push_back({ decode(aname), decode(aval) });
    }
    if (i < s.size() && s[i] == '/') { i += 2; return node; }   // self-closing '/>'
    if (i < s.size() && s[i] == '>') ++i;                       // '>'
    // children + text until matching close tag
    std::string text;
    auto flushText = [&]() {
      if (!text.empty()) { NodeImpl * t = make(); t->Type = ntText; t->TextValue = decode(text);
                           node->Children.Items.push_back(t); text.clear(); }
    };
    while (!eof())
    {
      if (s[i] == '<')
      {
        if (i + 1 < s.size() && s[i + 1] == '/')                // close tag
        { flushText(); size_t e = s.find('>', i); i = (e == std::string::npos) ? s.size() : e + 1; break; }
        if (i + 3 < s.size() && s.compare(i, 4, "<!--") == 0)
        { size_t e = s.find("-->", i + 4); i = (e == std::string::npos) ? s.size() : e + 3; continue; }
        if (i + 8 < s.size() && s.compare(i, 9, "<![CDATA[") == 0)
        { size_t e = s.find("]]>", i + 9); size_t cs = i + 9;
          text += s.substr(cs, (e == std::string::npos ? s.size() : e) - cs);
          i = (e == std::string::npos) ? s.size() : e + 3; continue; }
        flushText();
        NodeImpl * child = parseElement();
        if (child) node->Children.Items.push_back(child); else break;
      }
      else text += s[i++];
    }
    return node;
  }
};

struct XmlDocImpl
{
  std::vector<std::unique_ptr<NodeImpl>> Pool;
  ListImpl Root;                 // top-level nodes (the document element)
  NodeImpl * Element = nullptr;

  void Load(const UnicodeString & FileName)
  {
    Pool.clear(); Root.Items.clear(); Element = nullptr;
    std::FILE * f = std::fopen(std::string(UTF8String(FileName).c_str()).c_str(), "rb");
    if (!f) return;
    std::string all; char buf[8192]; size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
    std::fclose(f);
    if (all.size() >= 3 && (unsigned char)all[0] == 0xEF && (unsigned char)all[1] == 0xBB && (unsigned char)all[2] == 0xBF)
      all.erase(0, 3);
    Parser p(all, Pool);
    Element = p.parseElement();
    if (Element) Root.Items.push_back(Element);
  }
};

}  // namespace

//---------------------------------------------------------------------------
__fastcall TXMLDocument::TXMLDocument(void *) : FImpl(new XmlDocImpl()) {}
__fastcall TXMLDocument::~TXMLDocument() { delete static_cast<XmlDocImpl *>(FImpl); }
void __fastcall TXMLDocument::LoadFromFile(const UnicodeString & FileName)
{ static_cast<XmlDocImpl *>(FImpl)->Load(FileName); }
_di_IXMLNode __fastcall TXMLDocument::GetDocumentElement()
{ XmlDocImpl * d = static_cast<XmlDocImpl *>(FImpl); return d->Element ? _di_IXMLNode(d->Element) : _di_IXMLNode(); }
_di_IXMLNodeList __fastcall TXMLDocument::GetChildNodes()
{ return _di_IXMLNodeList(&static_cast<XmlDocImpl *>(FImpl)->Root); }

_di_IXMLDocument __fastcall NewXMLDocument(const UnicodeString &) { return _di_IXMLDocument(new TXMLDocument(nullptr)); }
_di_IXMLDocument __fastcall LoadXMLDocument(const UnicodeString & FileName)
{ TXMLDocument * d = new TXMLDocument(nullptr); d->LoadFromFile(FileName); return _di_IXMLDocument(d); }
