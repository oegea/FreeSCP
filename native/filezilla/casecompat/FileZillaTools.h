// Case-exact forwarding shim: source includes <FileZillaTools.h>, the real file is FilezillaTools.h.
// Explicit relative path (not <...>): on a case-insensitive FS (macOS) a <> include would find this shim again and recurse.
#include "../../../source/filezilla/FilezillaTools.h"
