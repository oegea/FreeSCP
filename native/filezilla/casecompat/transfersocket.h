// Case-exact forwarding shim (Linux is case-sensitive). Explicit relative path so a case-insensitive FS (macOS) cannot resolve it back to this shim.
#include "../../../source/filezilla/TransferSocket.h"
