/*
 * uxhandlewait.c — unix stub for PuTTY's Windows handle-wait API (windows/handle-wait.c).
 *
 * On Windows, PuTTY waits on OS HANDLEs (pipes, processes, sockets) via
 * WaitForMultipleObjects. On unix there are no such waitable handles in WinSCP's engine path —
 * sockets are serviced by select() (uxnet/winscp_net_select) — so the wait list is always
 * empty and add/delete are no-ops. SecureShell's EventSelectLoop consequently waits only on its
 * own socket event.
 */
#include <stdlib.h>
#include "putty.h"

HandleWait *add_handle_wait(struct callback_set *callback_set, HANDLE h,
                            handle_wait_callback_fn_t callback, void *callback_ctx)
{
    (void)callback_set; (void)h; (void)callback; (void)callback_ctx;
    return NULL;
}

void delete_handle_wait(struct callback_set *callback_set, HandleWait *hw)
{
    (void)callback_set; (void)hw;
}

HandleWaitList *get_handle_wait_list(struct callback_set *callback_set)
{
    HandleWaitList *hwl = (HandleWaitList *)calloc(1, sizeof(HandleWaitList));
    (void)callback_set;
    if (hwl) hwl->nhandles = 0;
    return hwl;
}

int handle_wait_activate(struct callback_set *callback_set, HandleWaitList *hwl, int index)
{
    (void)callback_set; (void)hwl; (void)index;
    return 0;
}

void handle_wait_list_free(HandleWaitList *hwl)
{
    free(hwl);
}
