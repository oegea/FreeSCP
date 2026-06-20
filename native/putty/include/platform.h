/*
 * platform.h — Unix/macOS platform layer for the WinSCP PuTTY (0.83) fork.
 *
 * Supplied on the include path INSTEAD of source/putty/windows/platform.h (no source edit).
 * Provides the same contract the PuTTY core expects from a platform: scalar/struct types,
 * timing, thread-local, and the network platform hooks (do_select/select_result/socket
 * iteration). Backed by BSD sockets + select (impl in native/putty/src). Grown by compiling
 * the putty core leaf-first, like rtlcompat.
 */
#ifndef PUTTY_UNIX_PLATFORM_H
#define PUTTY_UNIX_PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <limits.h>

struct Plug;   /* fwd (network.h) */

/* help context (WinSCP stubs this) */
typedef const char *HelpCtx;
#define NULL_HELPCTX ((HelpCtx)0)
#define HELPCTX(x) NULL_HELPCTX   /* headless: ignore help ids */

#define BUILDINFO_PLATFORM "macOS"
#define THREADLOCAL __thread

/* PuTTY core's clipboard enum needs the platform list; headless has none. */
#define PLATFORM_CLIPBOARDS(X)
#define CLIPUI_DEFAULT_AUTOCOPY 0
#define CLIPUI_DEFAULT_MOUSE 0
#define CLIPUI_DEFAULT_INS 0

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

/* Win synchronization types used by WinSCP's MPEXT callback_set — POSIX-backed.
 * (CRITICAL_SECTION is used as a [1] array member, so it must be a struct type.) */
#include <pthread.h>
typedef struct CRITICAL_SECTION { pthread_mutex_t mtx; int initialised; } CRITICAL_SECTION;
typedef void *HANDLE;
typedef intptr_t INT_PTR; typedef uintptr_t UINT_PTR;
typedef intptr_t LONG_PTR; typedef uintptr_t ULONG_PTR;
typedef UINT_PTR WPARAM; typedef LONG_PTR LPARAM; typedef LONG_PTR LRESULT;
typedef struct tree234_Tag tree234;   /* opaque; matches tree234.h (C11 allows identical redef) */

/* timing — milliseconds */
#define TICKSPERSEC 1000
unsigned long getticktime(void);
#define GETTICKCOUNT getticktime

#define DEFAULT_CODEPAGE 0
#define CP_UTF8 65001          /* used by some core code */

/* A filename is just a UTF-8 path here. */
struct Filename {
    char *path;
};
#define f_open(filename, mode, isprivate) ( fopen((filename)->path, (mode)) )

/* Fonts are irrelevant for headless SFTP. */
struct FontSpec {
    char *name;
};
struct FontSpec *fontspec_new(const char *name);

/* sockets are file descriptors */
typedef int SOCKET;
#define INVALID_SOCKET (-1)

/* Windows FD_* event bits — reused as the event mask passed to select_result(). */
#ifndef FD_READ
#define FD_READ     0x01
#define FD_WRITE    0x02
#define FD_OOB      0x04
#define FD_ACCEPT   0x08
#define FD_CONNECT  0x10
#define FD_CLOSE    0x20
#endif

/* Network platform hooks implemented by native/putty/src/uxnet-winscp.c. */
const char *do_select(struct Plug *plug, SOCKET skt, int enable);  /* WINSCP-style */
/* select_result(WPARAM,LPARAM) is declared by puttyexp.h (Windows-shaped: w=socket, l=events) */
SOCKET first_socket(int *state);
SOCKET next_socket(int *state);
int socket_writable(SOCKET skt);
void socket_reselect_all(void);

/* GSSAPI buffer types (referenced if GSS code is compiled; harmless to declare). */
typedef struct Ssh_gss_buf { size_t length; void *value; } Ssh_gss_buf;
typedef void *Ssh_gss_name;

#endif /* PUTTY_UNIX_PLATFORM_H */
