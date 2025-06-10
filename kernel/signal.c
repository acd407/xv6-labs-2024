#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_sigalarm(void)
{
  int gaps;
  int handler_va;
  argint(0, &gaps);
  argint(1, &handler_va);
  struct proc *p = myproc();
  p->alarm_interval = gaps;
  p->handler_va = handler_va;
  p->in_signal = 0;
  return 0;
}

void
sigproc(void) {
  struct proc *p = myproc();
  if (p->in_signal == 0 && p->alarm_interval && ticks % p->alarm_interval == 0) {
    p->in_signal = 1;
    memmove(&p->saved_trapframe, p->trapframe, sizeof(struct trapframe));
    p->trapframe->epc = (uint64)p->handler_va;
  }
}

uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  memmove(p->trapframe, &p->saved_trapframe, sizeof(struct trapframe));
  p->in_signal = 0;
  return p->trapframe->a0;;
}
