/* engine.h - Part 3: live execution with threads and semaphores */
#ifndef ENGINE_H
#define ENGINE_H

#include "common.h"

/* Runs one algorithm on a copy of `jobs` using one thread per job,
 * an arrival thread and the dispatcher (the calling thread).
 * Fills `out` with Gantt chart and metrics. Returns 0 on success. */
int run_algorithm(const Job *jobs, int n, Algorithm alg, int quantum, AlgoResult *out);

#endif
