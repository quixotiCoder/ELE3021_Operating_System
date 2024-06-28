#include "types.h" // data 유형 정의
#include "stat.h" // 파일, 디렉토리에 대한 정보 저장
#include "user.h" // 사용자 <-> 커널 상호작용 인터페이스 제공

#define NUM_LOOP 10000

int main(int argc, char* argv[]){
	int p, i;
	p = fork();
	if(p == 0){
		for(i = 0; i < NUM_LOOP; i++){
			printf(1, "Child\n");
			yield();
		}
	}
	else{
		for(i = 0; i < NUM_LOOP; i++){
			printf(1, "Parent\n");
			yield();
		}
	}
	exit();
}

