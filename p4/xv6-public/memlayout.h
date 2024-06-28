// Memory layout

#define EXTMEM  0x100000            // 확장(?)메모리 주소 = Start of extended memory
#define PHYSTOP 0xE000000           // 물리 메모리 top 주소 =Top physical memory
#define DEVSPACE 0xFE000000         // 다른 장치들 주소 = Other devices are at high addresses

// 핵심 주소들 [Key addresses] for address space layout (see kmap in vm.c for layout)
#define KERNBASE 0x80000000         // virtual address 시작 주소 = First kernel virtual address
#define KERNLINK (KERNBASE+EXTMEM)  // 커널 link되는 주소 = Address where kernel is linked

#define V2P(a) (((uint) (a)) - KERNBASE)                // 가상 ---> 물리
#define P2V(a) ((void *)(((char *) (a)) + KERNBASE))    // 물리 ---> 가상

#define V2P_WO(x) ((x) - KERNBASE)    // same as V2P, but without casts
#define P2V_WO(x) ((x) + KERNBASE)    // same as P2V, but without casts
