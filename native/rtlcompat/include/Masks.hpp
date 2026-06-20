//---------------------------------------------------------------------------
// Masks.hpp — System::Masks subset (TMask glob matcher). Declarations only; the engine
// mostly uses its own TFileMasks, this base is rarely exercised. Bodies added if needed.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_MASKS_HPP
#define WINSCP_RTLCOMPAT_MASKS_HPP

#include "winscp/rtldefs.h"
#include "winscp/UnicodeString.h"
#include "Classes.hpp"

// The engine references this as Masks::TMask (its Embarcadero unit).
namespace Masks {
  class TMask : public TObject
  {
  public:
    __fastcall TMask(const UnicodeString & MaskValue) : FMask(MaskValue) {}
    bool __fastcall Matches(const UnicodeString & Filename);
  private:
    UnicodeString FMask;
  };
}
using Masks::TMask;

#endif
