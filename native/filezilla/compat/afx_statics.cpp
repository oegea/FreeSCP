//---------------------------------------------------------------------------
// afx_statics.cpp — definitions of MFC's empty-string statics for the CStringA defined in
// FileZilla's stdafx.h. _afxDataNilA is a shared, never-freed (nRefs = -1) empty CStringDataA;
// _afxPchNilA points at its (empty) character data.
//---------------------------------------------------------------------------
#include "stdafx.h"

namespace {
struct NilA { CStringDataA hdr; char data[1]; };
NilA g_nilA = { { -1, 0, 0 }, { 0 } };   // nRefs=-1 (locked/never freed), nDataLength=0, nAllocLength=0
}

CStringDataA * _afxDataNilA = &g_nilA.hdr;
LPCSTR _afxPchNilA = g_nilA.data;
