/* algorithms.h - Part 2: scheduling decisions and the ready queue */
#ifndef ALGORITHMS_H
#define ALGORITHMS_H

#include <stddef.h>
#include "common.h"

void rq_init(ReadyQueue *q);
void rq_push(ReadyQueue *q, int job);
int  rq_remove_at(ReadyQueue *q, int pos);

/*
 * Decides which job runs during the next time unit.
 * Called by the dispatcher at every time boundary, with the ready-queue
 * mutex held and after all arrivals for this instant are in the queue.
 *
 *   current       job holding the CPU (unfinished), or -1
 *   quantum_used  units the current job has run in its RR slice (in/out)
 *   preempted     set to the job that was taken off the CPU, or -1
 *
 * Returns the job index to run, or -1 if the CPU is idle.
 */
int pick_next(Algorithm alg, int quantum, Job *jobs, ReadyQueue *rq,
              int current, int *quantum_used, int *preempted);

const char *algo_tag(Algorithm alg);                 /* "FCFS", "RR", ... */
void algo_name(Algorithm alg, int quantum, char *buf, size_t len);

#endif
