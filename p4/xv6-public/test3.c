#include "types.h"
#include "stat.h"
#include "user.h"

int initial_data = 0;

int
main(int argc, char *argv[])
{
  printf(1, "[Test 3] Make Copies\n");
  int pid;

  int parent_initial_fp = countfp();

  for(int i = 0; i < 10; i++){                        // 10 자식 만들기                     
    pid = fork();                                     // fork로 자식 생성
    if(pid == 0){
      sleep(100 + 50 * i);
      int child_initial_fp = countfp();

      initial_data = 1;                               // 자식에서 부모랑 공유 중이던 initial_data 수정
  
      int child_modified_fp = countfp();

      printf(1, "child [%d]'s result: %d\n", i,child_initial_fp - child_modified_fp);   // 자식의 freepage 수 1칸 만큼 변동 확인 (독립해서 나가면서 fp 1개 사용)
      if((child_initial_fp - child_modified_fp) == 0)
        printf(1, "[Test 3] fail\n\n");
      exit();
    }
  }

  for(int i = 0; i < 10; i++){
    wait();
  }

  if(parent_initial_fp - countfp() != 0)              // exit으로 자식 종료, freepage 수 적절 회수 확인
    printf(1, "[Test 3] fail\n\n");
  else
    printf(1, "[Test 3] pass\n\n");

  exit();
}
