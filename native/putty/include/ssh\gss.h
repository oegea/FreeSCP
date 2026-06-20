/* Minimal GSSAPI stub for the native port (NO_GSSAPI runtime): enough for HasGSSAPI() etc to
   compile and report "no GSS libraries". */
#ifndef WINSCP_NATIVE_SSH_GSS_STUB_H
#define WINSCP_NATIVE_SSH_GSS_STUB_H
typedef void *Ssh_gss_ctx;
typedef void *Ssh_gss_name;
#define SSH_GSS_OK 0
#define SSH_GSS_FAILURE 1
struct ssh_gss_library;
typedef struct ssh_gss_library {
    int (*acquire_cred)(struct ssh_gss_library *, Ssh_gss_ctx *, void *);
    int (*release_cred)(struct ssh_gss_library *, Ssh_gss_ctx *);
    const char *gsslogmsg;
    const char *name;
} ssh_gss_library;
typedef struct ssh_gss_liblist { int nlibraries; ssh_gss_library *libraries; } ssh_gss_liblist;
struct Conf;
ssh_gss_liblist *ssh_gss_setup(struct Conf *conf, void *unused);
void ssh_gss_cleanup(ssh_gss_liblist *list);
#endif
