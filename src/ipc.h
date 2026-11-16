/* ipc.h - Part 5 (logger process) and Part 6 (shared-memory report) */
#ifndef IPC_H
#define IPC_H

#include "common.h"

/* Part 5: pipe + fork + dup2 + exec of ./logger. Returns 0 on success. */
int  logger_start(const char *exe_dir, const char *log_path);
/* Sends one event line to the logger. Thread-safe (protected by log_lock). */
void log_event(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Closes the write end and waits for the logger to exit (no zombie). */
void logger_stop(void);

/* Part 6: shared memory + named semaphore + fork + exec of ./report. */
int  report_run(const char *exe_dir, const SharedReport *data);

#endif
