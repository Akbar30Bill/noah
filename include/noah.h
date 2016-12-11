#ifndef NOAH_H
#define NOAH_H

#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>
#include "types.h"
#include "util/misc.h"
#include "util/list.h"
#include "malloc.h"
#include "version.h"

/* privilege management */

void drop_privilege(void);
void elevate_privilege(void);

/* interface to user memory */

gaddr_t host_to_guest(void *);
void *guest_to_host(gaddr_t);

size_t copy_from_user(void *haddr, gaddr_t gaddr, size_t n); /* returns 0 on success */
ssize_t strncpy_from_user(void *haddr, gaddr_t gaddr, size_t n);
size_t copy_to_user(gaddr_t gaddr, const void *haddr, size_t n);
ssize_t strnlen_user(gaddr_t gaddr, size_t n);

/* linux emulation */

int do_exec(const char *elf_path, int argc, char *argv[], char **envp);
int do_open(const char *path, int flags, int mode);
int do_openat(int fd, const char *path, int flags, int mode);
int do_close(int fd);
int do_faccessat(int l_dirfd, const char *l_path, int l_mode);
int do_access(const char *path, int l_mode);
int do_futex_wake(gaddr_t uaddr, int count);

void main_loop();

/* signal */

#include "linux/signal.h"

typedef atomic_uint_least64_t atomic_sigbits_t;

void sigbits_emptyset(atomic_sigbits_t *sigbits);
int  sigbits_ismember(atomic_sigbits_t *sigbits, int sig);
uint64_t sigbits_addbit(atomic_sigbits_t *sigbits, int sig);
uint64_t sigbits_delbit(atomic_sigbits_t *sigbits, int sig);
uint64_t sigbits_addset(atomic_sigbits_t *sigbits, l_sigset_t *set);
uint64_t sigbits_delset(atomic_sigbits_t *sigbits, l_sigset_t *set);
uint64_t sigbits_replace(atomic_sigbits_t *sigbits, l_sigset_t *set);

void deliver_signal();
int get_sig_to_deliver();

/* task related data */

struct sighand {
  pthread_rwlock_t lock;
  l_sigaction_t sigaction[LINUX_NSIG];
};

_Thread_local extern atomic_sigbits_t task_sigpending;  // sigpending cannot be inside task struct because thread local variables referred by signal handler should be atomic type

struct task {
  struct list_head tasks; /* Threads in the current proc */
  gaddr_t set_child_tid, clear_child_tid;
  l_sigset_t sigmask;
  atomic_sigbits_t *sigpending;
};

struct proc {
  int nr_tasks;
  struct list_head tasks;
  pthread_rwlock_t lock;
  struct mm *mm;
  char *root; /* FS root */
  l_sigset_t sigpending;
  struct sighand sighand;
};

extern struct proc proc;
_Thread_local extern struct task task;

void set_initial_proc(struct proc *proc, char *root);
void init_signal(struct proc *);

/* Linux kernel constants */

#define LINUX_RELEASE "4.6.4"
#define LINUX_VERSION "#1 SMP PREEMPT Mon Jul 11 19:12:32 CEST 2016" /* FIXME */

#define LINUX_PATH_MAX 4096         /* including null */

/* conversion */

struct stat;
struct l_newstat;
struct statfs;
struct termios;
struct linux_termios;
struct l_statfs;
struct winsize;
struct linux_winsize;

int linux_to_darwin_at_flags(int flags);
int linux_to_darwin_o_flags(int l_flags);
int darwin_to_linux_o_flags(int r);
void stat_darwin_to_linux(struct stat *stat, struct l_newstat *lstat);
void statfs_darwin_to_linux(struct statfs *statfs, struct l_statfs *l_statfs);
void darwin_to_linux_termios(struct termios *bios, struct linux_termios *lios);
void linux_to_darwin_termios(struct linux_termios *lios, struct termios *bios);
void darwin_to_linux_winsize(struct winsize *ws, struct linux_winsize *lws);
void linux_to_darwin_winsize(struct winsize *ws, struct linux_winsize *lws);
int linux_to_darwin_signal(int signum);
int darwin_to_linux_signal(int signum);


/* debug */

#include "debug.h"

#endif
