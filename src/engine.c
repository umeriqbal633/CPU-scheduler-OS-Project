/*
 * engine.c - Part 3: Live execution.
 *
 * Threads
 *   dispatcher      the thread that calls run_algorithm()
 *   arrival thread  moves jobs into the ready queue at their arrival time
 *   job threads     one per job; each runs only when the dispatcher allows
 *
 * Synchronisation (all initialised per run)
 *   lock            mutex: ready queue, clock, released, every Job field
 *                   that changes (remaining, first_start, completion)
 *   run_sem[i]      0  job i blocks here until the dispatcher gives it the CPU
 *   done_sem        0  job posts after running one time unit
 *   tick_sem        0  dispatcher -> arrival thread: "clock is now t"
 *   arrived_sem     0  arrival thread -> dispatcher: "all jobs due at t queued"
 *
 * One step of the dispatcher at time t:
 *   1. clock = t; post tick_sem; wait arrived_sem   (arrivals for t queued)
 *   2. pick_next() under the mutex                  (algorithm decision)
 *   3. post run_sem[job]; wait done_sem             (job runs one unit)
 *   4. t++, update Gantt chart, completion, context switches
 * Handing out the CPU one unit at a time lets the dispatcher react to
 * arrivals at every boundary (needed for SRTF and the RR arrival rule).
 * No thread ever spins: every wait is a sem_wait or a mutex.
 */
#include "engine.h"

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "algorithms.h"
#include "ipc.h"
#include "jobtable.h"

typedef struct {
    Job        jobs[MAX_JOBS];
    int        n;
    int        order[MAX_JOBS];     /* job indices sorted by arrival (read-only) */
    ReadyQueue rq;
    int        clock;               /* current time unit                     */
    int        released;            /* jobs already put in the ready queue   */
    const char *tag;                /* "FCFS", "RR", ... for the log         */

    pthread_mutex_t lock;
    sem_t run_sem[MAX_JOBS];
    sem_t done_sem;
    sem_t tick_sem;
    sem_t arrived_sem;
} Sim;

typedef struct {
    Sim *sim;
    int  idx;
} JobArg;

static void sleep_unit(void)
{
    struct timespec ts = { TIME_UNIT_MS / 1000, (long)(TIME_UNIT_MS % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) != 0)
        ;
}

/* ---------------- job thread ---------------- */
static void *job_thread(void *arg)
{
    Sim *s = ((JobArg *)arg)->sim;
    int  i = ((JobArg *)arg)->idx;
    Job *j = &s->jobs[i];
    int  rem;

    do {
        sem_wait(&s->run_sem[i]);           /* blocked until dispatched */

        pthread_mutex_lock(&s->lock);
        int t = s->clock;
        rem = j->remaining;
        printf("[t=%3d] %-6s running   (remaining %d)\n", t, j->id, rem);
        fflush(stdout);
        pthread_mutex_unlock(&s->lock);

        sleep_unit();                       /* simulate one unit of work */

        pthread_mutex_lock(&s->lock);
        rem = --j->remaining;
        pthread_mutex_unlock(&s->lock);

        sem_post(&s->done_sem);             /* give the CPU back */
    } while (rem > 0);

    return NULL;
}

/* ---------------- arrival thread ---------------- */
static void *arrival_thread(void *arg)
{
    Sim *s = arg;
    int next = 0;

    while (next < s->n) {
        sem_wait(&s->tick_sem);             /* wait for the next time boundary */

        pthread_mutex_lock(&s->lock);
        int now = s->clock;
        while (next < s->n && s->jobs[s->order[next]].arrival <= now) {
            int k = s->order[next++];
            rq_push(&s->rq, k);
            s->released = next;
            printf("[t=%3d] %-6s arrived   (burst %d)\n", now, s->jobs[k].id, s->jobs[k].burst);
            log_event("[t=%4d] [%-4s] %-6s arrived (burst %d)", now, s->tag,
                      s->jobs[k].id, s->jobs[k].burst);
        }
        fflush(stdout);
        pthread_mutex_unlock(&s->lock);

        sem_post(&s->arrived_sem);          /* dispatcher may decide now */
    }
    return NULL;
}

/* ---------------- helpers ---------------- */
static void gantt_add(AlgoResult *r, const char *id, int t)
{
    if (r->nsegs > 0) {
        GanttSeg *last = &r->segs[r->nsegs - 1];
        if (last->end == t && strcmp(last->id, id) == 0) {
            last->end = t + 1;
            return;
        }
    }
    if (r->nsegs == MAX_SEGS) {
        r->gantt_truncated = 1;
        return;
    }
    GanttSeg *g = &r->segs[r->nsegs++];
    snprintf(g->id, sizeof g->id, "%s", id);
    g->start = t;
    g->end   = t + 1;
}

static const Job *sort_jobs;
static int arrival_cmp(const void *a, const void *b)
{
    const Job *x = &sort_jobs[*(const int *)a], *y = &sort_jobs[*(const int *)b];
    if (x->arrival != y->arrival) return x->arrival < y->arrival ? -1 : 1;
    return job_id_cmp(x->id, y->id);
}

static int result_cmp(const void *a, const void *b)
{
    return job_id_cmp(((const JobResult *)a)->id, ((const JobResult *)b)->id);
}

/* ---------------- dispatcher ---------------- */
int run_algorithm(const Job *input, int n, Algorithm alg, int quantum, AlgoResult *out)
{
    Sim *s = calloc(1, sizeof *s);
    if (!s) {
        perror("calloc");
        return -1;
    }
    s->n   = n;
    s->tag = algo_tag(alg);
    for (int i = 0; i < n; i++) {
        s->jobs[i] = input[i];
        s->jobs[i].remaining   = input[i].burst;
        s->jobs[i].first_start = -1;
        s->jobs[i].completion  = -1;
        s->order[i] = i;
    }
    sort_jobs = s->jobs;                    /* single-threaded at this point */
    qsort(s->order, (size_t)n, sizeof(int), arrival_cmp);
    rq_init(&s->rq);

    pthread_mutex_init(&s->lock, NULL);
    for (int i = 0; i < n; i++) sem_init(&s->run_sem[i], 0, 0);
    sem_init(&s->done_sem, 0, 0);
    sem_init(&s->tick_sem, 0, 0);
    sem_init(&s->arrived_sem, 0, 0);

    memset(out, 0, sizeof *out);
    out->alg     = alg;
    out->quantum = alg == ALG_RR ? quantum : 0;
    out->njobs   = n;
    algo_name(alg, quantum, out->name, sizeof out->name);

    printf("\n==================== %s ====================\n", out->name);
    log_event("========== %s started ==========", out->name);
    fflush(stdout);

    pthread_t   tids[MAX_JOBS], arr_tid;
    JobArg      args[MAX_JOBS];
    for (int i = 0; i < n; i++) {
        args[i].sim = s;
        args[i].idx = i;
        pthread_create(&tids[i], NULL, job_thread, &args[i]);
    }
    pthread_create(&arr_tid, NULL, arrival_thread, s);

    int t = 0, finished = 0;
    int current = -1;       /* job holding the CPU                     */
    int prev    = -1;       /* job that ran in [t-1, t), -1 = idle/none */
    int qused   = 0, ctx = 0, idle = 0;

    while (finished < n) {
        /* 1. publish the clock and let the arrival thread queue new jobs */
        pthread_mutex_lock(&s->lock);
        s->clock = t;
        int pending = s->released < n;
        pthread_mutex_unlock(&s->lock);
        if (pending) {
            sem_post(&s->tick_sem);
            sem_wait(&s->arrived_sem);
        }

        /* 2. scheduling decision */
        pthread_mutex_lock(&s->lock);
        int preempted;
        int next = pick_next(alg, quantum, s->jobs, &s->rq, current, &qused, &preempted);
        if (preempted >= 0) {
            printf("[t=%3d] %-6s preempted (remaining %d)\n", t, s->jobs[preempted].id,
                   s->jobs[preempted].remaining);
            log_event("[t=%4d] [%-4s] %-6s preempted (remaining %d)", t, s->tag,
                      s->jobs[preempted].id, s->jobs[preempted].remaining);
        }
        int first_time = 0;
        if (next >= 0 && next != current && s->jobs[next].first_start < 0) {
            s->jobs[next].first_start = t;
            first_time = 1;
        }
        int rem_before = next >= 0 ? s->jobs[next].remaining : 0;
        pthread_mutex_unlock(&s->lock);

        if (next < 0) {                     /* CPU idle for one unit */
            printf("[t=%3d] CPU IDLE\n", t);
            if (out->nsegs == 0 || strcmp(out->segs[out->nsegs - 1].id, "IDLE") != 0)
                log_event("[t=%4d] [%-4s] CPU idle", t, s->tag);
            fflush(stdout);
            gantt_add(out, "IDLE", t);
            sleep_unit();
            idle++;
            t++;
            prev = current = -1;
            continue;
        }

        if (next != current) qused = 0;
        if (prev >= 0 && prev != next) {    /* job -> different job */
            ctx++;
            printf("[t=%3d] ---- context switch: %s -> %s ----\n", t, s->jobs[prev].id,
                   s->jobs[next].id);
        }
        if (next != prev) {
            log_event("[t=%4d] [%-4s] %-6s %s (remaining %d)", t, s->tag, s->jobs[next].id,
                      first_time ? "started" : "resumed", rem_before);
        }
        fflush(stdout);
        current = next;

        /* 3. hand the CPU to the job thread for one time unit */
        sem_post(&s->run_sem[next]);
        sem_wait(&s->done_sem);

        /* 4. book-keeping */
        gantt_add(out, s->jobs[next].id, t);
        t++;
        qused++;
        prev = next;

        pthread_mutex_lock(&s->lock);
        int done = s->jobs[next].remaining == 0;
        if (done) s->jobs[next].completion = t;
        pthread_mutex_unlock(&s->lock);

        if (done) {
            finished++;
            current = -1;
            printf("[t=%3d] %-6s finished\n", t, s->jobs[next].id);
            log_event("[t=%4d] [%-4s] %-6s finished", t, s->tag, s->jobs[next].id);
            fflush(stdout);
        }
    }

    for (int i = 0; i < n; i++) pthread_join(tids[i], NULL);
    pthread_join(arr_tid, NULL);

    /* metrics (all threads joined -> no locking needed) */
    double sum_tat = 0, sum_wt = 0, sum_rt = 0;
    for (int i = 0; i < n; i++) {
        const Job *j = &s->jobs[i];
        JobResult *r = &out->jobs[i];
        snprintf(r->id, sizeof r->id, "%s", j->id);
        r->arrival    = j->arrival;
        r->burst      = j->burst;
        r->completion = j->completion;
        r->turnaround = j->completion - j->arrival;
        r->waiting    = r->turnaround - j->burst;
        r->response   = j->first_start - j->arrival;
        sum_tat += r->turnaround;
        sum_wt  += r->waiting;
        sum_rt  += r->response;
    }
    qsort(out->jobs, (size_t)n, sizeof(JobResult), result_cmp);   /* table in id order */
    out->avg_turnaround   = sum_tat / n;
    out->avg_waiting      = sum_wt / n;
    out->avg_response     = sum_rt / n;
    out->context_switches = ctx;
    out->total_time       = t;
    out->idle_time        = idle;

    log_event("========== %s finished at t=%d, %d context switches ==========",
              out->name, t, ctx);

    for (int i = 0; i < n; i++) sem_destroy(&s->run_sem[i]);
    sem_destroy(&s->done_sem);
    sem_destroy(&s->tick_sem);
    sem_destroy(&s->arrived_sem);
    pthread_mutex_destroy(&s->lock);
    free(s);
    return 0;
}
