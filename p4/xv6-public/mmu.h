// This file contains definitions for the
// x86 memory management unit (MMU).

// Eflags register = 인터럽트 활성화용
#define FL_IF           0x00000200      // Interrupt Enable

// Control Register flags = 레지스터용
#define CR0_PE          0x00000001      // Protection Enable
#define CR0_WP          0x00010000      // Write Protect
#define CR0_PG          0x80000000      // Paging

#define CR4_PSE         0x00000010      // Page size extension

// various segment selectors. = 세그먼트용
#define SEG_KCODE 1  // kernel code
#define SEG_KDATA 2  // kernel data+stack
#define SEG_UCODE 3  // user code
#define SEG_UDATA 4  // user data+stack
#define SEG_TSS   5  // this process's task state

// cpu->gdt[NSEGS] holds the above segments. = 위의 세그먼트를 담는 자료형
#define NSEGS     6

#ifndef __ASSEMBLER__
// Segment Descriptor
struct segdesc {
  uint lim_15_0 : 16;  // Low bits of segment limit
  uint base_15_0 : 16; // Low bits of segment base address
  uint base_23_16 : 8; // Middle bits of segment base address
  uint type : 4;       // Segment type (see STS_ constants)
  uint s : 1;          // 0 = system, 1 = application
  uint dpl : 2;        // Descriptor Privilege Level
  uint p : 1;          // Present
  uint lim_19_16 : 4;  // High bits of segment limit
  uint avl : 1;        // Unused (available for software use)
  uint rsv1 : 1;       // Reserved
  uint db : 1;         // 0 = 16-bit segment, 1 = 32-bit segment
  uint g : 1;          // Granularity: limit scaled by 4K when set
  uint base_31_24 : 8; // High bits of segment base address
};

// Normal segment
#define SEG(type, base, lim, dpl) (struct segdesc)    \
{ ((lim) >> 12) & 0xffff, (uint)(base) & 0xffff,      \
  ((uint)(base) >> 16) & 0xff, type, 1, dpl, 1,       \
  (uint)(lim) >> 28, 0, 0, 1, 1, (uint)(base) >> 24 }
#define SEG16(type, base, lim, dpl) (struct segdesc)  \
{ (lim) & 0xffff, (uint)(base) & 0xffff,              \
  ((uint)(base) >> 16) & 0xff, type, 1, dpl, 1,       \
  (uint)(lim) >> 16, 0, 0, 1, 0, (uint)(base) >> 24 }
#endif

#define DPL_USER    0x3     // User DPL

// Application segment type bits
#define STA_X       0x8     // Executable segment
#define STA_W       0x2     // Writeable (non-executable segments)
#define STA_R       0x2     // Readable (executable segments)

// System segment type bits
#define STS_T32A    0x9     // Available 32-bit TSS
#define STS_IG32    0xE     // 32-bit Interrupt Gate
#define STS_TG32    0xF     // 32-bit Trap Gate

// A virtual address 'la' has a three-part structure as follows:
//
//  아래의 그림 32bit 기준 Hierarchical Paging (계층적 페이징)
//
// +--------10------+-------10-------+---------12----------+
// | Page Directory |   Page Table   | Offset within Page  |
// |      Index     |      Index     |                     |
// +----------------+----------------+---------------------+
//  \--- PDX(va) --/ \--- PTX(va) --/


/*
---   Project04   --- 이 밑부터 페이지 관련
*/

// page directory index {va : virtual address}
// 메모리 싱에서 이차원 배열 표시가 안되기에 각각 일차원으로 2개 directory & page table 구분

// 페이지 디렉토리 인덱스 (page directory index)
#define PDX(va) (((uint)(va) >> PDXSHIFT) & 0x3FF)

// 페이지 테이블 인덱스 (page table index)
#define PTX(va) (((uint)(va) >> PTXSHIFT) & 0x3FF)

// 인덱스와 오프셋으로 가상 주소 구성 (construct virtual address from indexes and offset)
#define PGADDR(d, t, o) ((uint)((d) << PDXSHIFT | (t) << PTXSHIFT | (o)))

// 페이지 디렉토리 및 페이지 테이블 상수 (Page directory and page table constants).
#define NPDENTRIES 1024 // 페이지 디렉토리 당 디렉토리 항목 수 = index 10bit로 (# directory entries per page directory)
#define NPTENTRIES 1024 // 페이지 테이블 당 PTE 수 = index 10bit로 (# PTEs per page table)
#define PGSIZE 4096 // 페이지 테이블 속 하나의 페이지 엔트리인 '페이지'가 매핑하는 바이트 수 = 12bit이기에 (bytes mapped by a page)

#define PTXSHIFT 12 // 선형 주소에서 PTX의 오프셋 : Page Table의 한 요소 증가 == {하나의 'Page' 증가 = 12 bit}(offset of PTX in a linear address)
#define PDXSHIFT 22 // 선형 주소에서 PDX의 오프셋 : Page Directory의 한 요소 증가 == {하나의 'Page Table' 증가 = 22 bit}(offset of PDX in a linear address)

#define PGROUNDUP(sz) (((sz)+PGSIZE-1) & ~(PGSIZE-1))	// 페이지 범위 끝까지 [올리기] 
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1)) 			// 페이지 범위 밑으로 [내림] for 해당 페이지 포함 재할당

// 페이지 테이블/디렉토리 엔트리 플래그 (Page table/directory entry flags).

/*
---   Project04   --- 각 페이지 별 state {읽기 / 쓰기 판별}
*/
#define PTE_P 0x001 // 할당된 상태임 (Present)
#define PTE_W 0x002 // 쓰기 가능 (Writeable)
#define PTE_U 0x004 // 사용자 (User)
#define PTE_PS 0x080

// 페이지 테이블 또는 페이지 디렉토리 엔트리의 주소 (Address in page table or page directory entry)
#define PTE_ADDR(pte) ((uint)(pte) & ~0xFFF)
#define PTE_FLAGS(pte) ((uint)(pte) & 0xFFF)

#ifndef __ASSEMBLER__
typedef uint pte_t;

// Task state segment format
struct taskstate {
  uint link;         // Old ts selector
  uint esp0;         // Stack pointers and segment selectors
  ushort ss0;        //   after an increase in privilege level
  ushort padding1;
  uint *esp1;
  ushort ss1;
  ushort padding2;
  uint *esp2;
  ushort ss2;
  ushort padding3;
  void *cr3;         // Page directory base
  uint *eip;         // Saved state from last task switch
  uint eflags;
  uint eax;          // More saved state (registers)
  uint ecx;
  uint edx;
  uint ebx;
  uint *esp;
  uint *ebp;
  uint esi;
  uint edi;
  ushort es;         // Even more saved state (segment selectors)
  ushort padding4;
  ushort cs;
  ushort padding5;
  ushort ss;
  ushort padding6;
  ushort ds;
  ushort padding7;
  ushort fs;
  ushort padding8;
  ushort gs;
  ushort padding9;
  ushort ldt;
  ushort padding10;
  ushort t;          // Trap on task switch
  ushort iomb;       // I/O map base address
};

// Gate descriptors for interrupts and traps
struct gatedesc {
  uint off_15_0 : 16;   // low 16 bits of offset in segment
  uint cs : 16;         // code segment selector
  uint args : 5;        // # args, 0 for interrupt/trap gates
  uint rsv1 : 3;        // reserved(should be zero I guess)
  uint type : 4;        // type(STS_{IG32,TG32})
  uint s : 1;           // must be 0 (system)
  uint dpl : 2;         // descriptor(meaning new) privilege level
  uint p : 1;           // Present
  uint off_31_16 : 16;  // high bits of offset in segment
};

// Set up a normal interrupt/trap gate descriptor.
// - istrap: 1 for a trap (= exception) gate, 0 for an interrupt gate.
//   interrupt gate clears FL_IF, trap gate leaves FL_IF alone
// - sel: Code segment selector for interrupt/trap handler
// - off: Offset in code segment for interrupt/trap handler
// - dpl: Descriptor Privilege Level -
//        the privilege level required for software to invoke
//        this interrupt/trap gate explicitly using an int instruction.
#define SETGATE(gate, istrap, sel, off, d)                \
{                                                         \
  (gate).off_15_0 = (uint)(off) & 0xffff;                \
  (gate).cs = (sel);                                      \
  (gate).args = 0;                                        \
  (gate).rsv1 = 0;                                        \
  (gate).type = (istrap) ? STS_TG32 : STS_IG32;           \
  (gate).s = 0;                                           \
  (gate).dpl = (d);                                       \
  (gate).p = 1;                                           \
  (gate).off_31_16 = (uint)(off) >> 16;                  \
}

#endif
