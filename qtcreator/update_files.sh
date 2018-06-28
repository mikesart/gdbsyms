#!/bin/bash

PROJNAME=gdbsyms

BASEDIR=$(dirname $0)
cd $BASEDIR

DIRS=(..)

# Create blah.creator file if it doesn't exist
if [[ ! -f ${PROJNAME}.creator ]]; then echo -e "[General]\n" > ${PROJNAME}.creator; fi

# Create our defines file
DEFINES=$(cat <<'END_HEREDOC'
#define __LINUX__ 1
#define _GNU_SOURCE 1
#define GPUVIS_TRACE_IMPLEMENTATION 1
END_HEREDOC
)
echo "${DEFINES}" > ${PROJNAME}.config

> ${PROJNAME}.files
for i in ${DIRS[@]}; do
    echo Checking ${i}

    find ${i} -type f -iregex '.*\.\(c\|cxx\|cc\|cpp\|h\|lpp\|ypp\|sh\|inl\|txt\)$' -or -iname Makefile >> ${PROJNAME}.files
done

fgrep -i --color=never ".h" *.files  | xargs -I{} readlink -f {} | xargs -I{} dirname {} | sort | uniq > ${PROJNAME}.includes

wc -l ${PROJNAME}.files
wc -l ${PROJNAME}.includes
