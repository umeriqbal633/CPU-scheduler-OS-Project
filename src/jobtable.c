/*
 * jobtable.c - Part 1: Job table.
 *
 * Input format (one job per line):   job_id  arrival_time  burst_time
 * '#' starts a comment (whole line or trailing). Blank lines are ignored.
 * Rejected: missing/unreadable file, no jobs, wrong number of fields,
 * non-integer values, negative values, burst of 0, duplicate ids,
 * ids that are too long or equal to the reserved word IDLE, too many jobs.
 */
#include "jobtable.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int job_id_cmp(const char *a, const char *b)
{
    const char *oa = a, *ob = b;
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            while (*a == '0') a++;
            while (*b == '0') b++;
            const char *sa = a, *sb = b;
            while (isdigit((unsigned char)*a)) a++;
            while (isdigit((unsigned char)*b)) b++;
            size_t la = (size_t)(a - sa), lb = (size_t)(b - sb);
            if (la != lb) return la < lb ? -1 : 1;
            int c = strncmp(sa, sb, la);
            if (c) return c;
        } else {
            if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
            a++;
            b++;
        }
    }
    if (*a || *b) return *a ? 1 : -1;
    return strcmp(oa, ob);
}

/* Parses a whole, non-empty decimal integer. Returns 0 on success. */
static int parse_int(const char *s, long *out)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX)
        return -1;
    *out = v;
    return 0;
}

int load_jobs(const char *path, Job *jobs, char *err, size_t errlen)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(err, errlen, "cannot open '%s': %s", path, strerror(errno));
        return -1;
    }

    char line[512];
    int n = 0, lineno = 0;

    while (fgets(line, sizeof line, f)) {
        lineno++;
        if (!strchr(line, '\n') && !feof(f)) {
            snprintf(err, errlen, "line %d: line is too long", lineno);
            goto fail;
        }
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';

        char *tok[3], *save = NULL;
        int nt = 0;
        for (char *p = strtok_r(line, " \t\r\n", &save); p; p = strtok_r(NULL, " \t\r\n", &save)) {
            if (nt < 3) tok[nt] = p;
            nt++;
        }
        if (nt == 0) continue;                       /* blank / comment */
        if (nt != 3) {
            snprintf(err, errlen, "line %d: expected 'job_id arrival burst' (3 fields), found %d",
                     lineno, nt);
            goto fail;
        }
        if (strlen(tok[0]) >= MAX_ID_LEN) {
            snprintf(err, errlen, "line %d: job id '%s' is longer than %d characters",
                     lineno, tok[0], MAX_ID_LEN - 1);
            goto fail;
        }
        if (strcasecmp(tok[0], "IDLE") == 0) {
            snprintf(err, errlen, "line %d: 'IDLE' is reserved and cannot be a job id", lineno);
            goto fail;
        }

        long arr, burst;
        if (parse_int(tok[1], &arr) != 0) {
            snprintf(err, errlen, "line %d: arrival time '%s' is not a whole number", lineno, tok[1]);
            goto fail;
        }
        if (parse_int(tok[2], &burst) != 0) {
            snprintf(err, errlen, "line %d: burst time '%s' is not a whole number", lineno, tok[2]);
            goto fail;
        }
        if (arr < 0 || burst < 0) {
            snprintf(err, errlen, "line %d: negative time for job %s (arrival %ld, burst %ld)",
                     lineno, tok[0], arr, burst);
            goto fail;
        }
        if (burst == 0) {
            snprintf(err, errlen, "line %d: job %s has burst time 0 (must be at least 1)",
                     lineno, tok[0]);
            goto fail;
        }
        if (arr > MAX_TIME || burst > MAX_TIME) {
            snprintf(err, errlen, "line %d: times must not exceed %d", lineno, MAX_TIME);
            goto fail;
        }
        for (int i = 0; i < n; i++) {
            if (strcmp(jobs[i].id, tok[0]) == 0) {
                snprintf(err, errlen, "line %d: duplicate job id '%s'", lineno, tok[0]);
                goto fail;
            }
        }
        if (n == MAX_JOBS) {
            snprintf(err, errlen, "too many jobs (maximum is %d)", MAX_JOBS);
            goto fail;
        }

        Job *j = &jobs[n++];
        memset(j, 0, sizeof *j);
        strcpy(j->id, tok[0]);
        j->arrival     = (int)arr;
        j->burst       = (int)burst;
        j->remaining   = (int)burst;
        j->first_start = -1;
        j->completion  = -1;
    }

    if (ferror(f)) {
        snprintf(err, errlen, "error while reading '%s'", path);
        goto fail;
    }
    fclose(f);

    if (n == 0) {
        snprintf(err, errlen, "'%s' contains no jobs (file is empty or only comments)", path);
        return -1;
    }
    return n;

fail:
    fclose(f);
    return -1;
}
