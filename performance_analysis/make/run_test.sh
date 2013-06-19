#!/bin/bash

# Configs
NR_ITERATIONS=1
MAKE_TEST=./make_test.sh
PARSE_RESULTS=./parse_results.awk
RESULT_FOLDER=`pwd`/results                 # Same in make_test.sh

function check_script(){
    if ! [[ -x $1 ]]
    then
        echo "Missing script : $1" 1>&2
        exit 1
    fi
}

function sanity_checks(){
    check_script $MAKE_TEST
    check_script $PARSE_RESULTS
}

sanity_checks

for i in $(seq 1 1 $NR_ITERATIONS)
do
    $MAKE_TEST
done

if ! [[ -d $RESULT_FOLDER ]]
then
    echo "No $RESULT_FOLDER was generated" 1>&2
    exit 1
fi

$PARSE_RESULTS $RESULT_FOLDER/*
