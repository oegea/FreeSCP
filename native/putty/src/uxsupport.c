/*
 * uxsupport.c — small Unix/macOS platform pieces for the WinSCP PuTTY fork:
 * timing, entropy/noise, username, platform defaults, critical sections, Filename/FontSpec.
 * (Network + select are in uxnet.c / uxsel.c; storage in uxstore.c.)
 */
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pwd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netdb.h>

#include "putty.h"
#include "marshal.h"

/* ---- timing ---- */
unsigned long getticktime(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

uint64_t prng_reseed_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ---- entropy ---- */
static int read_urandom(void *buf, int len)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return 0;
    int got = 0;
    while (got < len) {
        ssize_t n = read(fd, (char *)buf + got, len - got);
        if (n <= 0) break;
        got += (int)n;
    }
    close(fd);
    return got == len;
}

void noise_get_heavy(void (*func)(void *, int))
{
    struct rusage ru; getrusage(RUSAGE_SELF, &ru); func(&ru, sizeof(ru));
    pid_t pid = getpid(); func(&pid, sizeof(pid));
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts); func(&ts, sizeof(ts));
    clock_gettime(CLOCK_MONOTONIC, &ts); func(&ts, sizeof(ts));
    unsigned char buf[64];
    if (read_urandom(buf, sizeof(buf))) { func(buf, sizeof(buf)); memset(buf, 0, sizeof(buf)); }
}
void noise_get_light(void (*func)(void *, int))
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); func(&ts, sizeof(ts));
}
void noise_regular(void) {}
void noise_ultralight(NoiseSourceId id, unsigned long data) { (void)id; (void)data; }

/* ---- user / service ---- */
char *get_username(void)
{
    const char *e = getenv("USER");
    if (!e || !*e) { struct passwd *pw = getpwuid(getuid()); e = pw ? pw->pw_name : "user"; }
    return dupstr(e);
}

int net_service_lookup(const char *service)
{
    struct servent *se = getservbyname(service, NULL);
    return se ? ntohs(se->s_port) : 0;
}

/* ---- platform defaults (no registry; engine supplies real config) ---- */
char *platform_default_s(const char *name) { (void)name; return NULL; }
bool platform_default_b(const char *name, bool def) { (void)name; return def; }
int platform_default_i(const char *name, int def) { (void)name; return def; }
Filename *platform_default_filename(const char *name) { (void)name; return filename_from_str(""); }
FontSpec *platform_default_fontspec(const char *name) { (void)name; return fontspec_new(""); }

/* ---- critical sections (Win API over pthread) ---- */
void InitializeCriticalSection(CRITICAL_SECTION *cs)
{
    pthread_mutexattr_t a; pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&cs->mtx, &a); pthread_mutexattr_destroy(&a);
    cs->initialised = 1;
}
void DeleteCriticalSection(CRITICAL_SECTION *cs) { if (cs->initialised) pthread_mutex_destroy(&cs->mtx); }
void EnterCriticalSection(CRITICAL_SECTION *cs) { if (!cs->initialised) InitializeCriticalSection(cs); pthread_mutex_lock(&cs->mtx); }
void LeaveCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_unlock(&cs->mtx); }

/* ---- Filename (UTF-8 path) ---- */
Filename *filename_from_str(const char *str)
{
    Filename *fn = snew(Filename);
    fn->path = dupstr(str ? str : "");
    return fn;
}
const char *filename_to_str(const Filename *fn) { return fn->path; }
bool filename_is_null(const Filename *fn) { return !fn->path || !*fn->path; }
Filename *filename_copy(const Filename *fn) { return filename_from_str(fn ? fn->path : ""); }
void filename_free(Filename *fn) { if (fn) { sfree(fn->path); sfree(fn); } }
void filename_serialise(BinarySink *bs, const Filename *f) { put_asciz(bs, f->path ? f->path : ""); }
Filename *filename_deserialise(BinarySource *src) { return filename_from_str(get_asciz(src)); }

/* ---- FontSpec (irrelevant headless) ---- */
FontSpec *fontspec_new(const char *name)
{
    FontSpec *fs = snew(FontSpec);
    fs->name = dupstr(name ? name : "");
    return fs;
}
FontSpec *fontspec_copy(const FontSpec *f) { return fontspec_new(f ? f->name : ""); }
void fontspec_free(FontSpec *f) { if (f) { sfree(f->name); sfree(f); } }
void fontspec_serialise(BinarySink *bs, FontSpec *f) { put_asciz(bs, f->name ? f->name : ""); }
FontSpec *fontspec_deserialise(BinarySource *src) { return fontspec_new(get_asciz(src)); }
