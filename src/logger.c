/*
 * logger.c - Part 5: logger process.
 *
 * Started by the scheduler with fork() + exec(). Before exec the scheduler
 * used dup2() to make the read end of a pipe this program's standard input.
 * The logger simply reads event lines from stdin until EOF (the scheduler
 * closed the write end) and appends them to the log file.
 *
 *   usage: logger [log_file]        (default: schedule.log)
 */
#include <stdio.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "schedule.log";
    FILE *out = fopen(path, "a");
    if (!out) {
        perror("logger: fopen");
        /* keep draining stdin so the scheduler never blocks on a full pipe */
        char tmp[512];
        while (fgets(tmp, sizeof tmp, stdin))
            ;
        return 1;
    }

    time_t now = time(NULL);
    char stamp[64];
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(out, "##### logger pid %d started %s #####\n", (int)getpid(), stamp);
    fflush(out);

    char line[1024];
    long count = 0;
    while (fgets(line, sizeof line, stdin)) {      /* stdin == pipe read end */
        fputs(line, out);
        fflush(out);
        count++;
    }

    fprintf(out, "##### logger finished: %ld events #####\n\n", count);
    fclose(out);
    return 0;
}
