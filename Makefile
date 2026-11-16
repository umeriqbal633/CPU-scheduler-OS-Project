# Mini CPU Scheduler - build everything with:  make
#
#   make                     build scheduler, logger and report
#   make TIME_UNIT_MS=20     faster simulation (run 'make clean' first)
#   make results             regenerate results.txt (all mode, every input)
#   make test                quick checks (expected RR output + bad inputs)
#   make clean

CC           = gcc
TIME_UNIT_MS ?= 200
CFLAGS       = -Wall -Wextra -pthread -std=gnu11 -O2 -DTIME_UNIT_MS=$(TIME_UNIT_MS)
LDLIBS       = -pthread -lrt

SRC     = src
SCHED_OBJS  = $(SRC)/scheduler.o $(SRC)/jobtable.o $(SRC)/algorithms.o \
              $(SRC)/engine.o $(SRC)/output.o $(SRC)/ipc.o
REPORT_OBJS = $(SRC)/report.o $(SRC)/output.o
LOGGER_OBJS = $(SRC)/logger.o
HEADERS     = $(wildcard $(SRC)/*.h)

QUANTUM ?= 2

.PHONY: all clean results test

all: scheduler logger report

scheduler: $(SCHED_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

report: $(REPORT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

logger: $(LOGGER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(SRC)/%.o: $(SRC)/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

results: all
	@rm -f results.txt
	@for f in inputs/*.txt; do \
	    echo "Running $$f ..."; \
	    ./scheduler $$f all $(QUANTUM) | \
	        awk '/^COMPARISON TABLE/{p=1} p{print} p && /^$$/{exit}' >> results.txt; \
	done
	@echo "results.txt written"

test: all
	@sh tests/run_tests.sh

clean:
	rm -f $(SRC)/*.o scheduler logger report
