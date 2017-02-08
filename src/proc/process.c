#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <pthread.h>

#include "common.h"
#include "noah.h"
#include "vmm.h"
#include "mm.h"

#include "linux/common.h"
#include "linux/misc.h"
#include "linux/errno.h"

struct proc proc;
_Thread_local struct task task;

DEFINE_SYSCALL(sched_yield)
{
  sleep(0);
  return 0;
}

DEFINE_SYSCALL(getpid)
{
  return syswrap(getpid());
}

DEFINE_SYSCALL(getuid)
{
  return syswrap(getuid());
}

DEFINE_SYSCALL(getgid)
{
  return syswrap(getgid());
}

DEFINE_SYSCALL(setuid, l_uid_t, uid)
{
  return syswrap(setuid(uid));
}

DEFINE_SYSCALL(setgid, l_gid_t, gid)
{
  return syswrap(setgid(gid));
}

DEFINE_SYSCALL(geteuid)
{
  return syswrap(geteuid());
}

DEFINE_SYSCALL(getegid)
{
  return syswrap(getegid());
}

DEFINE_SYSCALL(setpgid, pid_t, pid, pid_t, pgid)
{
  return syswrap(setpgid(pid, pgid));
}

DEFINE_SYSCALL(getppid)
{
  return syswrap(getppid());
}

DEFINE_SYSCALL(getpgrp)
{
  return syswrap(getpgrp());
}

DEFINE_SYSCALL(getpgid, l_pid_t, pid)
{
  return syswrap(getpgid(pid));
}

DEFINE_SYSCALL(getsid, l_pid_t, pid)
{
  return syswrap(getsid(pid));
}

DEFINE_SYSCALL(getgroups, int, gidsetsize, gaddr_t, grouplist_ptr)
{
  gid_t gl[gidsetsize];
  int r = syswrap(getgroups(gidsetsize, gl));
  if (r < 0)
    return r;
  if (copy_to_user(grouplist_ptr, gl, sizeof gl))
    return -LINUX_EFAULT;
  return r;
}

DEFINE_SYSCALL(setgroups, int, gidsetsize, gaddr_t, grouplist_ptr)
{
  gid_t gl[gidsetsize];
  if (copy_from_user(gl, grouplist_ptr, sizeof gl))
    return -LINUX_EFAULT;
  return syswrap(setgroups(gidsetsize, gl));
}

DEFINE_SYSCALL(setresuid, l_uid_t, ruid, l_uid_t, euid, l_uid_t, suid)
{
  return syswrap(setreuid(ruid, euid));
}

DEFINE_SYSCALL(getresuid, gaddr_t, ruid, gaddr_t, euid, gaddr_t, suid)
{
  int n;
  n = getuid();
  if (copy_to_user(ruid, &n, sizeof n)) {
    return -LINUX_EFAULT;
  }
  n = geteuid();
  if (copy_to_user(euid, &n, sizeof n)) {
    return -LINUX_EFAULT;
  }
  n = getuid();
  if (copy_to_user(suid, &n, sizeof n)) {
    return -LINUX_EFAULT;
  }
  return 0;
}

DEFINE_SYSCALL(setresgid, l_gid_t, rgid, l_gid_t, egid, l_gid_t, sgid)
{
  return syswrap(setregid(rgid, egid));
}

DEFINE_SYSCALL(getresgid, gaddr_t, rgid, gaddr_t, egid, gaddr_t, sgid)
{
  int n;
  n = getgid();
  if (copy_to_user(rgid, &n, sizeof n)) {
    return -LINUX_EFAULT;
  }
  n = getegid();
  if (copy_to_user(egid, &n, sizeof n)) {
    return -LINUX_EFAULT;
  }
  n = getgid();
  if (copy_to_user(sgid, &n, sizeof n)) {
    return -LINUX_EFAULT;
  }
  return 0;
}

DEFINE_SYSCALL(gettid)
{
  uint64_t tid;
  pthread_threadid_np(NULL, &tid);
  return tid;
}

DEFINE_SYSCALL(setsid)
{
  return syswrap(setsid());
}

DEFINE_SYSCALL(getrlimit, int, l_resource, gaddr_t, rl_ptr)
{
  struct rlimit rl;
  struct l_rlimit l_rl;

  int resource = 0;
  switch (l_resource) {
  case LINUX_RLIMIT_CPU: resource = RLIMIT_CPU; break;
  case LINUX_RLIMIT_FSIZE: resource = RLIMIT_FSIZE; break;
  case LINUX_RLIMIT_DATA: resource = RLIMIT_DATA; break;
  case LINUX_RLIMIT_STACK: resource = RLIMIT_STACK; break;
  case LINUX_RLIMIT_CORE: resource = RLIMIT_CORE; break;
  case LINUX_RLIMIT_RSS: resource = RLIMIT_RSS; break;
  case LINUX_RLIMIT_NPROC: resource = RLIMIT_NPROC; break;
  case LINUX_RLIMIT_NOFILE: resource = RLIMIT_NOFILE; break;
  case LINUX_RLIMIT_MEMLOCK: resource = RLIMIT_MEMLOCK; break;
  case LINUX_RLIMIT_AS: resource = RLIMIT_AS; break;
  }

  int r = syswrap(getrlimit(resource, &rl));
  if (r < 0)
    return r;

  darwin_to_linux_rlimit(resource, &rl, &l_rl);
  if (copy_to_user(rl_ptr, &l_rl, sizeof l_rl))
    return -LINUX_EFAULT;

  return r;
}

DEFINE_SYSCALL(setrlimit, unsigned int, resource, gaddr_t, rlim)
{
  warnk("setrlimit is not implemented\n");
  return -LINUX_ENOSYS;
}

DEFINE_SYSCALL(exit, int, reason)
{
  if (task.clear_child_tid) {
    long zero = 0;
    if (copy_to_user(task.clear_child_tid, &zero, sizeof task.clear_child_tid))
      return -LINUX_EFAULT;
    do_futex_wake(task.clear_child_tid, 1);
  }
  vmm_destroy_vcpu();
  pthread_rwlock_wrlock(&proc.lock);
  if (proc.nr_tasks == 1) {
    _exit(reason);
  } else {
    proc.nr_tasks--;
    list_del(&task.head);
    pthread_rwlock_unlock(&proc.lock);
    pthread_exit(&reason);
  }
}

DEFINE_SYSCALL(exit_group, int, reason)
{
  if (task.clear_child_tid) {
    long zero = 0;
    if (copy_to_user(task.clear_child_tid, &zero, sizeof task.clear_child_tid))
      return -LINUX_EFAULT;
    do_futex_wake(task.clear_child_tid, 1);
  }
  _exit(reason);
}

DEFINE_SYSCALL(tgkill)
{
  printk("unimplemented syscall: tgkill\n");
  return -LINUX_ENOSYS;
}

DEFINE_SYSCALL(capget, gaddr_t, header_ptr, gaddr_t, data_ptr)
{
  printk("capget is unimplemented\n");
  return 0;
}

struct utsname {
  char sysname[65];
  char nodename[65];
  char release[65];
  char version[65];
  char machine[65];
  char domainname[65];
};

DEFINE_SYSCALL(uname, gaddr_t, buf_ptr)
{
  struct utsname buf;

  strncpy(buf.sysname, "Linux", sizeof buf.sysname - 1);
  strncpy(buf.release, LINUX_RELEASE, sizeof buf.release - 1);
  strncpy(buf.version, LINUX_VERSION, sizeof buf.version - 1);
  strncpy(buf.machine, "x86_64", sizeof buf.machine - 1);
  strncpy(buf.domainname, "GNU/Linux", sizeof buf.domainname - 1);

  int err = syswrap(gethostname(buf.nodename, sizeof buf.nodename - 1));
  if (err < 0) {
    return err;
  }

  if (copy_to_user(buf_ptr, &buf, sizeof(struct utsname))) {
    return -LINUX_EFAULT;
  }

  return 0;
}

DEFINE_SYSCALL(prctl, int, option)
{
  /* FIXME */
  printk("prctl is not implemented yet\n");
  return -EINVAL;
}

DEFINE_SYSCALL(arch_prctl, int, code, gaddr_t, addr)
{
  uint64_t t;

  switch (code) {
  case LINUX_ARCH_SET_GS:
    vmm_write_vmcs(VMCS_GUEST_GS_BASE, addr);
    return 0;
  case LINUX_ARCH_SET_FS:
    vmm_write_vmcs(VMCS_GUEST_FS_BASE, addr);
    return 0;
  case LINUX_ARCH_GET_FS:
    vmm_read_vmcs(VMCS_GUEST_FS_BASE, &t);
    if (copy_to_user(addr, &t, sizeof t))
      return -LINUX_EFAULT;
    return 0;
  case LINUX_ARCH_GET_GS:
    vmm_read_vmcs(VMCS_GUEST_GS_BASE, &t);
    if (copy_to_user(addr, &t, sizeof t))
      return -LINUX_EFAULT;
    return 0;
  default:
    return -LINUX_EINVAL;
  }
}

DEFINE_SYSCALL(set_tid_address, gaddr_t, tidptr)
{
  task.clear_child_tid = tidptr;
  return sys_gettid();
}

DEFINE_SYSCALL(set_robust_list, gaddr_t, head, size_t, len)
{
  return 0;
}

int linux_to_darwin_waitopts(int options)
{
  int opts = 0;
  if (options & LINUX_WNOHANG) {
    opts |= WNOHANG;
    options &= ~LINUX_WNOHANG;
  }
  if (options & LINUX_WUNTRACED) {
    opts |= WUNTRACED;
    options &= ~LINUX_WUNTRACED;
  }
  if (options != 0) {
    warnk("unknown options given to wait4: 0x%x\n", options);
  }
  return opts;
}

DEFINE_SYSCALL(wait4, int, pid, gaddr_t, status_ptr, int, options, gaddr_t, rusage_ptr)
{
  int status;
  struct rusage rusage;

  int ret = syswrap(wait4(pid, &status, linux_to_darwin_waitopts(options), &rusage));
  if (ret < 0) {
    return ret;
  }

  if (rusage_ptr != 0) {
    if (copy_to_user(rusage_ptr, &rusage, sizeof rusage)) {
      return -LINUX_EFAULT;
    }
  }

  if (status_ptr != 0) {
    /*
     *  SSSS SSSS -000 0000 -> exited. S is return status
     *  .... .... CYYY YYYY -> signaled if Y != 0. Y is the signal number
     *  SSSS SSSS 0111 1111 -> stopped. S is the sigal number
     */
    int st = 0;
    if (WIFEXITED(status)) {
      st = WEXITSTATUS(status) << 8;
    } else if (WIFSIGNALED(status)) {
      st = darwin_to_linux_signal(WTERMSIG(status));
      if (WCOREDUMP(status))
        st |= 0x80;
    } else if (WIFSTOPPED(status)) {
      st = (darwin_to_linux_signal(WSTOPSIG(status)) << 8) | 0x7f;
    }
    if (copy_to_user(status_ptr, &st, sizeof st)) {
      return -LINUX_EFAULT;
    }
  }

  return ret;
}

static void
darwin_to_linux_rusage(struct rusage *ru, struct l_rusage *lru)
{
  lru->ru_utime.tv_sec = ru->ru_utime.tv_sec;
  lru->ru_utime.tv_usec = ru->ru_utime.tv_usec;
  lru->ru_stime.tv_sec = ru->ru_stime.tv_sec;
  lru->ru_stime.tv_usec = ru->ru_stime.tv_usec;
  lru->ru_maxrss = ru->ru_maxrss;
  lru->ru_ixrss = ru->ru_ixrss;
  lru->ru_idrss = ru->ru_idrss;
  lru->ru_isrss = ru->ru_isrss;
  lru->ru_minflt = ru->ru_minflt;
  lru->ru_majflt = ru->ru_majflt;
  lru->ru_nswap = ru->ru_nswap;
  lru->ru_inblock = ru->ru_inblock;
  lru->ru_oublock = ru->ru_oublock;
  lru->ru_msgsnd = ru->ru_msgsnd;
  lru->ru_msgrcv = ru->ru_msgrcv;
  lru->ru_nsignals = ru->ru_nsignals;
  lru->ru_nvcsw = ru->ru_nvcsw;
  lru->ru_nivcsw = ru->ru_nivcsw;
}

DEFINE_SYSCALL(getrusage, int, who, gaddr_t, rusage_ptr)
{
  struct rusage h_rusage;
  int r;
  if ((r = syswrap(getrusage(who, &h_rusage))) < 0) {
    return r;
  }

  struct l_rusage l_rusage;
  darwin_to_linux_rusage(&h_rusage, &l_rusage);

  if (copy_to_user(rusage_ptr, &l_rusage, sizeof l_rusage)) {
    return -LINUX_EFAULT;
  }
  return 0;
}

DEFINE_SYSCALL(getpriority, int, which, int, who)
{
  return syswrap(getpriority(which, who));
}

DEFINE_SYSCALL(setpriority, int, which, int, who, int, niceval)
{
  return syswrap(setpriority(which, who, niceval));
}

DEFINE_SYSCALL(sched_getaffinity, l_pid_t, pid, unsigned int, len, gaddr_t, user_mask_ptr)
{
  static const unsigned sizeof_cpumask_t = 32; /* FIXME */
  if (len < sizeof_cpumask_t)
    return -LINUX_EINVAL;
  unsigned char buf[sizeof_cpumask_t] = {0};
  buf[0] = 0x1;
  if (copy_to_user(user_mask_ptr, buf, sizeof buf))
    return -LINUX_EFAULT;
  return sizeof_cpumask_t;
}
