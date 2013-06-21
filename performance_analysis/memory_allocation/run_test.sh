#!/bin/bash

#set -x

# Constants
PAD=$(printf '%0.1s' "."{1..60})
PADLENGTH=60
KB=1024
MB=$(( 1024 * $KB ))
KERNEL_TEST=1
MODULE_TEST=0
MAX_THREADS=`awk -F ':' '/processor/{max=$2} END{print max + 2}'  /proc/cpuinfo`

# Configs
KERNEL_SOURCES=/usr/src/linux_3.9
KERNEL_CONFIG=`pwd`/config
MODULE=lkma_performance
PROC_ENTRY=/proc/lkma_performance
BRANCH=master
MODULE_RESULTS_DIR="data/module"
KERNEL_RESULTS_DIR="data/kernel"
LAST_SUCCESS=.last_success
LOWER_LIMIT=$(( 0 * $MB ))
#LOWER_LIMIT=$(cat $LAST_SUCCESS)
UPPER_LIMIT=$(( 90 * $MB ))
STEP=$(( 1 * $KB ))

function check_return(){
    local return_value=$1

    if ! [[ $return_value -eq 0 ]]
    then
        exit 1
    fi
}

function sanity_checks(){
    if ! [[ -f $KERNEL_CONFIG ]]
    then
        echo "Missing kernel configuration file : '$KERNEL_CONFIG'" 1>&2
        exit 1
    fi

    if ! [[ -d $KERNEL_SOURCES ]]
    then
        echo "Directory '$KERNEL_SOURCES' not found" 1>&2
        exit 1
    fi

    if ! (lsmod | grep $MODULE) > /dev/null;
    then
        echo "Insert the module $MODULE before running the script" 1>&2
        exit 1
    fi

    if ! [[ -f $PROC_ENTRY ]]
    then
        echo "Missing proc entry : $PROC_ENTRY" 1>&2
        exit 1
    fi
}

function mb(){
    local size=$1
    echo $(( size / MB ))
}

function kb(){
    local size=$1
    echo $(( (size % MB) / KB ))
}

function b(){
    local size=$1
    echo $(( size % KB ))
}

function pretty_print(){
    local size=$1
    local result=""
    local mbs=""
    local kbs=""
    local bs=""

    mbs=$(mb $size)
    if ! [[ $mbs -eq 0 ]]
    then
        result="$mbs MB"
    fi

    kbs=$(kb $size)
    if ! [[ $kbs -eq 0 ]]
    then
        result="$result $kbs KB"
    fi

    bs=$(b $size)
    if ! [[ $bs -eq 0 ]]
    then
        result="$result $bs B"
    fi

    if [[ -z $result ]]
    then
        result="0 B"
    fi

    echo $result
}

function print_test_result(){
    local lower=$1;
    local upper=$2;
    local step=$3;
    local result=$4;
    local kernel_test=$5;
    local test_name="";

    if [[ $kernel_test -eq $KERNEL_TEST ]]
    then
        kernel_test=K
    else
        kernel_test=M
    fi

    test_name="[$(pretty_print $lower) - $(pretty_print $upper)]:$(pretty_print $step)"


    printf '%s %s' "$kernel_test" "$test_name"
    printf '%*.*s' 0 $(( $PADLENGTH - ${#test_name} - ${#result} - 1 )) "$PAD"
    printf '%s\n' "$result"
}

function get_stats(){
    local lower=$1;
    local upper=$2;
    local step=$3;
    local kernel_test=$4
    local mid=$(( ($lower + $upper) / 2 ));
    local result_dir=""
    local filename="";
    local result="";
    local test_name="$lower $upper $step"

    if [[ $kernel_test -eq 1 ]]
    then
        result_dir=$KERNEL_RESULTS_DIR
    else
        result_dir=$MODULE_RESULTS_DIR
    fi

    filename=${result_dir}/${lower}_${upper}_${step}_${RANDOM}.data;

    echo "$lower $upper $step $kernel_test" > $PROC_ENTRY
    if ! [[ $? -eq 0 ]]
    then
        echo "Failed to write to : $PROC_ENTRY";
        exit 1
    fi

    cat "${PROC_ENTRY}" > $filename

    if ! [[ $? -eq 0 ]]
    then
        rm $filename
        print_test_result $lower $upper $step "failed" $kernel_test

        if [[ $step -le 3 ]]
        then
            echo "Step is $step :("
            exit 1
        fi

        sleep 1
        get_stats $lower $mid $step $kernel_test
        sleep 1
        get_stats $mid $upper $step $kernel_test
    else
        print_test_result $lower $upper $step "passed" $kernel_test
    fi
}

function generic_test(){
    local lower=$1;
    local upper=$2;
    local step=$3;

    sleep 1
    get_stats $lower $upper $step $MODULE_TEST
    sleep 1
    get_stats $lower $upper $step $KERNEL_TEST
}

sanity_checks

## TODO Automatic test start after restart

#cp $KERNEL_CONFIG $KERNEL_SOURCES
#cd $KERNEL_SOURCES
#make bzImage KALLSYMS_EXTRA_PASS=1 -j$MAX_THREADS
#check_return $?

#update-grub

mkdir -p $MODULE_RESULTS_DIR
mkdir -p $KERNEL_RESULTS_DIR

for st_interval in $(seq $LOWER_LIMIT $MB $UPPER_LIMIT)
do
    if [[ $(( st_interval + MB )) -lt $(( 4 * MB )) ]]
    then
        generic_test $st_interval $(( $st_interval + $MB )) 100
    else
        generic_test $st_interval $(( $st_interval + $MB )) $STEP
    fi
done

#set +x
