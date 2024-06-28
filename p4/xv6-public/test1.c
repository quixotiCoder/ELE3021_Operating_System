#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int parent_fp, child_fp;
  int pid;

  printf(1, "[Test 1] initial sharing\n");
	
  parent_fp = countfp();                      // 부모의 freepage 수

  pid = fork();                               // fork로 자식 생성
  if(pid == 0){
    child_fp = countfp();                    // 자식의 freepage 수 확인          
                                             // 부모 자식 간 freepage 차이 == 정상적으로 줄었는지 확인 
    if(parent_fp - child_fp == 68)
      printf(1, "[Test 1] pass\n\n");
    else
      printf(1, "[Test 1] fail\n\n");
    
    exit();
  }
  else{
    wait();
  }

  exit();
}

