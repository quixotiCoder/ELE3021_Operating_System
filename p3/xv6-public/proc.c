#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

//prohect03
void
thread_kill(struct proc *curproc) // 쓰레드 자식들 정리
{
  struct proc *t;

  acquire(&ptable.lock); // 쓰레드 동시 접근 방지

  for(t = ptable.proc; t < &ptable.proc[NPROC]; t++){
    if(!t->is_thread) // 프로세스 거르기
      continue;

    // 해당 함수 호출한 thread랑 같은 공간 공유하는 자식 쓰레드 고름
    if((curproc->is_main && t->parent->pid == curproc->pid) ||  (!curproc->is_main && t->pid == curproc->pid && t->tid != curproc->tid)){
        kfree(t->kstack); // 초기화
        t->kstack = 0;
        t->pid = 0;
      
        if(!curproc->is_main && t->is_main){
          curproc->parent = t->parent;
          t->is_main = 0;
          t->num_t = 0;
        }
        t->parent = 0;
        t->name[0] = 0;
        t->killed = 0;
        t->state = UNUSED;
        
        t->is_thread = 0;
        t->tid = 0;
        t->arg = 0;
        t->retval = 0;
      }
    }
    release(&ptable.lock);
}

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock); // 81번 line에서 ptable 접근해야 하기에 lock 걸기

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED) // unused인 프로세스 찾으면 found로 분기
      goto found;

  release(&ptable.lock);
  return 0;

found: // 배열 빈 공간 찾아서 프로세스 만들 곳 명시하는 과정
  p->state = EMBRYO; // scheduler에서 아직 고르지 못하도록 만들 상태
  p->pid = nextpid++; // pid 작은 게 먼저 생성됨을 의미
  // project03
  // 사용될 쓰레드 변수 초기화
  p->num_t = 0;
  p->is_thread = 0;  // 처음엔 기본적으로 '프로세스'를 만듦 -> thread 관련 값들 초기화 for 추후 thread로 변환 시 변경
  p->is_main = 0;
  p->next_tid = 0;
  p->tid = 0;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){ // kalloc을 통한 메모리 할당 오류시
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE; // 스택 사이즈만큼 올리고 시작

  // Leave room for trap frame.
  sp -= sizeof *p->tf; // trap frame을 위한 공간 할당 하고 trap frame 넣기
  p->tf = (struct trapframe*)sp; // 를sp(Stak Pointer)를 trap frame의 주소로

  // Set up new context to start executing at forkret, => trapret에 의해 노출 되었던 것처럼 user에서 눈속임
  // which returns to trapret.
  sp -= 4; // 4 byte 크기 내리고
  *(uint*)sp = (uint)trapret; // trapret : interrupt 발생 후, user 레벨 복귀 시 반드시 거치는 함수. 자식도 user 레벨로 돌아가도록. -> sp에 저장

  // context 정보를 stack에 삽입하는 과정
  // 자식 process는 sched를 통해 들어가는 것 X. 같은 동작 하도록 넣어주기
  sp -= sizeof *p->context;
  p->context = (struct context*)sp; // sp(Stak Pointer)를 context의 주소로
  memset(p->context, 0, sizeof *p->context); // 다른 레지스터의 값들을 0으로 넣기
  p->context->eip = (uint)forkret; // 자식 프로세스가 마치 forekret 함수에서부터 실행을 재개하는 것처럼 보여야 되기 때문에 컨텍스트 구조의 eip 인스트럭션 포인터 위치에 forkret 함수의 주소를 넣어서 context 완성

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n) // 쓰레드가 sbrk 호출하는 경우 커버해줘야함 
{
  uint sz;
  struct proc *curproc = myproc();
  struct proc* proc = curproc; // 기본적으로 process를 위한 작동

  //project03
  acquire(&ptable.lock); // 쓰레드는 병렬적으로 작동하기에 lock 걸고 sbrk 실행되도록록
  
  if(curproc->is_thread && !curproc->is_main) // 쓰레드에서 호출한 경우, 공유 메모리이기에 부모 프로세스의 메모리 늘리기 위해 지정
    proc = curproc->parent;

  sz = proc->sz;
  if(n > 0){
    if((sz = allocuvm(proc->pgdir, sz, sz + n)) == 0){
      release(&ptable.lock);
      return -1;
    }
  } else if(n < 0){
    if((sz = deallocuvm(proc->pgdir, sz, sz + n)) == 0){
      release(&ptable.lock);
      return -1;
    }
  }
  proc->sz = sz;

  if(proc->is_main){ // 쓰레드 중 main(프로세스처럼 작동)에서 호출 시, 자식 전부 공유
    for(struct proc *t = ptable.proc; t < &ptable.proc[NPROC]; t++){
      if(t->is_thread && !t->is_main && t->parent->pid == proc->pid){
        t->sz = proc->sz;
        t->pgdir = proc->pgdir;
      }
    }
  }

  release(&ptable.lock);

  switchuvm(curproc);
  return 0;
}


// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){ // allocproc : 새로운 프로세스 만들고 할당
    return -1;
  }

  acquire(&ptable.lock); // bc thread 할당 시 문제 가능

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){ // copyuvm : 부모의 user memory page table copy함 -> copyuvm | setkvm X인 (= page 할당 오류) 시 '0' 반환
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz; // NewProcess의 memory size(sz) = 현재 프로세스의 메모리 사이즈
  np->parent = curproc; // '현재 실행 중인 프로세스'를 '부모'로 지정 bc 새 자식 형성시 '현재 실행 프로세스'를 배낄테니
  *np->tf = *curproc->tf; // trapframe 역시 부모의 내용 그대로 복사

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0; // '자식의 return value' 지정해줌. tf의 eax에 return value 저장되기에 이 값을 '0'으로 선언

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  np->state = RUNNABLE; // -> fork된 자식 스케쥴링 가능

  release(&ptable.lock);

  return pid; // 자식의 pid (= 0) return
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void) // 전체 다 정리
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  // Project03
  if(curproc->is_thread){ // 쓰레드에서 exit 호출 시, 자식 전부 정리
    thread_kill(curproc);

    curproc->num_t = 0; // 부모인 main 정리
    curproc->is_thread = 0;
    curproc->is_main = 0;
    curproc->next_tid = 0;
    curproc->tid = 0;
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one. 좀비 process의 인자 전부 초기화 + UNUSED
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti(); // 인터럽트 차단

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}


// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void) // 새로 만들어진 프로세스는 forkret 끝나면 스택에 저장해둔대로 trapret 함수로 들어감 -> interrupt 발생 시, user level로 돌아가기 위해 실행되는 부분
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) { // xv6 처음 부팅 시에만 1회 실행하는 코드
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc). !!! scheduler가 새로 생긴 프로세스 선택 -> forkret 실행 ->  끝나면 trapret 함수로 리턴 -> trapret 실행 후 종료 -> trap frame 정보 복구 -> user level로 복귀
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid) // 쓰레드에 호출 시, 스레드가 속한 프로세스 내의 모든 스레드가 모두 정리되고 자원을 회수
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;

      if(p->is_thread){
        release(&ptable.lock);

        struct proc *main;
        if(p->is_main)
          main = p;
        else
          main = p->parent;

        thread_kill(main); // 자식 정리
        
        main->num_t = 0; // 후 부모인 main 정리
        main->is_thread = 0;
        main->is_main = 0;
        main->next_tid = 0;
        main->tid = 0;
        main->killed = 1;

        acquire(&ptable.lock);
      }

      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}


/*

  Project 03

*/ 
// 현재 실행 중인 프로세스를 본따 그 속에 존재하는 것'처럼' process를 thread처럼 변경하기
// 1. fork를 차용 't' 새로 할당해서 만들고 나머지 프로세스와 공유 (새로 추가되면 쓰레드 간 / 프로세스와도 공유를 위헤 전체 프로세스 크기에 반영)
// 2. exec을 차용 [stack & register 영역]은 별도 할당 (= 새 프로세스 만들고 프로세스지만 쓰레드처럼 활용)
int
thread_create(thread_t *thread,void *(*start_routine)(void*),void *arg)
{
  uint sp, ustack[3+MAXARG+1];
  struct proc *t_parent_proc = myproc(); 
  struct proc *new_t; 

  if((new_t = allocproc()) == 0){
    return -1;
  }

  acquire(&ptable.lock); // 쓰레드 관련 동시 금지

  if(t_parent_proc->is_thread && !t_parent_proc->is_main) // 쓰레드이지만 main 아니면
    t_parent_proc = t_parent_proc->parent; // 부모(=main 쓰레드)를 찾아오기
  
  t_parent_proc->sz = PGROUNDUP(t_parent_proc->sz); // main의 sz 경계 까지로 설정

  // exec의 pgdir 할당과정 따옴
  if((t_parent_proc->sz = allocuvm(t_parent_proc->pgdir, t_parent_proc->sz, t_parent_proc->sz+2*PGSIZE)) == 0){
    release(&ptable.lock);
    return -1; 
  }
  clearpteu(t_parent_proc->pgdir, (char*)(t_parent_proc->sz - 2*PGSIZE));

  sp = t_parent_proc->sz;
  
  new_t->pgdir = t_parent_proc->pgdir; // 만들 쓰레드에 프로세스 정보 공유
  new_t->sz = t_parent_proc->sz;
  *new_t->tf = *t_parent_proc->tf;
 
  for(struct proc *t = ptable.proc; t < &ptable.proc[NPROC]; t++){
    if(t->is_thread && !t->is_main && t->parent->pid == t_parent_proc->pid)
      t->sz = t_parent_proc->sz; // 자식 쓰레드들에도 바뀐 늘어난 sz만큼 할당
  }

  // 쓰레드 정보 할당 시작
  new_t->is_thread = 1; 
  new_t->is_main = 0;

  if(!t_parent_proc->is_thread && t_parent_proc->num_t == 0){ // thread가 하나도 없던 경우 = 구동중인 프로세스 -> main thread
    t_parent_proc->is_thread = 1;
    t_parent_proc->is_main = 1;
    t_parent_proc->next_tid = 2;
    t_parent_proc->tid = 1;
  }

  new_t->parent = t_parent_proc; // new thread의 부모는 구동 중 프로세스(= main thread)
  new_t->pid = t_parent_proc->pid;

  new_t->parent->num_t++;
 
  new_t->tid = new_t->parent->next_tid++;
  *thread = new_t->tid;

  // 개별 staxk 할당
  ustack[0] = 0xffffffff; // fake return PC
  ustack[1] = (uint)arg; // 함수 인자

  sp -= 8;
  if(copyout(new_t->pgdir, sp, ustack, 8) < 0){
    release(&ptable.lock);
    return -1;
  }
  // 개별 register 할당
  new_t->tf->eax = 0;
  new_t->tf->eip = (uint)start_routine; // eip(= return)에 받은 함수 주소 인자 설정
  new_t->tf->esp = sp;

  for(int i = 0; i < NOFILE; i++)
    if(t_parent_proc->ofile[i])
      new_t->ofile[i] = filedup(t_parent_proc->ofile[i]);
  new_t->cwd = idup(t_parent_proc->cwd);

  new_t->state = RUNNABLE;
  switchuvm(new_t);

  release(&ptable.lock);
  yield(); // 생성 완료된 thread 실행
  return 0;
}

// exit 함수 차용
// 이 함수를 호출한 thread를 종료 -> 갖고 있던 retval 반환
void
thread_exit(void *retval) // 전부 삭제가 아닌 호출한 쓰레드만 종료
{
  struct proc *t = myproc();
  struct proc *p;
  int fd;

  if(t == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(t->ofile[fd]){
      fileclose(t->ofile[fd]);
      t->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(t->cwd);
  end_op();
  t->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(t->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == t){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Project03
  t->retval = retval; // 반환값
  t->parent->num_t --; // num_t 하나 줄임

  // Jump into the scheduler, never to return.
  t->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// wait를 차용
// 인자로 들어오는 thred 종료 대기
int
thread_join(thread_t thread, void **retval)
{
  struct proc *t;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock); // 쓰레드 관련

  if(curproc->is_thread && !curproc->is_main) // curproc main 아니면
    curproc = curproc->parent; // main으로 curporc 설정

  for(;;){
    for(t = ptable.proc; t < &ptable.proc[NPROC]; t++){
      if(!t->is_thread) // 쓰레드만 찾기
        continue;
      if(t->tid != thread || t->parent->pid != curproc->pid) // 찾는 'thread' 아니거나 같은 main 소속 아닌 경우
        continue;

      if(t->state == ZOMBIE){ // tid == thread인 인자로 받은 thread 찾고, 걔가 zombie이면 
        *retval = t->retval; // 반환값 받고
        kfree(t->kstack); // 초기화 진행
        t->kstack = 0;
        t->pid = 0;
        t->parent = 0;
        t->name[0] = 0;
        t->killed = 0;
        t->state = UNUSED;

        t->is_thread = 0;
        t->tid = 0;
        t->arg = 0;
        t->retval = 0;  

        release(&ptable.lock);
        return 0; // 정상
      }
    }
  
    if(curproc->killed){
      release(&ptable.lock);
      return -1; // 비정상
    }

    sleep(curproc, &ptable.lock);
  }
}


