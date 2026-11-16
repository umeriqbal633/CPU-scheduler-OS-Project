/*
 * algorithms.c - Part 2: FCFS, SJF, SRTF and Round Robin.
 *
 * Conventions (from the project brief):
 *   - ties: earlier arrival first, then smaller job id
 *   - RR:   jobs arriving at the moment a quantum expires enter the queue
 *           BEFORE the preempted job (the dispatcher guarantees arrivals are
 *           queued before pick_next() is called)
 *   - SRTF: checked at every time unit; equal remaining time -> running
 *           job continues
 */
#include "algorithms.h"

#include <stdio.h>
#include "jobtable.h"

void rq_init(ReadyQueue *q) { q->count = 0; }

void rq_push(ReadyQueue *q, int job)
{
    if (q->count < MAX_JOBS)
        q->items[q->count++] = job;
}

int rq_remove_at(ReadyQueue *q, int pos)
{
    int job = q->items[pos];
    for (int i = pos; i < q->count - 1; i++)
        q->items[i] = q->items[i + 1];
    q->count--;
    return job;
}

/* Sort key used by each algorithm (smaller = better). */
static int key_of(const Job *j, Algorithm alg)
{
    switch (alg) {
    case ALG_FCFS: return j->arrival;
    case ALG_SJF:  return j->burst;
    case ALG_SRTF: return j->remaining;
    default:       return 0;
    }
}

/* Is job a strictly better choice than job b under `alg`? */
static int better(const Job *a, const Job *b, Algorithm alg)
{
    int ka = key_of(a, alg), kb = key_of(b, alg);
    if (ka != kb) return ka < kb;
    if (a->arrival != b->arrival) return a->arrival < b->arrival;
    return job_id_cmp(a->id, b->id) < 0;
}

/* Position in the queue of the best job, or -1 if the queue is empty. */
static int best_pos(const ReadyQueue *rq, const Job *jobs, Algorithm alg)
{
    int best = -1;
    for (int i = 0; i < rq->count; i++)
        if (best < 0 || better(&jobs[rq->items[i]], &jobs[rq->items[best]], alg))
            best = i;
    return best;
}

int pick_next(Algorithm alg, int quantum, Job *jobs, ReadyQueue *rq,
              int current, int *quantum_used, int *preempted)
{
    *preempted = -1;
    int pos;

    switch (alg) {
    case ALG_FCFS:
    case ALG_SJF:
        /* Non-preemptive: the running job keeps the CPU until it finishes. */
        if (current >= 0) return current;
        pos = best_pos(rq, jobs, alg);
        return pos < 0 ? -1 : rq_remove_at(rq, pos);

    case ALG_SRTF:
        pos = best_pos(rq, jobs, alg);
        if (current < 0)
            return pos < 0 ? -1 : rq_remove_at(rq, pos);
        if (pos >= 0 && jobs[rq->items[pos]].remaining < jobs[current].remaining) {
            int next = rq_remove_at(rq, pos);
            rq_push(rq, current);
            *preempted = current;
            return next;
        }
        return current;                 /* equal or longer -> keep running */

    case ALG_RR:
        if (current >= 0) {
            if (*quantum_used < quantum) return current;
            if (rq->count == 0) {       /* nobody waiting: new slice, same job */
                *quantum_used = 0;
                return current;
            }
            rq_push(rq, current);       /* back of the queue, after arrivals */
            *preempted = current;
        }
        *quantum_used = 0;
        return rq->count == 0 ? -1 : rq_remove_at(rq, 0);
    }
    return -1;
}

const char *algo_tag(Algorithm alg)
{
    static const char *tags[] = { "FCFS", "SJF", "SRTF", "RR" };
    return tags[alg];
}

void algo_name(Algorithm alg, int quantum, char *buf, size_t len)
{
    if (alg == ALG_RR)
        snprintf(buf, len, "Round Robin (q = %d)", quantum);
    else
        snprintf(buf, len, "%s", algo_tag(alg));
}
