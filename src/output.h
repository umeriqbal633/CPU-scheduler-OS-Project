/* output.h - Part 4: Gantt chart and metric tables (used by scheduler and report) */
#ifndef OUTPUT_H
#define OUTPUT_H

#include <stdio.h>
#include "common.h"

void print_gantt(FILE *out, const AlgoResult *r);
void print_job_table(FILE *out, const AlgoResult *r);
void print_comparison(FILE *out, const SharedReport *rep);

#endif
