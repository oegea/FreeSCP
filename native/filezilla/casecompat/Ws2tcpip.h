// Case-exact forwarding shim: source/filezilla includes <Ws2tcpip.h>, the real compat header is winapi/ws2tcpip.h.
// Explicit relative path so a case-insensitive FS (macOS) cannot resolve it back to this shim.
#include "../../rtlcompat/include/winapi/ws2tcpip.h"
