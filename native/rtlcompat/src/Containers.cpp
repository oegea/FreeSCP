//---------------------------------------------------------------------------
// Containers.cpp — TList / TStrings / TStringList bodies (System.Classes).
//---------------------------------------------------------------------------
#include "Classes.hpp"
#include "winscp/SysStrFuncs.h"
#include <algorithm>
#include <cstdio>

//--- TList ---
int __fastcall TList::Add(void * Item)
{
  FItems.push_back(Item);
  int idx = static_cast<int>(FItems.size()) - 1;
  Notify(Item, lnAdded);
  return idx;
}
void __fastcall TList::Insert(int Index, void * Item)
{
  FItems.insert(FItems.begin() + Index, Item);
  Notify(Item, lnAdded);
}
void __fastcall TList::Delete(int Index)
{
  void * Item = FItems[static_cast<size_t>(Index)];
  FItems.erase(FItems.begin() + Index);
  Notify(Item, lnDeleted);
}
int __fastcall TList::IndexOf(void * Item)
{
  for (size_t i = 0; i < FItems.size(); ++i) if (FItems[i] == Item) return static_cast<int>(i);
  return -1;
}
int __fastcall TList::Remove(void * Item)
{
  int i = IndexOf(Item);
  if (i >= 0) Delete(i);
  return i;
}
void __fastcall TList::Clear()
{
  while (!FItems.empty()) Delete(static_cast<int>(FItems.size()) - 1);
}
void __fastcall TList::Move(int CurIndex, int NewIndex)
{
  void * Item = FItems[static_cast<size_t>(CurIndex)];
  FItems.erase(FItems.begin() + CurIndex);
  FItems.insert(FItems.begin() + NewIndex, Item);
}
void __fastcall TList::Sort(int (* Compare)(void *, void *))
{
  std::sort(FItems.begin(), FItems.end(),
            [Compare](void * a, void * b) { return Compare(a, b) < 0; });
}

//--- TStrings (abstract helpers) ---
int __fastcall TStrings::Add(const UnicodeString & S) { return AddObject(S, nullptr); }
void __fastcall TStrings::AddStrings(TStrings * Strings)
{
  for (int i = 0; i < Strings->GetCount(); ++i) AddObject(Strings->Get(i), Strings->GetObject(i));
}
int __fastcall TStrings::IndexOf(const UnicodeString & S)
{
  for (int i = 0; i < GetCount(); ++i) if (Get(i) == S) return i;
  return -1;
}
int __fastcall TStrings::IndexOfName(const UnicodeString & Name)
{
  for (int i = 0; i < GetCount(); ++i) if (SameText(GetName(i), Name)) return i;
  return -1;
}
UnicodeString __fastcall TStrings::GetText()
{
  UnicodeString Result;
  for (int i = 0; i < GetCount(); ++i) Result += Get(i) + UnicodeString(L"\r\n");
  return Result;
}
void __fastcall TStrings::SetText(const UnicodeString & Text)
{
  Clear();
  UnicodeString line;
  for (int i = 1; i <= Text.Length(); ++i)
  {
    wchar_t c = static_cast<wchar_t>(Text[i]);
    if (c == L'\n') { Add(line); line = UnicodeString(); }
    else if (c != L'\r') line += UnicodeString(c, 1);
  }
  if (!line.IsEmpty()) Add(line);
}
UnicodeString __fastcall TStrings::GetName(int Index)
{
  UnicodeString S = Get(Index);
  int p = S.Pos(UnicodeString(L"="));
  return (p > 0) ? S.SubString(1, p - 1) : UnicodeString();
}
UnicodeString __fastcall TStrings::GetValue(const UnicodeString & Name)
{
  int i = IndexOfName(Name);
  if (i < 0) return UnicodeString();
  UnicodeString S = Get(i);
  return S.SubString(Name.Length() + 2, S.Length());
}
void __fastcall TStrings::SetValue(const UnicodeString & Name, const UnicodeString & Value)
{
  int i = IndexOfName(Name);
  UnicodeString line = Name + UnicodeString(L"=") + Value;
  if (i < 0) Add(line); else Put(i, line);
}
UnicodeString __fastcall TStrings::GetCommaText()
{
  UnicodeString Result;
  for (int i = 0; i < GetCount(); ++i)
  { if (i > 0) Result += UnicodeString(Delimiter, 1); Result += Get(i); }
  return Result;
}
UnicodeString __fastcall TStrings::GetDelimitedText()
{
  UnicodeString Result;
  for (int i = 0; i < GetCount(); ++i)
  { if (i > 0) Result += UnicodeString(Delimiter, 1); Result += Get(i); }
  return Result;
}
void __fastcall TStrings::SetDelimitedText(const UnicodeString & Value)
{
  Clear();
  UnicodeString item;
  wchar_t d = Delimiter;
  for (int i = 1; i <= Value.Length(); ++i)
  { wchar_t c = (wchar_t)Value[i]; if (c == d) { Add(item); item = UnicodeString(); } else item += UnicodeString(c, 1); }
  if (!item.IsEmpty()) Add(item);
}
void __fastcall TStrings::SetCommaText(const UnicodeString & Value)
{
  Clear();
  UnicodeString item;
  for (int i = 1; i <= Value.Length(); ++i)
  {
    wchar_t c = static_cast<wchar_t>(Value[i]);
    if (c == Delimiter) { Add(item); item = UnicodeString(); }
    else item += UnicodeString(c, 1);
  }
  if (!item.IsEmpty()) Add(item);
}

//--- TStringList ---
int __fastcall TStringList::GetCount() { return static_cast<int>(FList.size()); }
UnicodeString __fastcall TStringList::Get(int Index) { return FList[static_cast<size_t>(Index)].FString; }
void __fastcall TStringList::Put(int Index, const UnicodeString & S) { FList[static_cast<size_t>(Index)].FString = S; }
TObject * __fastcall TStringList::GetObject(int Index) { return FList[static_cast<size_t>(Index)].FObject; }
void __fastcall TStringList::PutObject(int Index, TObject * AObject) { FList[static_cast<size_t>(Index)].FObject = AObject; }
int __fastcall TStringList::AddObject(const UnicodeString & S, TObject * AObject)
{
  FList.push_back({S, AObject});
  if (Sorted) Sort();
  return IndexOf(S);
}
void __fastcall TStringList::Insert(int Index, const UnicodeString & S)
{ FList.insert(FList.begin() + Index, {S, nullptr}); }
void __fastcall TStringList::Delete(int Index) { FList.erase(FList.begin() + Index); }
void __fastcall TStringList::Clear() { FList.clear(); }
int __fastcall TStringList::IndexOf(const UnicodeString & S)
{
  for (size_t i = 0; i < FList.size(); ++i)
  {
    bool eq = CaseSensitive ? (FList[i].FString == S) : (FList[i].FString.CompareIC(S) == 0);
    if (eq) return static_cast<int>(i);
  }
  return -1;
}
void __fastcall TStringList::Sort()
{
  bool ci = !CaseSensitive;
  std::sort(FList.begin(), FList.end(), [ci](const TItem & a, const TItem & b) {
    return ci ? (a.FString.CompareIC(b.FString) < 0) : (a.FString.Compare(b.FString) < 0); });
}

UnicodeString __fastcall TStrings::GetValueFromIndex(int Index)
{
  UnicodeString S = Get(Index);
  int p = S.Pos(UnicodeString(L"="));
  return (p > 0) ? S.SubString(p + 1, S.Length()) : UnicodeString();
}

void __fastcall TStrings::Move(int CurIndex, int NewIndex)
{ UnicodeString s = Get(CurIndex); TObject * o = GetObject(CurIndex); Delete(CurIndex);
  Insert(NewIndex, s); PutObject(NewIndex, o); }
void __fastcall TStrings::Exchange(int I1, int I2)
{ UnicodeString s = Get(I1); TObject * o = GetObject(I1);
  Put(I1, Get(I2)); PutObject(I1, GetObject(I2)); Put(I2, s); PutObject(I2, o); }

bool __fastcall TStringList::Find(const UnicodeString & S, int & Index)
{ Index = IndexOf(S); if (Index >= 0) return true; Index = GetCount(); return false; }

void __fastcall TStrings::LoadFromFile(const UnicodeString & FileName)
{
  std::string path; for (char16_t c : FileName.raw()) path.push_back((char)c);
  std::FILE * f = std::fopen(path.c_str(), "rb"); if (!f) return;
  std::string all; char buf[4096]; size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
  std::fclose(f);
  UnicodeString u; for (char c : all) u += UnicodeString((char16_t)(unsigned char)c, 1);
  SetText(u);
}
void __fastcall TStrings::SaveToFile(const UnicodeString & FileName)
{
  std::string path; for (char16_t c : FileName.raw()) path.push_back((char)c);
  std::FILE * f = std::fopen(path.c_str(), "wb"); if (!f) return;
  UnicodeString t = GetText(); for (int i = 1; i <= t.Length(); ++i) { char c = (char)t[i]; std::fwrite(&c, 1, 1, f); }
  std::fclose(f);
}
void __fastcall TStrings::SaveToStream(TStream * Stream)
{
  if (Stream == nullptr) return;
  // byte-per-char (matches SaveToFile); UTF-8-aware INI snapshotting is done in IniFiles.cpp.
  UnicodeString t = GetText();
  std::string bytes; bytes.reserve(t.Length());
  for (int i = 1; i <= t.Length(); ++i) bytes.push_back((char)t[i]);
  if (!bytes.empty()) Stream->WriteBuffer(bytes.data(), (int)bytes.size());
}
void __fastcall TStrings::LoadFromStream(TStream * Stream)
{
  if (Stream == nullptr) return;
  __int64 sz = Stream->GetSize(); std::string bytes((size_t)(sz > 0 ? sz : 0), '\0');
  if (sz > 0) Stream->ReadBuffer(&bytes[0], (int)sz);
  UnicodeString u; for (char c : bytes) u += UnicodeString((char16_t)(unsigned char)c, 1);
  SetText(u);
}
bool __fastcall TStrings::Equals(TStrings * Strings)
{
  if (Strings == nullptr) return false;
  if (GetCount() != Strings->GetCount()) return false;
  for (int i = 0; i < GetCount(); ++i) if (Get(i) != Strings->Get(i)) return false;
  return true;
}
