#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t *q)
{
        if (q == NULL)
                return 1;
        return (q->size == 0);
}

void enqueue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: put a new process to queue [q] */
  if (q->size < MAX_QUEUE_SIZE)
      {
        q->proc[q->size] = proc;
        q->size++;
      }
}

struct pcb_t *dequeue(struct queue_t *q)
{
        /* TODO: return a pcb whose prioprity is the highest
         * in the queue [q] and remember to remove it from q
         * */
  if (empty(q))
      return NULL;

    int highest_pcb_id = 0;
    for (int i = 1; i < q->size; i++)
      {
        if (q->proc[i]->prio < q->proc[highest_pcb_id]->prio)
          {
            highest_pcb_id = i;
          }
      }

    struct pcb_t *res = q->proc[highest_pcb_id];

  /* Dồn các phần tử phía sau lên để lấp chỗ trống */
    for (int i = highest_pcb_id; i < q->size - 1; i++)
      {
        q->proc[i] = q->proc[i + 1];
      }
  
    q->proc[q->size - 1] = NULL;
    q->size--;

    return res;
}

struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: remove a specific item from queue
         * */
  int target_idx = -1;
    for (int i = 0; i < q->size; i++)
      {
        if (q->proc[i] == proc)
          {
            target_idx = i;
            break;
          }
      }

  if (target_idx == -1)
    return NULL;

  struct pcb_t *res = q->proc[target_idx];

  /* Don mang sau khi xoa pcb */
  for (int i = target_idx; i < q->size - 1; i++)
    {
      q->proc[i] = q->proc[i + 1];
    }

  q->proc[q->size - 1] = NULL;
  q->size--;

  return res;
}
