// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"        // uint 등 사용 변수 type
#include "defs.h"         // 사용하게 될 타 파일의 함수들
#include "param.h"        //  NPROC, KSTACKSIZE 등 parameter들
//---   Project04   --- 주로 쓰일 헤더파일
#include "memlayout.h"    // [Physical / Virtual Memory 시작 주소] && [V2P / P2V 등 내용]
#include "mmu.h"          // [MMU(Memory Management Unit)에 관한 macro / definition 존재] --> page 내용들 여기 있음

#include "spinlock.h"     // lock 관련

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run { // 새로이 이용될 빈 free 페이지 영역 하나 저장하는 구조체
  struct run *next;
};

//---   Project04   ---
int ref_c[PHYSTOP / PGSIZE] = {0, };    // 페이지 별 by 인덱스(전체 물리 크기 / 사이즈) 참조 횟수
int num_of_fp = 0 ;             		// 시스템 전체 free page 수 by [전역변수]. 처음엔 '0' <- kree마다 올려주기

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

// Initialization happens in two phases. : 초기화 과정
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend) // free page들을 linked list로 연결
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);

}
// 위 과정을 통해 ---   projecot04    ---를 위한 초기화

//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r; // free page될 얘를 저장할 임시 구조체

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP) 
    panic("kfree");

  // Fill with junk to catch dangling refs.
  // memset(v, 1, PGSIZE); // 0x01로 채워넣음 = 해제 과정 시작 -> 밑에서 조건 충족시

  if(kmem.use_lock)
    acquire(&kmem.lock);

  //---   Project04   ---
  r = (struct run*)v;                 // r에 인자로 받은 페이지 v를 넣음

  // 해제는 ref_cnt가 '0'인 경우만 가능
  if (ref_c[V2P(v) / PGSIZE] > 0)     // 참조 횟수가 0보다 크면
    ref_c[V2P(v) / PGSIZE] --;        // 일단 참조 횟수 '1'감소

  if (ref_c[V2P(v) / PGSIZE] == 0)    // 페이지의 참조 횟수가 0일 때에만 페이지를 free하고 freelist로 반환(kfree)
  {
    memset(v, 1, PGSIZE);             // 내부 초기화 (0x01로 채워넣음) 실헹
    r->next = kmem.freelist;          // 다음 free 페이지로 옮겨두고
    num_of_fp++;                      // free page 수 '1' 증가  
    kmem.freelist = r;                // freelist에 r을 넣음
  }

  if(kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory. = kalloc은 물리 메모리 할당
// Returns a pointer that the kernel can use. = return 값은 주소를 가리키는 포인터
// Returns 0 if the memory cannot be allocated. = 할당 에러 시 0 반환
char*
kalloc(void) 
{
  struct run *r; // free page를 저장할 임시 구조체 포인터

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist; // r에 freelist 하나 할당

  if(r) // 정상 할당 시,
  {
    kmem.freelist = r->next; 
    //---   Project04   ---
    ref_c[V2P((char*)r) >> PTXSHIFT] = 1; // free page가 어떤 프로세스에 의해 할당될 때 해당 페이지 인덱스 찾아가서 ref 1로 설정
    num_of_fp --;                         // free page 수 '1' 감소
  }

  if(kmem.use_lock)
    release(&kmem.lock);

  return (char*)r; // 할당된 새로운 물리 페이지 주소 반환
}

//---   Project04   ---

// 주어진 페이지의 참조 횟수를 1 증가시킵니다
// 적절한 locking을 사용해야 합니다 <- 여러 프로세스가 동시에 접근할 수 있기에
void incr_refc(uint pa){
  acquire(&kmem.lock);
  
  ref_c[pa / PGSIZE]++;
  
  release(&kmem.lock);
}

// 주어진 페이지의 참조 횟수를 1 감소시킵니다
// 적절한 locking을 사용해야 합니다 <- 여러 프로세스가 동시에 접근할 수 있기에
void decr_refc(uint pa){
  acquire(&kmem.lock);
  
  ref_c[pa / PGSIZE]--;
  
  release(&kmem.lock);
}

// 주어진 페이지의 참조 횟수를 반환합니다
// 적절한 locking을 사용해야 합니다 <- 여러 프로세스가 동시에 접근할 수 있기에
int get_refc(uint pa){
  acquire(&kmem.lock);
  
  int count = ref_c[pa / PGSIZE];
  
  release(&kmem.lock);
  
  return count;
}

// [시스템]에 존재하는 free page의 총 개수를 반환합니다
int countfp(void){
  acquire(&kmem.lock);
  
  int count = num_of_fp;
  
  release(&kmem.lock);
  
  return count;
}
