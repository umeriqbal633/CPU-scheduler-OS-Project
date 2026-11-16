/*
 * scheduler.c - main program of the Mini CPU Scheduler.
 *
 *   ./scheduler <input_file> <algorithm> [quantum]
 *       algorithm : fcfs | sjf | srtf | rr | all
 *
 * Flow:
 *   1. load and validate the job table                 (Part 1, jobtable.c)
 *   2. start the logger process (pipe/fork/dup2/exec)   (Part 5, ipc.c)
 *   3. run each selected algorithm live with threads    (Parts 2-3)
 *   4. print the Gantt chart after each run             (Part 4, output.c)
 *   5. hand all metrics to the report process via shm   (Part 6, ipc.c)
 *   6. stop the logger and wait for it
 */
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "algorithms.h"
#include "common.h"
#include "engine.h"
#include "ipc.h"
#include "jobtable.h"
#include "output.h"

#define DEFAULT_QUANTUM 2
#define LOG_FILE        "schedule.log"

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <input_file> <algorithm> [quantum]\n"
            "  algorithm : fcfs | sjf | srtf | rr | all\n"
            "  quantum   : time quantum for rr / all (positive integer, default %d)\n"
            "Examples:\n"
            "  %s inputs/jobs1.txt fcfs\n"
            "  %s inputs/jobs1.txt rr 2\n"
            "  %s inputs/jobs1.txt all 2\n",
            prog, DEFAULT_QUANTUM, prog, prog, prog);
}

/* Directory containing this executable (so ./logger and ./report are found). */
static void exe_directory(char *buf, size_t len, const char *argv0)
{
    char tmp[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", tmp, sizeof tmp - 1);
    if (n > 0) {
        tmp[n] = '\0';
    } else {
        snprintf(tmp, sizeof tmp, "%s", argv0);
    }
    snprintf(buf, len, "%s", dirname(tmp));
}

int main(int argc, char **argv)
{
    if (argc < 3 || argc > 4) {
        usage(argv[0]);
        return 1;
    }

    /* ---- algorithm selection ---- */
    Algorithm algs[MAX_ALGOS];
    int nalgs = 0;
    const char *a = argv[2];
    if (strcasecmp(a, "fcfs") == 0) algs[nalgs++] = ALG_FCFS;
    else if (strcasecmp(a, "sjf") == 0) algs[nalgs++] = ALG_SJF;
    else if (strcasecmp(a, "srtf") == 0) algs[nalgs++] = ALG_SRTF;
    else if (strcasecmp(a, "rr") == 0) algs[nalgs++] = ALG_RR;
    else if (strcasecmp(a, "all") == 0) {
        algs[nalgs++] = ALG_FCFS;
        algs[nalgs++] = ALG_SJF;
        algs[nalgs++] = ALG_SRTF;
        algs[nalgs++] = ALG_RR;
    } else {
        fprintf(stderr, "Error: unknown algorithm '%s'\n", a);
        usage(argv[0]);
        return 1;
    }
    int uses_rr = algs[nalgs - 1] == ALG_RR;

    /* ---- quantum ---- */
    int quantum = DEFAULT_QUANTUM;
    if (argc == 4) {
        char *end;
        long q = strtol(argv[3], &end, 10);
        if (end == argv[3] || *end != '\0' || q <= 0 || q > MAX_TIME) {
            fprintf(stderr, "Error: quantum must be a positive whole number (got '%s')\n", argv[3]);
            return 1;
        }
        quantum = (int)q;
        if (!uses_rr)
            fprintf(stderr, "Note: quantum is only used by Round Robin; ignoring it.\n");
    } else if (uses_rr) {
        fprintf(stderr, "Note: no quantum given, using default quantum %d.\n", DEFAULT_QUANTUM);
    }

    /* ---- Part 1: job table ---- */
    static Job jobs[MAX_JOBS];
    char err[256];
    int n = load_jobs(argv[1], jobs, err, sizeof err);
    if (n < 0) {
        fprintf(stderr, "Error: %s\n", err);
        return 1;
    }

    printf("Loaded %d job(s) from %s   (1 time unit = %d ms)\n", n, argv[1], TIME_UNIT_MS);
    printf("  %-6s %7s %5s\n", "Job", "Arrival", "Burst");
    for (int i = 0; i < n; i++)
        printf("  %-6s %7d %5d\n", jobs[i].id, jobs[i].arrival, jobs[i].burst);

    /* A dead logger must not kill us with SIGPIPE; write() returns EPIPE instead. */
    signal(SIGPIPE, SIG_IGN);

    char exe_dir[PATH_MAX];
    exe_directory(exe_dir, sizeof exe_dir, argv[0]);

    /* ---- Part 5: logger process ---- */
    if (logger_start(exe_dir, LOG_FILE) != 0)
        fprintf(stderr, "Warning: logger could not be started; continuing without %s\n", LOG_FILE);
    log_event("session: input=%s algorithm=%s quantum=%d jobs=%d", argv[1], argv[2],
              uses_rr ? quantum : 0, n);

    /* ---- Parts 2-4: run the algorithms live ---- */
    static SharedReport report;
    memset(&report, 0, sizeof report);
    snprintf(report.input_file, sizeof report.input_file, "%s", argv[1]);

    for (int k = 0; k < nalgs; k++) {
        AlgoResult *r = &report.algos[k];
        if (run_algorithm(jobs, n, algs[k], quantum, r) != 0) {
            logger_stop();
            return 1;
        }
        report.nalgos = k + 1;
        print_gantt(stdout, r);
        printf("Avg waiting %.2f | Avg turnaround %.2f | Avg response %.2f | Context switches %d\n",
               r->avg_waiting, r->avg_turnaround, r->avg_response, r->context_switches);
        fflush(stdout);
    }

    /* ---- Part 6: report process via shared memory ---- */
    int rc = report_run(exe_dir, &report);

    /* ---- shut down the logger (EOF on the pipe, then waitpid) ---- */
    log_event("session finished");
    logger_stop();
    printf("\nEvents written to %s\n", LOG_FILE);
    return rc == 0 ? 0 : 1;
}
