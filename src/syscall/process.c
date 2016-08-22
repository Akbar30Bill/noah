#include "common.h"

#include "noah.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

DEFINE_SYSCALL(getpid)
{
  return getpid();
}

DEFINE_SYSCALL(getuid)
{
  return 0;
}

DEFINE_SYSCALL(getgid)
{
  return 0;
}

DEFINE_SYSCALL(geteuid)
{
  return geteuid();
}

DEFINE_SYSCALL(getegid)
{
  return 0;
}

DEFINE_SYSCALL(getppid)
{
  return getppid();
}

DEFINE_SYSCALL(exit, int, reason)
{
  _exit(reason);
}

DEFINE_SYSCALL(exit_group, int, reason)
{
  _exit(reason);
}

struct utsname {
  char sysname[65];
  char nodename[65];
  char release[65];
  char version[65];
  char machine[65];
  char domainname[65];
};

DEFINE_SYSCALL(uname, gaddr_t, buf)
{
  struct utsname *_buf = guest_to_host(buf);

  strncpy(_buf->sysname, "Linux", sizeof _buf->sysname - 1);
  strncpy(_buf->release, LINUX_RELEASE, sizeof _buf->release - 1);
  strncpy(_buf->version, LINUX_VERSION, sizeof _buf->version - 1);
  strncpy(_buf->machine, "x86_64", sizeof _buf->machine - 1);
  strncpy(_buf->domainname, "GNU/Linux", sizeof _buf->domainname - 1);

  if (gethostname(_buf->nodename, sizeof _buf->nodename - 1) < 0) {
    return -1;
  }
  return 0;
}

#define ARCH_SET_GS 0x1001
#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003
#define ARCH_GET_GS 0x1004

DEFINE_SYSCALL(arch_prctl, int, code, uint64_t, addr)
{
  switch (code) {
  case ARCH_SET_GS:
    hv_vmx_vcpu_write_vmcs(vcpuid, VMCS_GUEST_GS_BASE, addr);
    return 0;
  case ARCH_SET_FS:
    hv_vmx_vcpu_write_vmcs(vcpuid, VMCS_GUEST_FS_BASE, addr);
    return 0;
  case ARCH_GET_FS:
  case ARCH_GET_GS:
  default:
    return -1;
  }
}

DEFINE_SYSCALL(set_tid_address, int *, tidptr)
{
  return 0;
}
