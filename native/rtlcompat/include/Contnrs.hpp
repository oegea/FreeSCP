//---------------------------------------------------------------------------
// Contnrs.hpp — System::Contnrs subset (TObjectList; base of TNamedObjectList).
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_CONTNRS_HPP
#define WINSCP_RTLCOMPAT_CONTNRS_HPP

#include "Classes.hpp"

// TObjectList — TList that (optionally) owns/deletes its TObject* items.
class TObjectList : public TList
{
public:
  bool OwnsObjects = true;
  TObject * __fastcall GetItem(int Index) { return static_cast<TObject *>(Get(Index)); }
  void __fastcall SetItem(int Index, TObject * AObject) { Put(Index, AObject); }
  __declspec(property(get=GetItem, put=SetItem)) TObject * Items[];
  int __fastcall Add(TObject * AObject) { return TList::Add(AObject); }
  int __fastcall IndexOf(TObject * AObject) { return TList::IndexOf(AObject); }
protected:
  virtual void __fastcall Notify(void * Ptr, TListNotification Action)
  {
    if (OwnsObjects && Action == lnDeleted && Ptr != nullptr)
      delete static_cast<TObject *>(Ptr);
  }
};

#endif
