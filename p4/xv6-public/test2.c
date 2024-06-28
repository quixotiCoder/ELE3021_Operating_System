#include "types.h"
#include "stat.h"
#include "user.h"

int initial_data = 0;

int
main(int argc, char *argv[])
{
  printf(1, "[Test 2] Make a Copy\n");
  int pid;

  pid = fork();                                       // fork로 자식 생성    
  if(pid == 0){                                       // 자식 프로세스만 아레 실행
    int child_initial_fp = countfp();

    initial_data = 1;                                 // 자식에서 부모랑 공유 중이던 initial_data 수정                  
  
    int child_modified_fp = countfp();                // 자식의 freepage 수 변동 확인

    if((child_initial_fp - child_modified_fp) == 1)
      printf(1, "[Test 2] pass\n\n");
    else
      printf(1, "[Test 2] fail\n\n");
  }
  else{
    wait();
  }

  exit();
}
