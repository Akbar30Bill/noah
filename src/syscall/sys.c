#include "common.h"
#include "noah.h"
#include "linux/misc.h"

#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <sys/sysctl.h>

DEFINE_SYSCALL(sysinfo, gaddr_t, info_ptr)
{
  struct l_sysinfo *l_info = guest_to_host(info_ptr);
  size_t len;

  struct timeval boottime;
  len = sizeof boottime;
  if (sysctlbyname("kern.boottime", &boottime, &len, NULL, 0) < 0) exit(1);
  l_info->uptime = boottime.tv_sec;

  double loadavg[3];
  if (getloadavg(loadavg, sizeof loadavg / sizeof loadavg[0]) < 0)
    abort();

  l_info->loads[0] = loadavg[0] * LINUX_SYSINFO_LOADS_SCALE;
  l_info->loads[1] = loadavg[1] * LINUX_SYSINFO_LOADS_SCALE;
  l_info->loads[2] = loadavg[2] * LINUX_SYSINFO_LOADS_SCALE;

  int64_t memsize;
  len = sizeof memsize;
  if (sysctlbyname("hw.memsize", &memsize, &len, NULL, 0) < 0) exit(1);
  l_info->totalram = memsize;

  int64_t freepages;
  len = sizeof freepages;
  if (sysctlbyname("vm.page_free_count", &freepages, &len, NULL, 0) < 0) exit(1);
  l_info->freeram = freepages * 0x1000;

  uint64_t swapinfo[3];
  len = sizeof swapinfo;
  if (sysctlbyname("vm.swapusage", &swapinfo, &len, NULL, 0) < 0) exit(1);
  l_info->totalswap = swapinfo[0];
  l_info->freeswap = swapinfo[2];

  /* TODO */
  l_info->sharedram = 0;
  l_info->bufferram = 0;
  l_info->procs = 100;
  l_info->totalhigh = 0;
  l_info->freehigh = 0;

  l_info->mem_unit = 1;

  return 0;
}
