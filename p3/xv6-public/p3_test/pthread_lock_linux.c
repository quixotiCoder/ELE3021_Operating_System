#include <stdio.h>
#include <pthread.h>

int shared_resource = 0;

#define NUM_ITERS 10
#define NUM_THREADS 10

int turn = 0; // 상태 확인용
enum state { idle, want_in, in_cs }; // flag의 상태
enum state flag[NUM_THREADS]; // 각 tid 별 상태 flag에 저장

int next_turn = -1; // 2단계 진입용

void lock(int tid);
void unlock(int tid);

void lock(int tid)
{
    flag[tid] = want_in; // 1단계 진입
    next_turn = tid; // unlock 시 타 tid가 할당해줄 값

    level1: // 2단계 실패시 돌아올 라벨
    while (turn != tid) ; // 타 스레드 종료까지 무한대기

    flag[tid] = in_cs; // 2단계 진입
    int j = 0;

    while (j < NUM_THREADS)
    {
        if(j != tid)
            if(flag[j] == in_cs)
                goto level1;
        j++;        
    }
}

void unlock(int tid)
{
    flag[tid] = idle; // 상태 초기화
    turn = next_turn; // 1단계 대기 스레드 풀어주기
}

void* thread_func(void* arg) {
    int tid = *(int*)arg;
    
    lock(tid);
    
        for(int i = 0; i < NUM_ITERS; i++)    shared_resource++;
    
    unlock(tid);
    
    pthread_exit(NULL);
}

int main() {
    pthread_t threads[NUM_THREADS];
    int tids[NUM_THREADS];
    
    for (int i = 0; i < NUM_ITERS; i++) {
        tids[i] = i;
        pthread_create(&threads[i], NULL, thread_func, &tids[i]);
    }
    
    for (int i = 0; i < NUM_ITERS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("shared: %d\n", shared_resource);
    
    return 0;
}
