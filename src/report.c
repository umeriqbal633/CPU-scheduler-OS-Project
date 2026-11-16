/*
 * report.c - Part 6: report process.
 *
 * Started by the scheduler with fork() + exec():
 *     report <shm_name> <sem_name>
 * Attaches to the POSIX shared-memory segment, blocks on the named
 * semaphore until the scheduler has finished writing, then prints the
 * final report. The scheduler removes the segment and semaphore afterwards.
 */
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"
#include "output.h"

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <shm_name> <sem_name>  (started by scheduler)\n", argv[0]);
        return 1;
    }

    int fd = shm_open(argv[1], O_RDONLY, 0);
    if (fd < 0) {
        perror("report: shm_open");
        return 1;
    }
    const SharedReport *rep = mmap(NULL, sizeof(SharedReport), PROT_READ, MAP_SHARED, fd, 0);
    if (rep == MAP_FAILED) {
        perror("report: mmap");
        close(fd);
        return 1;
    }
    sem_t *ready = sem_open(argv[2], 0);
    if (ready == SEM_FAILED) {
        perror("report: sem_open");
        munmap((void *)rep, sizeof(SharedReport));
        close(fd);
        return 1;
    }

    printf("[report] pid %d attached to shared memory, waiting for data...\n", (int)getpid());
    fflush(stdout);
    sem_wait(ready);                         /* blocks until scheduler posts */

    printf("\n################################################################\n");
    printf("                FINAL REPORT  (read from shared memory)\n");
    printf("################################################################\n");
    printf("Input file: %s\n", rep->input_file);

    for (int i = 0; i < rep->nalgos; i++)
        print_job_table(stdout, &rep->algos[i]);

    if (rep->nalgos > 1) {
        printf("\n");
        print_comparison(stdout, rep);
        printf("\n");
    }
    fflush(stdout);

    sem_close(ready);
    munmap((void *)rep, sizeof(SharedReport));
    close(fd);
    return 0;
}
