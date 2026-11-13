#ifndef WINSCP_FZ_TEXTS_H
#define WINSCP_FZ_TEXTS_H
// Forward to the real resource-id header (explicit relative path: this shim shadows it on the
// include path). The ids must be upstream's, because LoadStr() looks the text up in the table
// generated from source/resource/*.rc (rtlcompat/src/ResStrings.cpp); a private enum here made
// every FTP status/error message come out as "str#NN".
#include "../../../source/resource/TextsFileZilla.h"
#endif
