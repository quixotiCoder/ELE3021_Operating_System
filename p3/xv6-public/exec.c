#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"
#include "spinlock.h"

// Project03
struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

int
exec(char *path, char **argv) // 새로운 프로세스 이미지로 덮어씌움. 절대로 return 하지 않다가 '에러 발생 시에만' return 해줌
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  struct proc *curproc = myproc();

  //Project03

  acquire(&ptable.lock); // 쓰레드 동시 금지

  if(curproc->is_thread) // 쓰레드에서 exec 호출 시
    thread_kill(curproc); // thread들 전부 종료되도록 -> 새로 process를 실행할 curproc만 남음
  release(&ptable.lock);

  begin_op();

  if((ip = namei(path)) == 0){ // namei 를 통해 'inode' 얻어옴 
    end_op();
    cprintf("exec: fail\n");
    return -1;
  }
  ilock(ip);
  pgdir = 0;

  // Check ELF header = 정보를 얻고 새로운 프로세스를 만듦
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad; // 에러 case
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pgdir = setupkvm()) == 0) // setupkvm : 새로운 page table 만들고 kernel memory adress를 mapping해줌
    goto bad;

  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){ // load 된 program의 헤더의 크기만큼 loop를 돌며
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph)) // readi 함수 : page 단위로 실행 파일의 내용(명령어, 데이터)를 읽음
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0) // sz 할당 / allocuvm : 읽은 내용을 메모리에 쓰기 위해 user memory에 page 하나 할당
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0) // pgdir 할당 / loaduvm : 파일로부터 내용물을 읽어와서 메모리에 내용을 채워 넣음 = 실행 파일의 모든 내용이 메모리에 올라온 상태
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz); // 페이지 경계 설정 
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0) // 페이지 2개 할당 : user stack의 영역에 page를 할당받음. 원래 1개만 있어도 되지만 2개를 받음. 1개는 guard용(= overflow 등으로 내용물이 너무 많아서 넘치게 되더라도 다른 부분에 직접적인 피해를 주지 않기 위해서)
    goto bad; 
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE)); // pgdir 초기화
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack. = ustack은 주소와 인자를 거꾸로 넣어주기 위해 사용
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  } // 인자를 넣는 과정. 마무리 후 아래의 마무리 작업 실시
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc; // 인자의 개수
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0) // copyout 함수를 통해 ustack 복붙
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  // Commit to the user image. fork와 달리 기존 실행 프로세스를 대체하기에 그 자리 그대로 사용
  // projcet03
  // 따로 빼둔 thread에서 쓰레드 할당 하도록
  oldpgdir = curproc->pgdir; // 현재 실행 중인 page는 oldpgdir로 두고 현재의 pgdir 자리 그대로 사용 
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->tf->eip = elf.entry;  // main
  curproc->tf->esp = sp;
  switchuvm(curproc); // 새로 만들어진 프로세스의 page table을 사용하겠는다는 의미로 swtichuvm을 통해 교체 실시

  freevm(oldpgdir); // 기존 프로세스가 사용하던 메모리는 필요 없으니 oldpgdir에 저장된 page를 free로 해제. 기존 프로세스의 자리에 새 프로세스로 대체 완성
  return 0;

 bad: // 에러 case 처리문
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}
