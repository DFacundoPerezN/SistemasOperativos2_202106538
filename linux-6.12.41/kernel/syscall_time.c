#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/timekeeping.h>

SYSCALL_DEFINE0(uptime_s) {
    unsigned long uptime_s = ktime_get_boottime_seconds();
    return uptime_s;
}