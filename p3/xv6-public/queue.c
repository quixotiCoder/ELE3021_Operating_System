#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

void init(struct queue* q, int tq) // 배열 큐 초기화
{
	q->front = -1;
	q->rear = -1;
	q->count = 0;
    q->tq =tq;
}

int is_empty(struct queue* q)
{
    return (q->front == q->rear);
}

int is_full(struct queue* q)
{
	return (q->rear == 99998);
}

void enqueue(struct queue* q, int e)
{
	if (is_full(q))
   		return;

    else
  	{
		q->rear++;
    	q->size[q->rear] = e;
	}
}

void dequeue(struct queue* q)
{
	if (is_empty(q))
    {
        return;
    }
    q->front++;
    q->count--;
}

int front(struct queue* q)
{
	if(is_empty(q))
		return 0;

	return q->size[q->front];
}
void delete(struct queue* q, int e)
{
    if (is_empty(q))
    {
        return;
    }
	for (int i = q->front; i != q->rear; i++)
    {
        if (q->size[i] == e)
        {
            for (int j = i; j != q->rear; j++)
            {
                q->size[j] = q->size[j + 1];
            }
            q->rear = q->rear - 1;
            q->count--;
            return;
        }
    }
}
