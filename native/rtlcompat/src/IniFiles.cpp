//---------------------------------------------------------------------------
// IniFiles.cpp — System::IniFiles subset: file-backed INI storage (TCustomIniFile /
// TMemIniFile). This is the native port's real config backend (the registry path is a
// compile-only stub; Configuration selects INI on non-Windows). Sections/idents are matched
// case-insensitively (Delphi semantics). File I/O is UTF-8.
//---------------------------------------------------------------------------
#include <vcl.h>
#include "Registry.hpp"
#include "SysUtils.hpp"
#include "StrUtils.hpp"
#include <vector>
#include <utility>
#include <cstdio>
#include <string>

//---------------------------------------------------------------------------
struct Impl
{
  struct Section { UnicodeString Name; std::vector<std::pair<UnicodeString, UnicodeString>> Items; };
  std::vector<Section> Sections;

  Section * Find(const UnicodeString & Name)
  {
    for (auto & s : Sections) if (SameText(s.Name, Name)) return &s;
    return nullptr;
  }
  Section & Ensure(const UnicodeString & Name)
  {
    if (Section * s = Find(Name)) return *s;
    Sections.push_back(Section{Name, {}});
    return Sections.back();
  }
};

//---------------------------------------------------------------------------
Impl & __fastcall TCustomIniFile::D()
{
  if (FData == nullptr) FData = new Impl();
  return *static_cast<Impl *>(FData);
}

// UTF-8 file -> sections. Lines: "[Section]" opens a section; "ident=value" adds an entry;
// blanks and ';'/'#' comments are skipped.
void __fastcall TCustomIniFile::EnsureLoaded()
{
  if (FLoaded) return;
  FLoaded = true;
  Impl & d = D();
  std::string path(UTF8String(FFileName).c_str());
  std::FILE * f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return;
  std::string all; char buf[8192]; size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
  std::fclose(f);
  // strip UTF-8 BOM
  if (all.size() >= 3 && (unsigned char)all[0] == 0xEF && (unsigned char)all[1] == 0xBB && (unsigned char)all[2] == 0xBF)
    all.erase(0, 3);

  Impl::Section * cur = nullptr;
  size_t i = 0;
  while (i < all.size())
  {
    size_t e = all.find('\n', i);
    std::string raw = all.substr(i, (e == std::string::npos ? all.size() : e) - i);
    i = (e == std::string::npos ? all.size() : e + 1);
    if (!raw.empty() && raw.back() == '\r') raw.pop_back();
    UnicodeString line = Trim(UTF8ToString(RawByteString(raw.c_str())));
    if (line.IsEmpty()) continue;
    wchar_t c0 = line[1];
    if (c0 == L';' || c0 == L'#') continue;
    if (c0 == L'[')
    {
      int rb = line.Pos(L"]");
      UnicodeString name = (rb > 2) ? line.SubString(2, rb - 2) : UnicodeString();
      cur = &d.Ensure(name);
    }
    else
    {
      int eq = line.Pos(L"=");
      if (eq > 0)
      {
        UnicodeString ident = Trim(line.SubString(1, eq - 1));
        UnicodeString value = line.SubString(eq + 1, line.Length() - eq);
        if (cur == nullptr) cur = &d.Ensure(UnicodeString());
        cur->Items.push_back({ident, value});
      }
    }
  }
}

//---------------------------------------------------------------------------
UnicodeString __fastcall TCustomIniFile::ReadString(const UnicodeString & Section, const UnicodeString & Ident, const UnicodeString & Default)
{
  EnsureLoaded();
  if (Impl::Section * s = D().Find(Section))
    for (auto & kv : s->Items) if (SameText(kv.first, Ident)) return kv.second;
  return Default;
}
void __fastcall TCustomIniFile::WriteString(const UnicodeString & Section, const UnicodeString & Ident, const UnicodeString & Value)
{
  EnsureLoaded();
  Impl::Section & s = D().Ensure(Section);
  for (auto & kv : s.Items) if (SameText(kv.first, Ident)) { kv.second = Value; return; }
  s.Items.push_back({Ident, Value});
}
int __fastcall TCustomIniFile::ReadInteger(const UnicodeString & Section, const UnicodeString & Ident, int Default)
{
  UnicodeString S = ReadString(Section, Ident, UnicodeString());
  if (S.IsEmpty()) return Default;
  try { return StrToInt(S); } catch (...) { return Default; }
}
void __fastcall TCustomIniFile::WriteInteger(const UnicodeString & Section, const UnicodeString & Ident, int Value)
{ WriteString(Section, Ident, IntToStr(Value)); }
bool __fastcall TCustomIniFile::ReadBool(const UnicodeString & Section, const UnicodeString & Ident, bool Default)
{ return ReadInteger(Section, Ident, Default ? 1 : 0) != 0; }
void __fastcall TCustomIniFile::WriteBool(const UnicodeString & Section, const UnicodeString & Ident, bool Value)
{ WriteInteger(Section, Ident, Value ? 1 : 0); }
bool __fastcall TCustomIniFile::ValueExists(const UnicodeString & Section, const UnicodeString & Ident)
{
  EnsureLoaded();
  if (Impl::Section * s = D().Find(Section))
    for (auto & kv : s->Items) if (SameText(kv.first, Ident)) return true;
  return false;
}
bool __fastcall TCustomIniFile::SectionExists(const UnicodeString & Section)
{ EnsureLoaded(); return D().Find(Section) != nullptr; }
void __fastcall TCustomIniFile::DeleteKey(const UnicodeString & Section, const UnicodeString & Ident)
{
  EnsureLoaded();
  if (Impl::Section * s = D().Find(Section))
    for (size_t k = 0; k < s->Items.size(); ++k)
      if (SameText(s->Items[k].first, Ident)) { s->Items.erase(s->Items.begin() + k); return; }
}
void __fastcall TCustomIniFile::EraseSection(const UnicodeString & Section)
{
  EnsureLoaded();
  auto & v = D().Sections;
  for (size_t k = 0; k < v.size(); ++k) if (SameText(v[k].Name, Section)) { v.erase(v.begin() + k); return; }
}
void __fastcall TCustomIniFile::ReadSection(const UnicodeString & Section, TStrings * Strings)
{
  EnsureLoaded();
  if (Strings == nullptr) return;
  Strings->Clear();
  if (Impl::Section * s = D().Find(Section)) for (auto & kv : s->Items) Strings->Add(kv.first);
}
void __fastcall TCustomIniFile::ReadSections(TStrings * Strings)
{
  EnsureLoaded();
  if (Strings == nullptr) return;
  Strings->Clear();
  for (auto & s : D().Sections) Strings->Add(s.Name);
}

void __fastcall TCustomIniFile::UpdateFile()
{
  if (FFileName.IsEmpty()) return;
  std::string out;
  for (auto & s : D().Sections)
  {
    out += "["; out += std::string(UTF8String(s.Name).c_str()); out += "]\n";
    for (auto & kv : s.Items)
    {
      out += std::string(UTF8String(kv.first).c_str());
      out += "=";
      out += std::string(UTF8String(kv.second).c_str());
      out += "\n";
    }
    out += "\n";
  }
  std::string path(UTF8String(FFileName).c_str());
  std::FILE * f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return;
  if (!out.empty()) std::fwrite(out.data(), 1, out.size(), f);
  std::fclose(f);
}

//---------------------------------------------------------------------------
// TMemIniFile snapshot: emit "[Section]" headers + "ident=value" lines (Delphi GetStrings
// format), and parse the same back in SetStrings.
void __fastcall TMemIniFile::GetStrings(TStrings * Strings)
{
  EnsureLoaded();
  if (Strings == nullptr) return;
  Strings->Clear();
  for (auto & s : D().Sections)
  {
    Strings->Add(UnicodeString(L"[") + s.Name + UnicodeString(L"]"));
    for (auto & kv : s.Items) Strings->Add(kv.first + UnicodeString(L"=") + kv.second);
  }
}
void __fastcall TMemIniFile::SetStrings(TStrings * Strings)
{
  FLoaded = true;
  D().Sections.clear();
  if (Strings == nullptr) return;
  Impl::Section * cur = nullptr;
  for (int i = 0; i < Strings->Count; ++i)
  {
    UnicodeString line = Trim(Strings->Strings[i]);
    if (line.IsEmpty()) continue;
    if (line[1] == L'[')
    {
      int rb = line.Pos(L"]");
      cur = &D().Ensure((rb > 2) ? line.SubString(2, rb - 2) : UnicodeString());
    }
    else
    {
      int eq = line.Pos(L"=");
      if (eq > 0 && cur != nullptr)
        cur->Items.push_back({Trim(line.SubString(1, eq - 1)), line.SubString(eq + 1, line.Length() - eq)});
    }
  }
}
