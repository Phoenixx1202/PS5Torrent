/* Own-process setup following Spectrum Library src/jb.c. No external daemon. */
#include "ps5_jailbreak.h"
#include "ui.h"
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <ps5/kernel.h>

int ps5_request_jailbreak(void)
{
    pid_t pid = getpid();
    if (!kernel_get_proc(pid)) return -1;
    int result = 0;
    if (kernel_set_ucred_uid(pid, 0)) result = -1;
    if (kernel_set_ucred_ruid(pid, 0)) result = -1;
    if (kernel_set_ucred_svuid(pid, 0)) result = -1;
    if (kernel_set_ucred_rgid(pid, 0)) result = -1;
    if (kernel_set_ucred_svgid(pid, 0)) result = -1;
    intptr_t root = kernel_get_root_vnode();
    if (!root) result = -1;
    else {
        if (kernel_set_proc_rootdir(pid, root)) result = -1;
        if (kernel_set_proc_jaildir(pid, root)) result = -1;
    }
    if (kernel_set_ucred_authid(pid, 0x4801000000000013ULL)) result = -1;
    uint8_t caps[16];
    memset(caps, 0xff, sizeof(caps));
    if (kernel_set_ucred_caps(pid, caps)) result = -1;
    /* This SDK takes the complete 32-byte attribute array, not a scalar. */
    uint8_t attrs[32];
    if (kernel_get_ucred_attrs(pid, attrs)) result = -1;
    else {
        attrs[0] |= 0x80;
        if (kernel_set_ucred_attrs(pid, attrs)) result = -1;
    }
    return result;
}
