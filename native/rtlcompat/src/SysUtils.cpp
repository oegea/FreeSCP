//---------------------------------------------------------------------------
// SysUtils.cpp — System.Sysutils bodies (subset). Grows as engine .cpp files need it.
//---------------------------------------------------------------------------
#include "SysUtils.hpp"
#include "Classes.hpp"   // TSeekOrigin (soBeginning/soCurrent/soEnd)
#include <cerrno>
#include <cstring>
#include <unistd.h>

const UnicodeString EmptyStr;

__int64 __fastcall FileSeek(int Handle, __int64 Offset, int Origin)
{
  int whence = (Origin == soCurrent) ? SEEK_CUR : (Origin == soEnd) ? SEEK_END : SEEK_SET;
  return static_cast<__int64>(::lseek(Handle, static_cast<off_t>(Offset), whence));
}
__int64 __fastcall FileSeek(void * Handle, __int64 Offset, int Origin)
{
  return FileSeek(static_cast<int>(reinterpret_cast<intptr_t>(Handle)), Offset, Origin);
}

void __fastcall RaiseLastOSError() { RaiseLastOSError(errno); }

NORETURN void __fastcall Abort() { throw EAbort(UnicodeString()); }

void __fastcall RaiseLastOSError(int LastError)
{
  EOSError E(UnicodeString(::strerror(LastError)));
  E.ErrorCode = static_cast<DWORD>(LastError);
  throw E;
}

int __fastcall FileRead(int Handle, void * Buffer, int Count)
{
  return static_cast<int>(::read(Handle, Buffer, static_cast<size_t>(Count)));
}
int __fastcall FileWrite(int Handle, const void * Buffer, int Count)
{
  return static_cast<int>(::write(Handle, Buffer, static_cast<size_t>(Count)));
}
int __fastcall FileRead(int Handle, System::DynamicArray<System::Byte> Buffer, int Offset, int Count)
{
  return FileRead(Handle, &Buffer[Offset], Count);
}
int __fastcall FileWrite(int Handle, const System::DynamicArray<System::Byte> Buffer, int Offset, int Count)
{
  return FileWrite(Handle, &const_cast<System::DynamicArray<System::Byte> &>(Buffer)[Offset], Count);
}

int RandSeed = 0;

const UnicodeString System_Sysconst_SOSError = UnicodeString(L"System error. Code: %d. %s%s");
