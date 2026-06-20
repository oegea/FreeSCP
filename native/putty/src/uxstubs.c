/*
 * uxstubs.c — small platform stubs/shims for the WinSCP PuTTY fork on Unix/macOS:
 * case-insensitive str fns, minimal charset (UTF-8), no SSH-agent, no special proxy.
 */
#include <strings.h>
#include <stdlib.h>
#include <string.h>

#include "putty.h"
#include "marshal.h"
#include "network.h"

/* Windows case-insensitive string fns */
int stricmp(const char *a, const char *b) { return strcasecmp(a, b); }
int strnicmp(const char *a, const char *b, size_t n) { return strncasecmp(a, b, n); }

/* Charset conversion (codepage ignored; treat as UTF-8/passthrough). Enough for SFTP
   filenames; a full codec can replace this later. wchar_t is the platform width. */
bool BinarySink_put_mb_to_wc(BinarySink *bs, int codepage, const char *mbstr, int mblen)
{
    (void)codepage;
    for (int i = 0; i < mblen; i++) {
        wchar_t wc = (unsigned char)mbstr[i];
        put_data(bs, &wc, sizeof(wc));
    }
    return true;
}
bool BinarySink_put_wc_to_mb(BinarySink *bs, int codepage, const wchar_t *wcstr,
                             int wclen, const char *defchr)
{
    (void)codepage; (void)defchr;
    for (int i = 0; i < wclen; i++) {
        unsigned int cp = (unsigned int)wcstr[i];
        char buf[4]; int n;
        if (cp < 0x80) { buf[0] = (char)cp; n = 1; }
        else if (cp < 0x800) { buf[0] = 0xC0 | (cp >> 6); buf[1] = 0x80 | (cp & 0x3F); n = 2; }
        else if (cp < 0x10000) { buf[0] = 0xE0 | (cp >> 12); buf[1] = 0x80 | ((cp >> 6) & 0x3F); buf[2] = 0x80 | (cp & 0x3F); n = 3; }
        else { buf[0] = 0xF0 | (cp >> 18); buf[1] = 0x80 | ((cp >> 12) & 0x3F); buf[2] = 0x80 | ((cp >> 6) & 0x3F); buf[3] = 0x80 | (cp & 0x3F); n = 4; }
        put_data(bs, buf, n);
    }
    return true;
}

/* SSH agent — not supported on the native port (yet). */
agent_pending_query *agent_query(strbuf *in, void **out, int *outlen,
                                 void (*callback)(void *, void *, int), void *cctx,
                                 struct callback_set *cs)
{ (void)in; (void)callback; (void)cctx; (void)cs; if (out) *out = NULL; if (outlen) *outlen = 0; return NULL; }
void agent_cancel_query(agent_pending_query *q) { (void)q; }
bool agent_exists(void) { return false; }
Socket *agent_connect(Plug *plug) { (void)plug; return NULL; }

/* No platform-specific proxy/subprocess: fall through to a normal direct connection. */
Socket *platform_new_connection(SockAddr *addr, const char *hostname, int port,
                                bool privport, bool oobinline, bool nodelay, bool keepalive,
                                Plug *plug, Conf *conf, Interactor *itr)
{ (void)addr; (void)hostname; (void)port; (void)privport; (void)oobinline; (void)nodelay;
  (void)keepalive; (void)plug; (void)conf; (void)itr; return NULL; }

/* GSSAPI disabled: report no libraries. */
#include "ssh\gss.h"
ssh_gss_liblist *ssh_gss_setup(struct Conf *conf, void *unused)
{ (void)conf; (void)unused; static ssh_gss_liblist empty = { 0, 0 }; return &empty; }
void ssh_gss_cleanup(ssh_gss_liblist *list) { (void)list; }

/* filename from UTF-8 (engine passes UTF-8 paths) */
Filename *filename_from_utf8(const char *utf8) { return filename_from_str(utf8); }
