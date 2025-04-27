#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
#ifdef LAB_TRAPS
  backtrace();
#endif
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  uint64 addr;
  int n;
  uint64 bitmask;
  argaddr(0, &addr);
  argint(1, &n);
  argaddr(2, &bitmask);

  if (n > 32 || n < 0)
    return -1;
  
  int ans = 0;
  struct proc *p = myproc();
  for (int i = 0; i < n; i++) {
    uint64 va = addr + i * PGSIZE;
    int ibit = vm_pgccess(p->pagetable, va);
    ans = ans | (ibit << i);
  }

  if (copyout(p->pagetable, bitmask, (char *)&ans, sizeof(ans)) < 0)
    return -1;

  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_trace(void)
{
  int mask;
  argint(0, &mask);
  myproc()->trace_mask = mask;
  return 0;
}

uint64
sys_sysinfo(void)
{
  struct sysinfo info;
  struct proc *p = myproc();
  uint64 addr;

  argaddr(0, &addr);
  
  info.freemem = getfreemem();
  info.nproc = getnproc();

  if(copyout(p->pagetable, addr, (char *)&info, sizeof(info)) < 0)
    return -1;
    
  return 0;
}

uint64
sys_sigalarm(void)
{
  int ticks;
  uint64 handler;
  struct proc *p = myproc();
  argint(0, &ticks);
  argaddr(1, &handler);

  p->ticks = ticks;
  p->handler = handler;

  p->ticks_cnt = 0;

  return 0;
}

void sysproc_save_trapframe(struct trapframe *tf, struct trapframe *old_tf) {
  tf -> kernel_satp = old_tf -> kernel_satp;
  tf -> kernel_sp = old_tf -> kernel_sp;
  tf -> kernel_trap = old_tf -> kernel_trap;
  tf -> epc = old_tf -> epc;
  tf -> kernel_hartid = old_tf -> kernel_hartid;
  tf -> ra = old_tf -> ra;
  tf -> sp = old_tf -> sp;
  tf -> gp = old_tf -> gp;
  tf -> tp = old_tf -> tp;
  tf -> t0 = old_tf -> t0;
  tf -> t1 = old_tf -> t1;
  tf -> t2 = old_tf -> t2;
  tf -> s0 = old_tf -> s0;
  tf -> s1 = old_tf -> s1;
  tf -> a0 = old_tf -> a0;
  tf -> a1 = old_tf -> a1;
  tf -> a2 = old_tf -> a2;
  tf -> a3 = old_tf -> a3;
  tf -> a4 = old_tf -> a4;
  tf -> a5 = old_tf -> a5;
  tf -> a6 = old_tf -> a6;
  tf -> a7 = old_tf -> a7;
  tf -> s2 = old_tf -> s2;
  tf -> s3 = old_tf -> s3;
  tf -> s4 = old_tf -> s4;
  tf -> s5 = old_tf -> s5;
  tf -> s6 = old_tf -> s6;
  tf -> s7 = old_tf -> s7;
  tf -> s8 = old_tf -> s8;
  tf -> s9 = old_tf -> s9;
  tf -> s10 = old_tf -> s10;
  tf -> s11 = old_tf -> s11;
  tf -> t3 = old_tf -> t3;
  tf -> t4 = old_tf -> t4;
  tf -> t5 = old_tf -> t5;
  tf -> t6 = old_tf -> t6;
}

uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  sysproc_save_trapframe(p -> trapframe, &(p -> saved_trapframe));
  p -> handler_running = 0;
  return p->trapframe->a0;
}