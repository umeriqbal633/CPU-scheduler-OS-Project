/* jobtable.h - Part 1: reading and validating the input file */
#ifndef JOBTABLE_H
#define JOBTABLE_H

#include <stddef.h>
#include "common.h"

/* Loads jobs from `path` into `jobs` (capacity MAX_JOBS).
 * Returns the number of jobs, or -1 with a message in `err`. */
int load_jobs(const char *path, Job *jobs, char *err, size_t errlen);

/* "Natural" job-id comparison: P2 < P10. Used for tie-breaking. */
int job_id_cmp(const char *a, const char *b);

#endif
