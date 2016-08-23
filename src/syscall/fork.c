#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "common.h"
#include "noah.h"

DEFINE_SYSCALL(fork)
{
  // Because Apple Hypervisor Framwork won't let us use multiple VMs,
  // we destroy the current vm and restore it later
  vmm_snapshot();
  vmm_destroy();

  pid_t pid = fork();
  if (pid > 0) {
    PRINTF("fork parent, pid:%d\n", getpid());
    vmm_restore();
    return pid;
  } else if (pid == 0) {
    PRINTF("fork child, pid:%d\n", getpid());
    vmm_restore();
    return 0;
  } else {
    fprintf(stderr, "fork failed");
    exit(1);
  }
}
