//---------------------------------------------------------------------------
// Streams.cpp — TStream family bodies (System.Classes). POSIX-backed; this is RTL, not the
// platform adapter layer, but file/handle IO is thin enough to live here for now.
//---------------------------------------------------------------------------
#include "Classes.hpp"
#include "SysUtils.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>
#include <algorithm>

//--- TStream base: Position/Size via Seek; buffered read/write that must transfer fully ---
__int64 __fastcall TStream::Seek(__int64 /*Offset*/, Word /*Origin*/) { return 0; }

__int64 __fastcall TStream::GetPosition() { return Seek(0, soFromCurrent); }
void __fastcall TStream::SetPosition(__int64 Pos) { Seek(Pos, soFromBeginning); }

__int64 __fastcall TStream::GetSize()
{
  __int64 pos = Seek(0, soFromCurrent);
  __int64 result = Seek(0, soFromEnd);
  Seek(pos, soFromBeginning);
  return result;
}
void __fastcall TStream::SetSize(__int64 /*NewSize*/) {}

void __fastcall TStream::ReadBuffer(void * Buffer, int Count)
{
  if (Count != 0 && Read(Buffer, Count) != Count)
    throw EReadError(UnicodeString(L"Stream read error"));
}
void __fastcall TStream::WriteBuffer(const void * Buffer, int Count)
{
  if (Count != 0 && Write(Buffer, Count) != Count)
    throw EWriteError(UnicodeString(L"Stream write error"));
}
__int64 __fastcall TStream::CopyFrom(TStream * Source, __int64 Count)
{
  const int BufSize = 64 * 1024;
  char buf[BufSize];
  __int64 total = 0;
  if (Count == 0) { Source->Position = 0; Count = Source->Size; }
  while (Count > 0)
  {
    int n = static_cast<int>(std::min<__int64>(Count, BufSize));
    Source->ReadBuffer(buf, n);
    WriteBuffer(buf, n);
    Count -= n; total += n;
  }
  return total;
}

//--- THandleStream: raw fd ---
static int OriginToWhence(Word Origin)
{
  switch (Origin) { case soFromCurrent: return SEEK_CUR; case soFromEnd: return SEEK_END;
                    default: return SEEK_SET; }
}
int __fastcall THandleStream::Read(void * Buffer, int Count)
{
  ssize_t n = ::read(FHandle, Buffer, static_cast<size_t>(Count));
  return (n < 0) ? 0 : static_cast<int>(n);
}
int __fastcall THandleStream::Write(const void * Buffer, int Count)
{
  ssize_t n = ::write(FHandle, Buffer, static_cast<size_t>(Count));
  return (n < 0) ? 0 : static_cast<int>(n);
}
__int64 __fastcall THandleStream::Seek(__int64 Offset, Word Origin)
{
  return static_cast<__int64>(::lseek(FHandle, static_cast<off_t>(Offset), OriginToWhence(Origin)));
}

//--- TFileStream ---
__fastcall TFileStream::TFileStream(const UnicodeString & FileName, Word Mode)
  : THandleStream(-1)
{
  // UTF-16 -> UTF-8 path (naive ASCII/latin path for now; full UTF-8 in platform layer).
  const std::u16string & w = FileName.raw();
  std::string path; path.reserve(w.size());
  for (char16_t c : w) path.push_back(static_cast<char>(c));
  int flags;
  if (Mode == fmCreate) flags = O_RDWR | O_CREAT | O_TRUNC;
  else if ((Mode & 0x0F) == fmOpenWrite) flags = O_WRONLY;
  else if ((Mode & 0x0F) == fmOpenReadWrite) flags = O_RDWR;
  else flags = O_RDONLY;
  FHandle = ::open(path.c_str(), flags, 0644);
  if (FHandle < 0) throw EFOpenError(UnicodeString(L"Cannot open file ") + FileName);
}
__fastcall TFileStream::~TFileStream() { if (FHandle >= 0) ::close(FHandle); }

//--- TMemoryStream: growable buffer ---
__fastcall TMemoryStream::~TMemoryStream() { ::free(FData); }
void * __fastcall TMemoryStream::GetMemory() { return FData; }
void __fastcall TMemoryStream::Clear() { ::free(FData); FData = nullptr; FSize = FCapacity = FPosition = 0; }
__int64 __fastcall TMemoryStream::GetSize() { return FSize; }
void __fastcall TMemoryStream::SetSize(__int64 NewSize)
{
  unsigned char * p = static_cast<unsigned char *>(::realloc(FData, static_cast<size_t>(NewSize ? NewSize : 1)));
  if (p == nullptr && NewSize > 0) throw EStreamError(UnicodeString(L"Out of memory"));
  FData = p; FCapacity = NewSize; FSize = NewSize;
  if (FPosition > FSize) FPosition = FSize;
}
int __fastcall TMemoryStream::Read(void * Buffer, int Count)
{
  __int64 avail = FSize - FPosition;
  int n = static_cast<int>(std::min<__int64>(Count, avail < 0 ? 0 : avail));
  if (n > 0) { ::memcpy(Buffer, FData + FPosition, static_cast<size_t>(n)); FPosition += n; }
  return n;
}
int __fastcall TMemoryStream::Write(const void * Buffer, int Count)
{
  if (FPosition + Count > FCapacity) SetSize(FPosition + Count);
  ::memcpy(FData + FPosition, Buffer, static_cast<size_t>(Count));
  FPosition += Count;
  if (FPosition > FSize) FSize = FPosition;
  return Count;
}
__int64 __fastcall TMemoryStream::Seek(__int64 Offset, Word Origin)
{
  switch (Origin) { case soFromCurrent: FPosition += Offset; break;
                    case soFromEnd: FPosition = FSize + Offset; break;
                    default: FPosition = Offset; }
  return FPosition;
}

//--- TStringStream ---
__fastcall TStringStream::TStringStream(const UnicodeString & AString)
{
  // Store as UTF-16 bytes (matches engine treating it as raw buffer).
  const std::u16string & w = AString.raw();
  if (!w.empty()) Write(w.data(), static_cast<int>(w.size() * sizeof(char16_t)));
  FPosition = 0;
}
UnicodeString __fastcall TStringStream::DataString()
{
  return UnicodeString(reinterpret_cast<const char16_t *>(FData),
                       static_cast<int>(FSize / sizeof(char16_t)));
}
