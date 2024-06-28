#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"


int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void) // process의 memory size 늘리고 할당 메모리 시작 위치 반환
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)   // sbrk는 growproc을 호출하여 메모리 할당
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// SYSTEM CALLS FOR PROJECT02
void
sys_yield(void)
{
	yield();
}

// SYSTEM CALLS FOR PROJECT03
int
sys_thread_create(void)
{
  thread_t *thread;
  void *(*start_routine)(void *), *arg;

  if(argptr(0, (char**)&thread, sizeof(*thread)) < 0 || argptr(1, (char**)&start_routine, sizeof(*start_routine)) < 0 || argptr(2, (char**)&arg, sizeof(*arg)) < 0)
    return -1;
  return thread_create(thread, start_routine, arg);
}

int
sys_thread_exit(void)
{
  void *retval;

  if(argptr(0, (char**)&retval, sizeof(*retval)) < 0)
    return -1;
  thread_exit(retval);
  return 0;
}

int
sys_thread_join(void)
{
  thread_t thread;
  void **retval;

  if(argint(0, (int*)&thread) < 0 || argptr(1, (char**)&retval, sizeof(*retval)) < 0)
    return -1;
  return thread_join((thread_t)thread, retval);
}
