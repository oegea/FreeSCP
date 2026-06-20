//---------------------------------------------------------------------------
// Contnrs.hpp — System::Contnrs subset (TObjectList; base of TNamedObjectList).
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_CONTNRS_HPP
#define WINSCP_RTLCOMPAT_CONTNRS_HPP

#include "Classes.hpp"

class TObjectList : public TList
{
public:
  bool OwnsObjects = true;
};

#endif
