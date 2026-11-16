#!/bin/sh
# Quick checks for the Mini CPU Scheduler.  Run with:  make test
# Tip: build with 'make clean && make TIME_UNIT_MS=10' first for a fast run.

pass=0
fail=0

ok()  { pass=$((pass + 1)); echo "  PASS  $1"; }
bad() { fail=$((fail + 1)); echo "  FAIL  $1"; }

# Extract "Job Completion Turnaround Waiting Response" rows from report output.
rows() { grep '^| P' | awk -F'|' 'NF == 9 {gsub(/ /,""); print $2, $5, $6, $7, $8}'; }

echo "== Expected output from the brief (jobs1.txt, RR q=2) =="
out=$(./scheduler inputs/jobs1.txt rr 2 2>&1)
got=$(echo "$out" | rows)
want="P1 12 12 7 0
P2 9 8 5 1
P3 16 14 6 2"
[ "$got" = "$want" ] && ok "per-job metrics match the brief" || { bad "per-job metrics"; echo "$got"; }
echo "$out" | grep -q '11.33 |    6.00 |     1.00 |' && ok "averages 11.33 / 6.00 / 1.00" || bad "averages"
echo "$out" | grep -q '| P1 | P2 | P3 | P1 | P2 | P3 | P1 | P3 |' && ok "Gantt chart order" || bad "Gantt chart order"
echo "$out" | grep -q '^0    2    4    6    8    9    11   12   16$' && ok "Gantt chart times" || bad "Gantt chart times"

echo "== Bad input is rejected with a message (exit code 1) =="
for f in tests/empty.txt tests/only_comments.txt tests/negative.txt tests/duplicate.txt \
         tests/missing_field.txt tests/not_number.txt tests/zero_burst.txt tests/does_not_exist.txt; do
    msg=$(./scheduler "$f" fcfs 2>&1 >/dev/null)
    code=$?
    [ $code -eq 1 ] && [ -n "$msg" ] && ok "$f -> $msg" || bad "$f (exit $code)"
done

echo "== Bad command line =="
./scheduler inputs/jobs1.txt xyz >/dev/null 2>&1; [ $? -eq 1 ] && ok "unknown algorithm" || bad "unknown algorithm"
./scheduler inputs/jobs1.txt rr 0 >/dev/null 2>&1; [ $? -eq 1 ] && ok "quantum 0" || bad "quantum 0"
./scheduler >/dev/null 2>&1; [ $? -eq 1 ] && ok "no arguments" || bad "no arguments"

echo "== No leftovers in /dev/shm =="
ls /dev/shm 2>/dev/null | grep -q mini_sched && bad "shared memory / semaphore not removed" || ok "shared memory and semaphore removed"

echo
echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
