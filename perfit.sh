#!/bin/bash

sudo perf record --all-cpus --call-graph dwarf,8192 --freq=1000 -o /tmp/perf.data _release/gdbsyms symfiles/gdbsyms_0000.txt

sudo chown ${USER}:${USER} /tmp/perf.data

# perf report --no-children -i /tmp/perf.data
perf report -i /tmp/perf.data
