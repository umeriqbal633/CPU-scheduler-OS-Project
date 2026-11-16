/*
 * ipc.c - inter-process communication.
 *
 * Part 5  Logger process
 *   scheduler --pipe--> logger
 *   pipe() is created, fork() starts the child, the child dup2()s the read
 *   end onto its STDIN and exec()s ./logger, which reads events from stdin
 *   and appends them to schedule.log. Both sides close the ends they do not
 *   use. At the end the scheduler closes the write end (logger sees EOF)
 *   and waitpid()s the logger.
 *
 * Part 6  Report process
 *   POSIX shared memory (shm_open + ftruncate + mmap) holds a SharedReport.
 *   A named POSIX semaphore (initial value 0) tells the report process when
 *   the data is complete. The report is started with fork() + exec()
 *   BEFORE the data is written, so it really has to wait on the semaphore.
 *   Afterwards the scheduler removes both (shm_unlink, sem_unlink).
 */
#include "ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Part 5: logger                                                      */
/* ------------------------------------------------------------------ */

static int             log_fd  = -1;          /* write end of the pipe */
static pid_t           log_pid = -1;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;  /* protects log_fd writes */

int logger_start(const char *exe_dir, const char *log_path)
{
    int fds[2];
    if (pipe(fds) < 0) {
        perror("pipe");
        return -1;
    }
    /* Close-on-exec: the report process (started later with exec) must not
     * inherit the write end, otherwise the logger would never see EOF.    */
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);

    char path[1024];
    snprintf(path, sizeof path, "%s/logger", exe_dir);

    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork (logger)");
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {                         /* ---- child: logger ---- */
        close(fds[1]);                      /* child only reads        */
        if (dup2(fds[0], STDIN_FILENO) < 0) {   /* pipe becomes stdin  */
            perror("dup2");
            _exit(127);
        }
        close(fds[0]);
        execl(path, "logger", log_path, (char *)NULL);
        perror("exec logger");
        _exit(127);
    }

    close(fds[0]);                          /* parent only writes      */
    log_fd  = fds[1];
    log_pid = pid;
    return 0;
}

void log_event(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    if (len < 0) return;
    if (len > (int)sizeof buf - 2) len = (int)sizeof buf - 2;
    buf[len++] = '\n';

    pthread_mutex_lock(&log_lock);
    if (log_fd >= 0) {
        const char *p = buf;
        int left = len;
        while (left > 0) {
            ssize_t w = write(log_fd, p, (size_t)left);
            if (w < 0) {
                if (errno == EINTR) continue;
                break;                      /* logger gone: drop event */
            }
            p += w;
            left -= (int)w;
        }
    }
    pthread_mutex_unlock(&log_lock);
}

void logger_stop(void)
{
    pthread_mutex_lock(&log_lock);
    if (log_fd >= 0) {
        close(log_fd);                      /* logger reads EOF        */
        log_fd = -1;
    }
    pthread_mutex_unlock(&log_lock);

    if (log_pid > 0) {
        int status;
        while (waitpid(log_pid, &status, 0) < 0 && errno == EINTR)
            ;
        log_pid = -1;
    }
}

/* ------------------------------------------------------------------ */
/* Part 6: shared-memory report                                        */
/* ------------------------------------------------------------------ */

int report_run(const char *exe_dir, const SharedReport *data)
{
    char shm_name[64], sem_name[64], path[1024];
    snprintf(shm_name, sizeof shm_name, "/mini_sched_shm_%d", (int)getpid());
    snprintf(sem_name, sizeof sem_name, "/mini_sched_ready_%d", (int)getpid());
    snprintf(path, sizeof path, "%s/report", exe_dir);

    shm_unlink(shm_name);                   /* leftovers from a crash  */
    sem_unlink(sem_name);

    int fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        perror("shm_open");
        return -1;
    }
    if (ftruncate(fd, sizeof(SharedReport)) < 0) {
        perror("ftruncate");
        close(fd);
        shm_unlink(shm_name);
        return -1;
    }
    SharedReport *shm = mmap(NULL, sizeof(SharedReport), PROT_READ | PROT_WRITE,
                             MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        perror("mmap");
        close(fd);
        shm_unlink(shm_name);
        return -1;
    }

    /* "data ready" semaphore, initial value 0 -> report blocks until post */
    sem_t *ready = sem_open(sem_name, O_CREAT | O_EXCL, 0600, 0);
    if (ready == SEM_FAILED) {
        perror("sem_open");
        munmap(shm, sizeof(SharedReport));
        close(fd);
        shm_unlink(shm_name);
        return -1;
    }

    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork (report)");
    } else if (pid == 0) {                  /* ---- child: report ----  */
        execl(path, "report", shm_name, sem_name, (char *)NULL);
        perror("exec report");
        _exit(127);
    } else {                                /* ---- parent ----         */
        printf("\n[scheduler] report process started (pid %d); writing metrics to shared memory...\n",
               (int)pid);
        fflush(stdout);
        memcpy(shm, data, sizeof(SharedReport));
        sem_post(ready);                    /* data complete -> release report */

        int status;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
            ;
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            fprintf(stderr, "[scheduler] report process failed (status %d)\n", status);
    }

    /* Clean up: unmap, close, remove the segment and the semaphore. */
    munmap(shm, sizeof(SharedReport));
    close(fd);
    shm_unlink(shm_name);
    sem_close(ready);
    sem_unlink(sem_name);
    return pid < 0 ? -1 : 0;
}
