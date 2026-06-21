// JsonDoc.cpp — recursive-descent JSON parser backing the System.JSON shim (System.JSON.hpp).
// Parses the AWS instance-metadata credential JSON S3FileSystem reads. Operates on UTF-8 (the
// metadata is ASCII); leaf strings are unescaped, other primitives kept as written.

#include "System.JSON.hpp"
#include "winscp/SysExtra.h"   // UTF8ToString
#include <string>

namespace {

struct Parser
{
  const std::string & s;
  size_t i = 0;
  bool ok = true;
  Parser(const std::string & str) : s(str) {}

  void skipWs() { while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) ++i; }
  char peek() { return (i < s.size()) ? s[i] : '\0'; }

  // Parse a JSON string (assumes current char is '"'); returns the unescaped content (UTF-8).
  std::string parseString()
  {
    std::string out;
    if (peek() != '"') { ok = false; return out; }
    ++i;
    while (i < s.size())
    {
      char c = s[i++];
      if (c == '"') return out;
      if (c == '\\' && i < s.size())
      {
        char e = s[i++];
        switch (e)
        {
          case 'n': out.push_back('\n'); break;
          case 't': out.push_back('\t'); break;
          case 'r': out.push_back('\r'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case '/': out.push_back('/'); break;
          case '\\': out.push_back('\\'); break;
          case '"': out.push_back('"'); break;
          case 'u':
          {
            // \uXXXX -> UTF-8 (BMP only; sufficient for credential JSON).
            if (i + 4 <= s.size())
            {
              unsigned cp = 0;
              for (int k = 0; k < 4; ++k)
              { char h = s[i++]; cp <<= 4;
                if (h>='0'&&h<='9') cp |= (h-'0'); else if (h>='a'&&h<='f') cp |= (h-'a'+10);
                else if (h>='A'&&h<='F') cp |= (h-'A'+10); }
              if (cp < 0x80) out.push_back((char)cp);
              else if (cp < 0x800) { out.push_back((char)(0xC0|(cp>>6))); out.push_back((char)(0x80|(cp&0x3F))); }
              else { out.push_back((char)(0xE0|(cp>>12))); out.push_back((char)(0x80|((cp>>6)&0x3F))); out.push_back((char)(0x80|(cp&0x3F))); }
            }
            break;
          }
          default: out.push_back(e); break;
        }
      }
      else out.push_back(c);
    }
    ok = false;
    return out;
  }

  // Parse a primitive (number/true/false/null) as its raw text.
  std::string parsePrimitive()
  {
    size_t start = i;
    while (i < s.size())
    {
      char c = s[i];
      if (c==','||c=='}'||c==']'||c==' '||c=='\t'||c=='\n'||c=='\r') break;
      ++i;
    }
    return s.substr(start, i - start);
  }

  void skipValue();  // consume a value without building (for arrays/nested we don't expose)

  TJSONValue * parseValue()
  {
    skipWs();
    char c = peek();
    if (c == '{') return parseObject();
    if (c == '[') { skipValue(); TJSONValue * v = new TJSONValue(); return v; }  // arrays kept empty
    if (c == '"') { TJSONString * v = new TJSONString(); v->FText = UTF8ToString(RawByteString(parseString().c_str())); return v; }
    TJSONValue * v = new TJSONValue(); v->FText = UTF8ToString(RawByteString(parsePrimitive().c_str())); return v;
  }

  TJSONObject * parseObject()
  {
    if (peek() != '{') { ok = false; return nullptr; }
    ++i;
    TJSONObject * obj = new TJSONObject();
    skipWs();
    if (peek() == '}') { ++i; return obj; }
    while (ok)
    {
      skipWs();
      if (peek() != '"') { ok = false; break; }
      std::string key = parseString();
      skipWs();
      if (peek() != ':') { ok = false; break; }
      ++i;
      TJSONValue * val = parseValue();
      TJSONPair * pair = new TJSONPair();
      pair->JsonString = new TJSONString(); pair->JsonString->FText = UTF8ToString(RawByteString(key.c_str()));
      pair->JsonValue = val;
      obj->FPairs.push_back(pair);
      skipWs();
      char d = peek();
      if (d == ',') { ++i; continue; }
      if (d == '}') { ++i; break; }
      ok = false; break;
    }
    if (!ok) { delete obj; return nullptr; }
    return obj;
  }
};

void Parser::skipValue()
{
  skipWs();
  char c = peek();
  if (c == '"') { parseString(); return; }
  if (c == '{' || c == '[')
  {
    char open = c, close = (c == '{') ? '}' : ']';
    int depth = 0;
    while (i < s.size())
    {
      char ch = s[i];
      if (ch == '"') { parseString(); continue; }
      if (ch == open) depth++;
      else if (ch == close) { depth--; ++i; if (depth == 0) return; continue; }
      ++i;
    }
    return;
  }
  parsePrimitive();
}

} // namespace

TJSONValue * __fastcall TJSONObject::ParseJSONValue(const UnicodeString & S)
{
  std::string utf8 = std::string(UTF8String(S).c_str());
  Parser p(utf8);
  p.skipWs();
  if (p.peek() != '{') return nullptr;   // we only need object top-level
  TJSONObject * obj = p.parseObject();
  return p.ok ? static_cast<TJSONValue *>(obj) : (delete obj, nullptr);
}

TJSONValue * __fastcall TJSONObject::GetValue(const UnicodeString & Name)
{
  for (TJSONPair * pair : FPairs)
    if ((pair->JsonString != nullptr) && (pair->JsonString->Value() == Name))
      return pair->JsonValue;
  return nullptr;
}
