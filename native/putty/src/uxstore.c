/*
 * uxstore.c — storage/host-key/proxy platform layer for the WinSCP PuTTY fork on Unix.
 *
 * WinSCP supplies session config itself (via Conf), so the settings_r/w API is stubbed.
 * Only the random-seed file is real. Host-key verification is currently a STUB (see caveat)
 * — it must be wired to WinSCP's seat/known_hosts before any non-test use.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>

#include "putty.h"
#include "storage.h"

/* ---- settings: unused (WinSCP passes config via Conf) ---- */
settings_w *open_settings_w(const char *sessionname, char **errmsg)
{ (void)sessionname; if (errmsg) *errmsg = NULL; return NULL; }
void write_setting_s(settings_w *h, const char *key, const char *value) { (void)h; (void)key; (void)value; }
void write_setting_i(settings_w *h, const char *key, int value) { (void)h; (void)key; (void)value; }
void write_setting_filename(settings_w *h, const char *key, Filename *value) { (void)h; (void)key; (void)value; }
void write_setting_fontspec(settings_w *h, const char *key, FontSpec *value) { (void)h; (void)key; (void)value; }
void close_settings_w(settings_w *h) { (void)h; }

settings_r *open_settings_r(const char *sessionname) { (void)sessionname; return NULL; }
char *read_setting_s(settings_r *h, const char *key) { (void)h; (void)key; return NULL; }
int read_setting_i(settings_r *h, const char *key, int defvalue) { (void)h; (void)key; return defvalue; }
Filename *read_setting_filename(settings_r *h, const char *key) { (void)h; (void)key; return NULL; }
FontSpec *read_setting_fontspec(settings_r *h, const char *key) { (void)h; (void)key; return NULL; }
void close_settings_r(settings_r *h) { (void)h; }
void del_settings(const char *sessionname) { (void)sessionname; }

/* ---- host keys ----
 * have_ssh_host_key / enum_host_ca_* / host_ca_load are provided by WinSCP's PuttyIntf.cpp (the
 * real Seat-backed integration), so they are NOT defined here (would duplicate). store_host_key
 * is a no-op placeholder that PuttyIntf does not provide; WinSCP persists known hosts itself. */
void store_host_key(Seat *seat, const char *hostname, int port, const char *keytype, const char *key)
{ (void)seat; (void)hostname; (void)port; (void)keytype; (void)key; }

/* ---- random seed file ---- */
static const char *seed_path(void)
{
    static char path[1024];
    const char *home = getenv("HOME");
    if (!home) { struct passwd *pw = getpwuid(getuid()); home = pw ? pw->pw_dir : "/tmp"; }
    snprintf(path, sizeof(path), "%s/.winscp-native-seed", home);
    return path;
}
void write_random_seed(void *data, int len)
{
    FILE *f = fopen(seed_path(), "wb");
    if (f) { fwrite(data, 1, (size_t)len, f); fclose(f); }
}
void read_random_seed(noise_consumer_t consumer)
{
    FILE *f = fopen(seed_path(), "rb");
    if (!f) return;
    unsigned char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) consumer(buf, (int)n);
    fclose(f);
}

/* ---- local proxy / subprocess: not supported on the native port (yet) ---- */
char *platform_setup_local_proxy(Socket *socket, const char *cmd)
{ (void)socket; (void)cmd; return dupstr("local proxy not supported"); }
Socket *platform_start_subprocess(const char *cmd, Plug *plug, const char *prefix)
{ (void)cmd; (void)plug; (void)prefix; return NULL; }
