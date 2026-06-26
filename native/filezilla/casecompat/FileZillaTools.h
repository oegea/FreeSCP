// Case-exact forwarding shim. source/filezilla/FileZillaIntf.h includes <FileZillaTools.h>
// (capital Z); the real file is source/filezilla/FilezillaTools.h (lowercase z). macOS's
// case-insensitive filesystem hides the mismatch; Linux is case-sensitive. No source/ edit.
#include <FilezillaTools.h>
