/*
 * output.c - Part 4: text Gantt chart, per-job table, comparison table.
 *
 * Gantt chart example (Round Robin, q = 2, jobs1.txt):
 *   +----+----+----+----+----+----+----+----+
 *   | P1 | P2 | P3 | P1 | P2 | P3 | P1 | P3 |
 *   +----+----+----+----+----+----+----+----+
 *   0    2    4    6    8    9    11   12   16
 */
#include "output.h"

#include <string.h>

#define GANTT_LINE_WIDTH 72

static int num_width(int v)
{
    int w = 1;
    while (v >= 10) {
        v /= 10;
        w++;
    }
    return w;
}

static void print_border(FILE *out, const int *w, int from, int to)
{
    fputc('+', out);
    for (int i = from; i < to; i++) {
        for (int k = 0; k < w[i]; k++) fputc('-', out);
        fputc('+', out);
    }
    fputc('\n', out);
}

void print_gantt(FILE *out, const AlgoResult *r)
{
    int w[MAX_SEGS];
    for (int i = 0; i < r->nsegs; i++) {
        int len = (int)strlen(r->segs[i].id) + 2;
        int lbl = num_width(r->segs[i].start) + 1;
        w[i] = len > lbl ? len : lbl;
        if (w[i] < 4) w[i] = 4;
    }

    fprintf(out, "\nGantt chart - %s\n", r->name);

    /* split long charts over several rows */
    int from = 0;
    while (from < r->nsegs) {
        int to = from, col = 1;
        while (to < r->nsegs && (to == from || col + w[to] + 1 <= GANTT_LINE_WIDTH)) {
            col += w[to] + 1;
            to++;
        }

        print_border(out, w, from, to);
        fputc('|', out);
        for (int i = from; i < to; i++) {
            int len = (int)strlen(r->segs[i].id);
            int left = (w[i] - len) / 2;
            fprintf(out, "%*s%s%*s|", left, "", r->segs[i].id, w[i] - len - left, "");
        }
        fputc('\n', out);
        print_border(out, w, from, to);
        for (int i = from; i < to; i++)
            fprintf(out, "%-*d", w[i] + 1, r->segs[i].start);
        fprintf(out, "%d\n", r->segs[to - 1].end);

        from = to;
    }
    if (r->gantt_truncated)
        fprintf(out, "(Gantt chart truncated: more than %d segments)\n", MAX_SEGS);
}

void print_job_table(FILE *out, const AlgoResult *r)
{
    const char *sep =
        "+--------+---------+-------+------------+------------+---------+----------+\n";
    fprintf(out, "\nPer-job metrics - %s\n", r->name);
    fputs(sep, out);
    fprintf(out, "| %-6s | %7s | %5s | %10s | %10s | %7s | %8s |\n", "Job", "Arrival", "Burst",
            "Completion", "Turnaround", "Waiting", "Response");
    fputs(sep, out);
    for (int i = 0; i < r->njobs; i++) {
        const JobResult *j = &r->jobs[i];
        fprintf(out, "| %-6s | %7d | %5d | %10d | %10d | %7d | %8d |\n", j->id, j->arrival,
                j->burst, j->completion, j->turnaround, j->waiting, j->response);
    }
    fputs(sep, out);
    fprintf(out, "| %-6s | %7s | %5s | %10s | %10.2f | %7.2f | %8.2f |\n", "Avg", "", "", "",
            r->avg_turnaround, r->avg_waiting, r->avg_response);
    fputs(sep, out);
    fprintf(out, "Context switches: %d   Finished at t=%d   CPU idle: %d unit(s)\n",
            r->context_switches, r->total_time, r->idle_time);
}

void print_comparison(FILE *out, const SharedReport *rep)
{
    const char *sep =
        "+----------------------+-------------+----------------+--------------+------------------+\n";
    fprintf(out, "COMPARISON TABLE - %s\n", rep->input_file);
    fputs(sep, out);
    fprintf(out, "| %-20s | %11s | %14s | %12s | %16s |\n", "Algorithm", "Avg waiting",
            "Avg turnaround", "Avg response", "Context switches");
    fputs(sep, out);
    for (int i = 0; i < rep->nalgos; i++) {
        const AlgoResult *r = &rep->algos[i];
        fprintf(out, "| %-20s | %11.2f | %14.2f | %12.2f | %16d |\n", r->name, r->avg_waiting,
                r->avg_turnaround, r->avg_response, r->context_switches);
    }
    fputs(sep, out);
}
