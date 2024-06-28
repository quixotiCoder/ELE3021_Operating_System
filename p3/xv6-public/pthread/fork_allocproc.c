/*

쓰레드 API 만들기 참고용

*/

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

  acquire(&ptable.lock);

  np->state = RUNNABLE; // -> fork된 자식 스케쥴링 가능

  release(&ptable.lock);

  return pid; // 자식의 pid (= 0) return
}

//////////////////////////////////////////////////////////////////////////////

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

  // Project03
  p->isThread = 0; // 기본 설정 : process
  p->tid = -1;
  p->next_tid = 0;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){ // kalloc을 통한 메모리 할당 오류시
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE; // 모든 프로세스는 자신만의 커널 스택이 있기에 새로 4096만큼 배정

  // Leave room for trap frame.
  sp -= sizeof *p->tf; // trap frame을 위한 공간 할당 (102~103 line)
  p->tf = (struct trapframe*)sp; // 현재 프로세스의 register 쭉 저장하고 복구하는 과정을 위함

  // Set up new context to start executing at forkret, => trapret에 의해 노출 되었던 것처럼 user에서 눈속임
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret; // trapret : interrupt 발생 후, user 레벨 복귀 시 반드시 거치는 함수. 자식도 user 레벨로 돌아가도록.

  // context 정보를 stack에 삽입하는 과정
  // 자식 process는 sched를 통해 들어가는 것 X. 같은 동작 하도록 넣어주기
  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context); // 다른 레지스터의 값들을 0으로 넣기
  p->context->eip = (uint)forkret; // 자식 프로세스가 마치 forekret 함수에서부터 실행을 재개하는 것처럼 보여야 되기 때문에 컨텍스트 구조의 eip 인스트럭션 포인터 위치에 forkret 함수의 주소를 넣어서 context 완성

  return p;
}
