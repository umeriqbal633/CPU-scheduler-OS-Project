/*
 * common.h - types and constants shared by scheduler, logger and report.
 */
#ifndef COMMON_H
#define COMMON_H

#define MAX_JOBS     64          /* maximum jobs in one input file          */
#define MAX_ID_LEN   16          /* job id incl. '\0'                       */
#define MAX_TIME     10000       /* largest arrival / burst accepted        */
#define MAX_ALGOS    4           /* FCFS, SJF, SRTF, RR                     */
#define MAX_SEGS     512         /* Gantt segments stored per algorithm     */

/* One time unit of simulated work, in milliseconds.
 * Override at build time:  make TIME_UNIT_MS=50                          */
#ifndef TIME_UNIT_MS
#define TIME_UNIT_MS 200
#endif

typedef enum { ALG_FCFS = 0, ALG_SJF, ALG_SRTF, ALG_RR } Algorithm;

/* Part 1: one row of the job table */
typedef struct {
    char id[MAX_ID_LEN];
    int  arrival;
    int  burst;
    int  remaining;     /* burst time still to run           */
    int  first_start;   /* time of first CPU allocation, -1  */
    int  completion;    /* time the job finished, -1         */
} Job;

/* Ready queue: FIFO list of indices into the job table.
 * Shared between arrival thread and dispatcher -> protected by Sim.lock */
typedef struct {
    int items[MAX_JOBS];
    int count;
} ReadyQueue;

/* ---------- Results (these structs live in shared memory) ---------- */

typedef struct {
    char id[MAX_ID_LEN];    /* job id, or "IDLE" */
    int  start;
    int  end;
} GanttSeg;

typedef struct {
    char id[MAX_ID_LEN];
    int  arrival, burst, completion, turnaround, waiting, response;
} JobResult;

typedef struct {
    char      name[32];         /* e.g. "Round Robin (q = 2)" */
    Algorithm alg;
    int       quantum;
    int       njobs;
    JobResult jobs[MAX_JOBS];
    double    avg_turnaround, avg_waiting, avg_response;
    int       context_switches;
    int       total_time;       /* time the last job finished */
    int       idle_time;
    int       nsegs;
    int       gantt_truncated;
    GanttSeg  segs[MAX_SEGS];
} AlgoResult;

typedef struct {
    char       input_file[256];
    int        nalgos;
    AlgoResult algos[MAX_ALGOS];
} SharedReport;

#endif
