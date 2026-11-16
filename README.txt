===============================================================================
                    MINI CPU SCHEDULER  -  Operating Systems Lab
===============================================================================

1. GROUP MEMBERS
-------------------------------------------------------------------------------
  Name                      Roll number     Worked on
  ------------------------  --------------  -----------------------------------
  Umer Iqbal (leader)       24i-2528        Part 3 live execution (engine.c:
                                            dispatcher, arrival thread, job
                                            threads, semaphores, mutex),
                                            Part 2 algorithms (algorithms.c),
                                            main program (scheduler.c)
  Taha Abid                 24i-2507        Part 1 job table + input validation
                                            (jobtable.c), Part 4 Gantt chart and
                                            metric tables (output.c), test
                                            inputs (inputs/, tests/), results.txt
  Afraz Ahmed               24i-2498        Part 5 logger process (ipc.c
                                            logger part, logger.c), Part 6
                                            shared-memory report (ipc.c report
                                            part, report.c), Makefile, bonus
                                            starvation demo (jobs5.txt)

  All members reviewed the whole code and can explain every part.


2. HOW TO COMPILE AND RUN
-------------------------------------------------------------------------------
  Build (Linux, gcc):            make
  Faster simulation (optional):  make clean && make TIME_UNIT_MS=20
  Run the tests:                 make test
  Regenerate results.txt:        make results
  Remove binaries:               make clean

  Usage:
      ./scheduler <input_file> <algorithm> [quantum]
          algorithm : fcfs | sjf | srtf | rr | all
          quantum   : used by rr and all (default 2 if omitted)

  Examples:
      ./scheduler inputs/jobs1.txt fcfs
      ./scheduler inputs/jobs1.txt sjf
      ./scheduler inputs/jobs1.txt srtf
      ./scheduler inputs/jobs1.txt rr 2
      ./scheduler inputs/jobs1.txt all 2

  'make' builds three programs: scheduler (main), logger (started by the
  scheduler with fork + dup2 + exec) and report (started with fork + exec).
  Events are appended to schedule.log in the current directory.

  Output of one run:
    - live trace, one line per time unit, e.g.
          [t=  2] P3     arrived   (burst 8)
          [t=  2] P1     preempted (remaining 3)
          [t=  2] ---- context switch: P1 -> P2 ----
          [t=  2] P2     running   (remaining 3)
    - text Gantt chart and averages after each algorithm (scheduler)
    - FINAL REPORT: per-job table (completion, turnaround, waiting, response,
      averages) for every algorithm, plus the comparison table in 'all' mode
      (printed by the report process from shared memory)


3. SOURCE FILES
-------------------------------------------------------------------------------
  src/common.h      shared types: Job, ReadyQueue, AlgoResult, SharedReport
  src/jobtable.c    Part 1  parse + validate the input file
  src/algorithms.c  Part 2  FCFS / SJF / SRTF / RR decision (pick_next) and
                            the ready queue operations
  src/engine.c      Part 3  dispatcher, arrival thread, job threads,
                            semaphores and mutex; computes the metrics
  src/output.c      Part 4  Gantt chart, per-job table, comparison table
  src/ipc.c         Part 5  pipe/fork/dup2/exec of the logger, log_event()
                    Part 6  shared memory + semaphore, fork/exec of report
  src/logger.c      Part 5  logger program: stdin (pipe) -> schedule.log
  src/report.c      Part 6  report program: shared memory -> final report
  src/scheduler.c   main(): command line, ties everything together
  inputs/           test inputs (see section 7)
  tests/            bad-input files and run_tests.sh (make test)


4. DESIGN - HOW THE DISPATCHER CONTROLS THE THREADS
-------------------------------------------------------------------------------
  Inside the scheduler process, for each algorithm:
    - the dispatcher (main thread) owns the clock,
    - one arrival thread releases jobs into the ready queue,
    - one job thread per job does the "work".

  The dispatcher gives the CPU away ONE TIME UNIT AT A TIME. At every time
  boundary t it does:

    1. clock = t; sem_post(tick); sem_wait(arrived)
         -> the arrival thread wakes up, puts every job with arrival <= t
            into the ready queue, then posts 'arrived'. So all arrivals for
            time t are queued before any decision at time t.
    2. pick_next() (under the mutex) chooses the job for [t, t+1):
         FCFS/SJF keep the running job until it finishes; SRTF compares
         remaining times; RR checks the quantum. A preempted job is put
         back in the queue here.
    3. sem_post(run[job]); sem_wait(done)
         -> only that job thread wakes up, prints its line, sleeps one time
            unit (simulated work), decrements its remaining time and posts
            'done'. All other job threads stay blocked on their own run
            semaphore.
    4. t++, update Gantt chart, completion time and context switches.
    If the ready queue is empty the CPU is IDLE for that unit.

  Because the dispatcher decides at every unit boundary, SRTF preemption and
  the RR arrival rule happen at exactly the right time. A job thread exits
  after its last unit; the dispatcher joins all threads at the end.


5. SYNCHRONISATION TOOLS
-------------------------------------------------------------------------------
  Threads (inside the scheduler process, created again for each algorithm)

  Tool               Type / initial value   Protects / purpose
  -----------------  ---------------------  -----------------------------------
  Sim.lock           pthread mutex          ready queue, clock, 'released'
                                            counter, and the changing job
                                            fields (remaining, first_start,
                                            completion). Taken by dispatcher,
                                            arrival thread and job threads.
  log_lock (ipc.c)   pthread mutex          the pipe write end (log_fd), so
                                            event lines from the dispatcher
                                            and arrival thread never mix.
  run_sem[i]         POSIX sem, init 0      one per job: job i may run only
                                            after the dispatcher posts it.
  done_sem           POSIX sem, init 0      job -> dispatcher: "my time unit
                                            is finished".
  tick_sem           POSIX sem, init 0      dispatcher -> arrival thread:
                                            "the clock moved to t".
  arrived_sem        POSIX sem, init 0      arrival thread -> dispatcher:
                                            "all jobs due at t are queued".

  Lock order: Sim.lock may be held while calling log_event (which takes
  log_lock); never the other way round, so there is no deadlock.
  The Gantt chart and context-switch counter are only touched by the
  dispatcher thread, and the metrics are computed after all threads have
  been joined. Job ids/arrival/burst are written before the threads are
  created and only read afterwards.

  Processes

  Tool               Type / initial value   Purpose
  -----------------  ---------------------  -----------------------------------
  pipe               pipe()                 scheduler -> logger events. In the
                                            logger, dup2() makes the read end
                                            its stdin. Unused ends are closed
                                            in both processes; both ends are
                                            close-on-exec so the report process
                                            does not inherit them.
  shared memory      POSIX shm_open + mmap  holds SharedReport (all metrics,
                     name /mini_sched_shm_<pid>    Gantt data, averages)
  ready semaphore    POSIX named sem,       report waits on it; the scheduler
                     init 0, name                  posts it only after the whole
                     /mini_sched_ready_<pid>       SharedReport is written.

  Shared memory choice: POSIX (shm_open, ftruncate, mmap). The report
  process is started BEFORE the data is written, so it really blocks on the
  semaphore until the data is ready.

  No busy waiting: every wait is sem_wait, pthread_mutex_lock, pthread_join
  or waitpid. nanosleep is used only to simulate one time unit of work (and
  one unit of idle CPU time).

  Clean up: all job threads and the arrival thread are joined; semaphores
  and the mutex are destroyed after every run; the report process and the
  logger are waited for (no zombies); the pipe is closed; the shared memory
  segment and the named semaphore are removed (shm_unlink, sem_unlink).


6. ASSUMPTIONS AND CONVENTIONS
-------------------------------------------------------------------------------
  - Input: "job_id arrival burst" per line; '#' starts a comment (also after
    data). Lines may be in any order. Up to 64 jobs, ids up to 15 chars.
  - Rejected input (clear message, exit code 1): missing file, empty file /
    only comments, wrong number of fields, non-integer values, negative
    numbers, burst time 0, duplicate ids, the id "IDLE", times > 10000.
  - 1 time unit = TIME_UNIT_MS milliseconds (200 by default, see common.h).
  - Ties: earlier arrival first, then smaller job id. Ids are compared
    "naturally", so P2 < P10.
  - FCFS and SJF are non-preemptive. SJF uses the total burst time.
  - SRTF checks at every time unit; with equal remaining time the running
    job continues.
  - RR: a job that arrives at the moment a quantum expires enters the queue
    before the preempted job. If the quantum expires and nobody else is
    waiting, the same job simply continues (no preemption, no switch).
  - Context switch = CPU goes from one job directly to a different job. The
    first dispatch and switches to or from IDLE are not counted.
  - Response time = first time on the CPU - arrival time.
  - schedule.log is opened in append mode; each run starts with a header
    line from the logger.
  - If no quantum is given for rr/all, quantum 2 is used (a note is printed).


7. TEST INPUTS  (results for all of them are in results.txt, quantum 2)
-------------------------------------------------------------------------------
  inputs/jobs1.txt  example from the brief. RR q=2 gives exactly the expected
                    Gantt chart and table (avg TAT 11.33, WT 6.00, RT 1.00).
  inputs/jobs2.txt  ALL JOBS ARRIVE AT TIME 0 (tie-breaking by id).
  inputs/jobs3.txt  IDLE GAP: queue becomes empty at t=5 (next job at t=8)
                    and at t=13 (next job at t=15). Lines are unsorted.
  inputs/jobs4.txt  VERY DIFFERENT RESULTS: 10 jobs, a long job first, then
                    short ones. Avg waiting: FCFS 25.9, SJF 20.3, SRTF 7.3,
                    RR 14.5 (RR has 25 context switches).
  inputs/jobs5.txt  BONUS - STARVATION DEMO (see section 8).


8. OPTIONAL BONUS: STARVATION DEMO
-------------------------------------------------------------------------------
  inputs/jobs5.txt: P1 (burst 10) arrives at 0 together with a short job,
  and a new 2-unit job arrives every 2 units. Under SJF and SRTF there is
  always a shorter job ready, so P1 does not get the CPU until every short
  job has finished:

      ./scheduler inputs/jobs5.txt all 2

      Job P1 (burst 10)   FCFS    SJF    SRTF    RR (q=2)
      response time          0     18      18       0
      completion time       10     28      28      28

  Under SJF/SRTF, P1 does not run at all until t=18: it is starved for as
  long as short jobs keep arriving. RR also finishes P1 late, but P1 gets
  the CPU immediately and regularly (response 0). The averages look best
  for SJF/SRTF (avg waiting 1.80 vs 9.00 for FCFS), but the long job pays
  for it - the trade-off of shortest-job-first policies.


9. KNOWN LIMITATIONS
-------------------------------------------------------------------------------
  - At most 64 jobs; the Gantt chart stores at most 512 segments (metrics
    are still correct beyond that, the chart is marked as truncated).
  - Changing TIME_UNIT_MS needs 'make clean' first.
  - If the program is killed with Ctrl+C in the middle of a run, the shared
    memory / semaphore are not created yet (they are only created at the
    very end), so normally nothing is left behind; schedule.log may be
    incomplete.
  - Linux only (uses /proc/self/exe to find the logger and report programs).
