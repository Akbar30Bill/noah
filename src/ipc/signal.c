#include "common.h"
#include "linux/signal.h"

#include "noah.h"
#include "vmm.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>
#include <stdatomic.h>

_Thread_local atomic_sigbits_t task_sigpending;  // sigpending cannot be inside task struct because thread local variables referred by signal handler should be atomic type

void
set_sigpending(int signum)
{
  int l_signum = darwin_to_linux_signal(signum);
  // locking proc structure also blcoks signals, so don't need to acquire rdlock of proc
  sigbits_addbit(&task_sigpending, l_signum);
}

void
init_signal(struct proc *proc)
{
#ifndef ATOMIC_INT_LOCK_FREE // Workaround of the incorrect atomic macro name bug of Clang
#define __GCC_ATOMIC_INT_T_LOCK_FREE __GCC_ATOMIC_INT_LOCK_FREE
#define ATOMIC_INT_LOCK_FREE ATOMIC_INT_T_LOCK_FREE
#endif
  static_assert(ATOMIC_INT_LOCK_FREE == 2, "The compiler must support lock-free atomic int");

  for (int i = 0; i < NSIG; i++) {
    struct sigaction oact;
    sigaction(i + 1, NULL, &oact);
    if (!(oact.sa_handler == SIG_IGN || oact.sa_handler == SIG_DFL)) {
      fprintf(stderr, "sa_handler:%d\n", (int)oact.sa_handler);
    }
    assert(oact.sa_handler == SIG_IGN || oact.sa_handler == SIG_DFL);
    // flags, restorer, and mask will be flushed in execve, so just leave them 0
    proc->sighand.sigaction[i] = (l_sigaction_t) {
      .lsa_handler = (gaddr_t) oact.sa_handler,
      .lsa_flags = 0,
      .lsa_restorer= 0,
      .lsa_mask = {0}
    };
  }
  assert(proc->nr_tasks == 1);
  struct task *t = list_entry(proc->tasks.next, struct task, tasks);
  sigset_t set;
  sigprocmask(0, NULL, &set);
  darwin_to_linux_sigset(&set, &t->sigmask);
  t->sigpending = &task_sigpending; //TODO
  sigbits_emptyset(t->sigpending);
  sigpending(&set);
  darwin_to_linux_sigset(&set, &proc->sigpending);
}

static inline int
should_deliver(int sig)
{
  if (sig == 0) {
    return 0;
  }
  return (1 << (sig - 1)) & ~LINUX_SIGSET_TO_UI64(&task.sigmask);
}

static inline int
get_procsig_to_deliver(bool unsets)
{
  uint64_t pending;
  if ((pending = LINUX_SIGSET_TO_UI64(&proc.sigpending)) == 0) {
    return 0;
  }
  int sig = 0;
  while (sig <= 32) {
    if (((pending >> sig++) & 1) == 0)
      continue;
    if (should_deliver(sig)) {
      if (unsets) {
        LINUX_SIGDELSET(&proc.sigpending, sig);
      }
      return sig;
    }
  }
  return 0;
}

static inline int
get_tasksig_to_deliver(bool unsets)
{
  uint64_t task_sig, sig;

retry:
  if ((task_sig = *task.sigpending) == 0) {
    return 0;
  }

  sig = 0;
  while (sig <= 32) {
    if (((task_sig >> sig++) & 1) == 0)
      continue;

    if (should_deliver(sig)) {
      if (unsets) {
        uint64_t prev = sigbits_delbit(task.sigpending, sig);
        if (!(prev & (1 << (sig - 1))))
          goto retry;
      }
      return sig;
    }
  }
  return 0;
}

int
get_sig_to_deliver()
{
  int sig = get_procsig_to_deliver(false);
  if (sig) {
    return sig;
  }
  return get_tasksig_to_deliver(false);
}

static const struct retcode {
  uint16_t poplmovl;
  uint32_t nr_sigreturn;
  uint64_t syscall;
} __attribute__((packed)) retcode_bin = {
  0xb858, // popl %eax; movl $..., %eax
  NR_rt_sigreturn,
  0x0f05, // syscall
};

struct sigcontext {
  uint64_t vcpu_reg[NR_X86_REG_LIST]; //temporarily, FIXME
  int signum;
  l_sigset_t oldmask;
};

struct ucontext {
  struct sigcontext sigcontext;
};

struct sigframe {
  gaddr_t pretcode;
  struct retcode retcode;
  struct ucontext ucontext;
  // siginfo
};

int
setup_sigframe(int signum)
{
  int err = 0;
  struct sigframe frame;

  printf("setup_sigframe!\n");

  assert(signum <= LINUX_NSIG);
  assert(is_aligned(sizeof frame, sizeof(uint64_t)));
  assert(is_aligned(offsetof(struct sigframe, retcode), sizeof(uint64_t)));

  uint64_t rsp;
  vmm_read_register(HV_X86_RSP, &rsp);
  rsp -= sizeof frame;
  vmm_write_register(HV_X86_RSP, rsp);

  /* Setup sigframe */
  if (proc.sighand.sigaction[signum - 1].lsa_flags & LINUX_SA_RESTORER) {
    frame.pretcode = (gaddr_t) proc.sighand.sigaction[signum - 1].lsa_restorer;
  } else {
    frame.pretcode = rsp + offsetof(struct sigframe, retcode);
  }
  frame.retcode = retcode_bin;


  /* Setup sigcontext */
  for (uint64_t i = 0; i < NR_X86_REG_LIST; i++) {
    if (x86_reg_list[i] == HV_X86_IDT_BASE) {
      break;
    }
    //TODO: save some segment related regs
    //TODO: save FPU state
    vmm_read_register(x86_reg_list[i], &frame.ucontext.sigcontext.vcpu_reg[i]);
  }

  sigset_t dset;
  frame.ucontext.sigcontext.oldmask = task.sigmask;
  l_sigset_t newmask = proc.sighand.sigaction[signum - 1].lsa_mask;
  LINUX_SIGADDSET(&newmask, signum);
  task.sigmask = newmask;
  linux_to_darwin_sigset(&newmask, &dset);
  sigprocmask(SIG_SETMASK, &dset, NULL);

  frame.ucontext.sigcontext.signum = signum;

  /* OK, push them then... */
  if (copy_to_user(rsp, &frame, sizeof frame)) {
    err = -LINUX_EFAULT;
    goto error;
  }

  /* Setup signals */
  vmm_write_register(HV_X86_RDI, signum);
  vmm_write_register(HV_X86_RSI, 0); // TODO: siginfo
  vmm_write_register(HV_X86_RDI, 0); // TODO: ucontext

  vmm_write_register(HV_X86_RAX, 0);
  printf("handler:%llx\n", proc.sighand.sigaction[signum - 1].lsa_handler);
  vmm_write_register(HV_X86_RIP, proc.sighand.sigaction[signum - 1].lsa_handler);

  return 0;

error:
  task.sigmask = frame.ucontext.sigcontext.oldmask;
  linux_to_darwin_sigset(&task.sigmask, &dset);
  sigprocmask(SIG_SETMASK, &dset, NULL);

  return err;
}

void
deliver_signal()
{
  int sig;

  pthread_rwlock_wrlock(&proc.lock);
  sig = get_procsig_to_deliver(true);
  if (sig) {
    if (setup_sigframe(sig) < 0) {
      LINUX_SIGADDSET(&proc.sigpending, sig);
      sig = 0;
    }
  }
  pthread_rwlock_unlock(&proc.lock);

  if (sig) {
    return;
  }

  sig = get_tasksig_to_deliver(true);
  if (sig) {
    if (setup_sigframe(sig) < 0) {
      sigbits_addbit(task.sigpending, sig);
    }
  }
}

DEFINE_SYSCALL(alarm, unsigned int, seconds)
{
  assert(seconds == 0);
  return 0;
}

inline void
sigbits_emptyset(atomic_sigbits_t *sigbits)
{
  *sigbits = ATOMIC_VAR_INIT(0);
}

inline int
sigbits_ismember(atomic_sigbits_t *sigbits, int sig)
{
  return *sigbits & (1UL << (sig - 1));
}

inline uint64_t
sigbits_addbit(atomic_sigbits_t *sigbits, int sig)
{
  return atomic_fetch_or(sigbits, (1UL << (sig - 1)));
}

inline uint64_t
sigbits_delbit(atomic_sigbits_t *sigbits, int sig)
{
  return atomic_fetch_and(sigbits, ~(1UL << (sig - 1)));
}

inline uint64_t
sigbits_addset(atomic_sigbits_t *sigbits, l_sigset_t *set)
{
  return atomic_fetch_or(sigbits, LINUX_SIGSET_TO_UI64(set));
}

inline uint64_t
sigbits_delset(atomic_sigbits_t *sigbits, l_sigset_t *set)
{
  return atomic_fetch_and(sigbits, ~(LINUX_SIGSET_TO_UI64(set)));
}

inline uint64_t
sigbits_replace(atomic_sigbits_t *sigbits, l_sigset_t *set)
{
  return atomic_exchange(sigbits, LINUX_SIGSET_TO_UI64(set));
}

DEFINE_SYSCALL(rt_sigaction, int, sig, gaddr_t, act, gaddr_t, oact, size_t, size)
{
  if (sig <= 0 || sig > LINUX_NSIG || sig == LINUX_SIGKILL || sig == LINUX_SIGSTOP) {
    return -LINUX_EINVAL;
  }

  l_sigaction_t lact;
  struct sigaction dact, doact;

  if (copy_from_user(&lact, act, sizeof(l_sigaction_t)))  {
    return -LINUX_EFAULT;
  }

  linux_to_darwin_sigaction(&lact, &dact, set_sigpending);

  int err = 0;
  pthread_rwlock_wrlock(&proc.sighand.lock);
  
  err = syswrap(sigaction(sig, &dact, &doact));
  if (err < 0) {
    goto out;
  }
  if (oact != 0 && copy_to_user(oact, &proc.sighand.sigaction[sig - 1], sizeof(l_sigaction_t))) {
    sigaction(sig, &doact, NULL);
    err = -LINUX_EFAULT;
    goto out;
  }
  proc.sighand.sigaction[sig - 1] = lact;

out:
  pthread_rwlock_unlock(&proc.sighand.lock);

  return err;
}

DEFINE_SYSCALL(rt_sigprocmask, int, how, gaddr_t, nset, gaddr_t, oset, size_t, size)
{
  l_sigset_t lset, loset;
  sigset_t dset, doset;

  if (copy_from_user(&lset, nset, sizeof(l_sigset_t)))  {
    return -LINUX_EFAULT;
  }
  LINUX_SIGDELSET(&lset, SIGKILL);
  LINUX_SIGDELSET(&lset, SIGSTOP);

  int dhow;
  switch (how) {
    case LINUX_SIG_BLOCK:
      dhow = SIG_BLOCK;
      LINUX_SIGSET_ADD(&task.sigmask, &lset);
      break;
    case LINUX_SIG_UNBLOCK:
      dhow = SIG_UNBLOCK;
      LINUX_SIGSET_DEL(&task.sigmask, &lset);
      break;
    case LINUX_SIG_SETMASK:
      dhow = SIG_SETMASK;
      LINUX_SIGSET_SET(&task.sigmask, &lset);
      break;
    default:
      return -LINUX_EINVAL;
  }

  linux_to_darwin_sigset(&lset, &dset);

  int err = syswrap(sigprocmask(dhow, &dset, &doset));
  if (err < 0) {
    return err;
  }

  if (oset != 0) {
    darwin_to_linux_sigset(&doset, &loset);
    if (copy_to_user(oset, &loset, sizeof(l_sigset_t))) {
      sigprocmask(SIG_SETMASK, &doset, NULL);
      return -LINUX_EFAULT;
    }
  }

  task.sigmask = lset;

  return 0;
}

DEFINE_SYSCALL(rt_sigpending, gaddr_t, set, size_t, size)
{
  return 0;
}

DEFINE_SYSCALL(rt_sigreturn)
{
  printf("sig return!\n");
  return 0;
}

DEFINE_SYSCALL(sigaltstack, gaddr_t, uss, gaddr_t, uoss)
{
  return 0;
}

DEFINE_SYSCALL(kill, l_pid_t, pid, int, sig)
{
  return syswrap(kill(pid, linux_to_darwin_signal(sig)));
}
