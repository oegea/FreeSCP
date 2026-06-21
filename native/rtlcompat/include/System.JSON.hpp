#ifndef WINSCP_SYSTEM_JSON_HPP
#define WINSCP_SYSTEM_JSON_HPP

// Minimal System.JSON shim — only what S3FileSystem.cpp uses (AWS EC2 instance-metadata /
// credential parsing): TJSONObject::ParseJSONValue, Values[name], Value(), and an enumerator over
// the object's pairs. A compact recursive-descent parser builds the DOM (System.JSON.cpp).
// Not a full Delphi System.JSON — arrays/numbers are kept as text; extend on demand.

#include "winscp/UnicodeString.h"
#include <vector>

class TJSONValue
{
public:
  virtual ~TJSONValue() {}
  // Primitive textual value (string content unescaped; number/bool/null as written).
  virtual UnicodeString __fastcall Value() { return FText; }
  UnicodeString FText;
};

class TJSONString : public TJSONValue
{
public:
  virtual UnicodeString __fastcall Value() { return FText; }
};

class TJSONPair
{
public:
  ~TJSONPair() { delete JsonString; delete JsonValue; }
  TJSONString * JsonString = nullptr;
  TJSONValue * JsonValue = nullptr;
};

class TJSONObject : public TJSONValue
{
public:
  ~TJSONObject() { for (TJSONPair * p : FPairs) delete p; }

  // Parse a JSON document; returns a TJSONObject* (as TJSONValue*) for an object, or NULL on error
  // or non-object top level. Caller owns the result.
  static TJSONValue * __fastcall ParseJSONValue(const UnicodeString & S);

  // Value for a key, or NULL if absent (Delphi TJSONObject.Values[]).
  TJSONValue * __fastcall GetValue(const UnicodeString & Name);
  __declspec(property(get=GetValue)) TJSONValue * Values[];

  int __fastcall Count() { return static_cast<int>(FPairs.size()); }

  class TEnumerator
  {
  public:
    TEnumerator(TJSONObject * Obj) : FObj(Obj) {}
    bool __fastcall MoveNext()
    {
      ++FIndex;
      if (FIndex < static_cast<int>(FObj->FPairs.size())) { Current = FObj->FPairs[FIndex]; return true; }
      return false;
    }
    TJSONPair * Current = nullptr;
  private:
    TJSONObject * FObj;
    int FIndex = -1;
  };
  TEnumerator * __fastcall GetEnumerator() { return new TEnumerator(this); }

  std::vector<TJSONPair *> FPairs;
};

#endif
