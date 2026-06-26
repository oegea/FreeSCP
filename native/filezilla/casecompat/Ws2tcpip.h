// Case-exact forwarding shim. source/filezilla includes <Ws2tcpip.h> (capital W); the real
// compat header is winapi/ws2tcpip.h (lowercase). macOS's case-insensitive filesystem resolves
// the mismatch silently; Linux is case-sensitive, so we forward here. No source/ edit.
#include <ws2tcpip.h>
