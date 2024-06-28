#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
// cpu의 segement 초기화하는 함수 -> cpu 구동시 단 한번 실행
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
//  walkpgdir = 인자로 들어오는 pgdir 안에 있는 pde에 새로운 pte를 만들어서 반환하는 함수
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)]; // va의 Page Directory indeX 저장 [10 / 10 / 12]
  if(*pde & PTE_P){ // page table이 이미 존재하는 경우
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde)); // pde에 저장된 물리 주소를 가상 주소로 변환 
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0) // page table이 존재하지 않는 경우
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE); // page table '0'으로 전부 초기화
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U; // pde에 pgtab의 [물리 주소]를 저장
  }
  return &pgtab[PTX(va)]; // va의 Page Table indeX 반환
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.

// va는 가상 시작 주소, pa는 물리 시작 주소
// mappage는 va부터 size만큼의 가상 주소를 물리 주소 pa에 <매핑>하는 함수
// ! 2개의 주소를 요청된 사이즈만큼 계속해서 1:1 매핑 !
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);                   // 인자로 받은 va를 4KB로 내림
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);   // 최종 페이지는 받지 않음, 아래의 for문 돌다가 범위 나가도 size - 1로 해결
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)           //  새로운 PTE 할당
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;                     // `kmap`에 정적으로 할당되어 있는 4개의 영역에 대해 가상 물리 주소를 할당

  if((pgdir = (pde_t*)kalloc()) == 0) // pgdir에 페이지 사이즈(4KB)만큼 메모리 공간을 할당
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz); // oldsz까지 Page 경계까지 올리기 by 4byte까지 반올림
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child. 

//---   Project04 --- 
// 이 중 [[[복사]]] 과정 삭제 + ref_cnt 증가 by kalloc 선언 inc_refcnt함수
pde_t*
copyuvm(pde_t *pgdir, uint sz) // 인자 'pdgir & sz' 모두 부모의 것 받음 
{
  pde_t *d; // 자식 page directory
  pte_t *pte; // 자식 page table entry
  uint pa, i, flags;


  if((d = setupkvm()) == 0) // 자식을 위한 page directory 공간 생성
    return 0;

  for(i = 0; i < sz; i += PGSIZE){ // 부모 페이지 디렉토리의 페이지 사이즈와 동일히 반복
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0) // 부모 페이지 디렉토리 순회하며 페이지 테이블 엔트리 추출
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P)) // 페이지가 존재하지 X case
      panic("copyuvm: page not present");

    //---  Project04   --- 
    *pte &= (~PTE_W); // 페이지 [읽기모드] 설정 (부모 페이지 테이블 엔트리의 플래그 변경)
    pa = PTE_ADDR(*pte); // 물리 주소 추출
    flags = PTE_FLAGS(*pte); // 플래그 추출
    
    /*
    // 부모 페이지의 메모리 [[[복 붙]]]이 일어나는 과정 -> CoW니 필요 X
    if((mem = kalloc()) == 0) // 새 메모리 할당
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE); // [[[복붙]]]
    */

    if(mappages(d, (void*)i, PGSIZE, pa, flags) < 0) { // 자식 페이지 디렉토리에 부모 주소 <매핑>
      goto bad;
    }
    incr_refc(pa); // 물리 주소에 해당하는 페이지의 참조 횟수 증가
  }

  lcr3(V2P(pgdir)); // TLB flush
  return d; // 성공적 마무리 시, 자식 페이지 디렉토리 반환

bad:
  freevm(d); // 실패 시, 자식 페이지 디렉토리 해제  
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

// lab13
int
lazy_allocation() // lazy_allocation 선언 for Project04 - lab13
{
  uint pgflt_addr = rcr2(); // 가장 최근 page falut 일어난 trap 시작 가상 주소 : ReadCR2
  uint va = PGROUNDDOWN(pgflt_addr); // 내림 -> page fault난 주소를 <포함하는 페이지>를 할당하기 위해서

  char *mem = kalloc(); // 물리 페이지를 하나 할당 받고 [해당 페이지에 대한] 가상 주소 저장

  if(mem == 0){ // 에러 처리용
    cprintf("lazy_allocation out of memonry\n");
    return -1;
  }

  memset(mem, 0, PGSIZE); // 위에서 새로이 할당 받은 물리 페이지를 0으로 초기화
  // V2P(mem) mem이 가리키는 물리페이지의 가상주소 -> mem의 real 물리적 주소
  // va 와 V2P(mem)의 결과 [pa]를 mapping
  // 권한은 writing & user
  mappages(myproc()->pgdir, (char*)va, PGSIZE, V2P(mem), PTE_W | PTE_U); 
   
  return 0;
}

// <현재 프로세스>의 user memory에 할당된 <가상 페이지(logical page)의 수>를 반환합니다.
// <가상 주소 0>에서부터 <프로세스의 struct proc에 저장된 가상 주소 공간 크기>까지의
//  가상 페이 지 수를 세어야 합니다. (커널 주소 공간에 매핑된 페이지는 세지 않아도 됩니다)
int
countvp(void){
  struct proc *curproc = myproc();  // 현재 프로세스
  pde_t *pgdir = curproc->pgdir;    // 현재 프로세스의 페이지 디렉토리
  int vp_count = 0;                 

  for (int va = 0; va < curproc->sz; va += PGSIZE)  // 가상 주소 0에서부터 프로세스의 struct proc에 저장된 가상 주소 공간 크기까지
  {
      pte_t *pte = walkpgdir(pgdir, (void *)va, 0); // pte 반환
      if (pte && (*pte & PTE_P))                    // pte가 존재 && 할당된 상황
          vp_count++;
  }
  return vp_count;
}


// <현제 프로세스의 페이지 테이블을 탐색>하고
// <유효한 물리 주소가 할당된 page table entry의 수>를 반환합니다.
// xv6에서는 demand paging을 사용하지 않으므로
// countvp()의 결과와 동일해야 합니다.
int countpp(void) {
    struct proc *curproc = myproc(); // 현재 프로세스
    pde_t *pgdir = curproc->pgdir;   // 현재 프로세스의 페이지 디렉토리
    int pp_count = 0;

    for (int i = 0; i < NPDENTRIES; i++) {                  // [페이지 디렉토리] 전부 순회
        if ((i << PDXSHIFT) >= KERNBASE)                    // 페이지 디렉토리 -> 커널 영역에 속하면 중단
            break;
        if (pgdir[i] & PTE_P) {                             // 페이지 디렉토리 엔트리가 존재
            pte_t *pgtab = (pte_t*)P2V(PTE_ADDR(pgdir[i])); // 디렉토리 속 [페이지 테이블] 주소를 얻음

            for (int j = 0; j < NPTENTRIES; j++) {          // 페이지 테이블 전부 순회
                if (pgtab[j] & PTE_P)                       // 페이지 테이블 엔트리가 존재
                    pp_count++;           
            }
        }
    }
    return pp_count;
}


// 프로세스 내부 페이지 테이블을 저장하는데 <사용된 모든 페이지>와
// <페이지 디렉토리에 사용된 페이지>도 포함해야 합니다.
// <user level의 PTE 매핑을 저장하는 페이지 테이블> 뿐 아니라 
// <커널 페이지 테이블 매핑을 저장 하는 페이지 테이블>도 포함되어야 합니다.
int countptp(){
  struct proc* p = myproc();            // 현재 프로세스
  pde_t *pgdir = p->pgdir;              // 현재 프로세스의 페이지 디렉토리 
  int ptp_count = 0;

  ptp_count ++;                         // 페이지 디렉토리에 사용된 페이지

  for(int i = 0 ; i < NPDENTRIES ; i++) // pgdir의 엔트리 전부
  {
    if(pgdir[i] & PTE_P)                // PTE_P가 존재 & 할당된 경우 -> 사용된 모든 페이지
      ptp_count++;    
  }
  return ptp_count;
}

//---  Project04   ---
// trap.c에서는 mappages와 같은 static 함수를 사용 불가 -> vm.c에 함수 작성

// [구현]
/*
(부모|자식 프로세스)가 읽기 전용으로 표시된 페이지에 쓰기를 시도 -> 페이지 폴트가 발생
user memory의 복사본을 만들어야 합니다
페이지 폴트가 발생하면 CR2 레지스터가 폴트가 발생한 가상 주소를 저장 by rcr2()
가상 주소가 프로세스의 페이지 테이블에 매핑되지 않은 잘못된 범위에 속해 있다면 에러 메 시지를 출력하고 프로세스를 종료
그렇지 않다면, 필요한 페이지의 복사를 진행
*/
void CoW_handler(void) {
  uint pgflt_addr = rcr2();   // 가장 최근 page falut 일어난 trap 시작 가상 주소 : ReadCR2
  if(pgflt_addr < 0)          // 잘못된 범위
  {
    panic("잘못된 범위\n");
    return;
  }  

  pgflt_addr = PGROUNDDOWN(pgflt_addr);	// 내림 -> page fault난 주소를 <포함하는 페이지>를 할당하기 위해서

  pte_t *pte = walkpgdir(myproc()->pgdir, (void*)pgflt_addr, 0); // pte 반환

  if(!pte || !(*pte & PTE_P))        // pte가 존재하지 X or 할당되지 X
  {
    panic("존재 or 할당되지 않음\n");
    return;
  }
  
  if(*pte & PTE_W)                   // 쓰기 전용인 경우
  {
    panic("쓰기 전용 페이지\n");
    return;
  }

  int pa = PTE_ADDR(*pte);    // 물리 주소 추출
  int cnt_ref = get_refc(pa); // 물리 주소에 해당하는 페이지의 참조 횟수 추출

  if(cnt_ref > 1) {           // 참조 횟수가 1보다 큰 경우
    char *mem = kalloc();     // 물리 페이지를 하나 할당 받고 [해당 페이지에 대한] 가상 주소 저장
    if(mem == 0)              // 에러 처리용
    {
      panic("메모리 부족\n");
      return;
    }
  
    memmove(mem, (char*)P2V(pa), PGSIZE);    // 새로운 물리 주소에 복사
    *pte = V2P(mem) | PTE_W | PTE_U | PTE_P; // 새로운 물리 주소로 변경 & 쓰기 가능으로 변경
    decr_refc(pa);                           // 기존 물리 주소에 대한 참조 횟수 감소
  }
  else {            // 참조 횟수가 1인 경우 == 마지막 프로세스는 페이지를 가리키는 유일한 프로세스
    *pte |= PTE_W;  // 그저 쓰기 가능으로 변경 후, 사용
  }

  lcr3(V2P(myproc()->pgdir)); // 페이지 테이블 항목을 변경할 때마다 TLB를 flush
}


//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
