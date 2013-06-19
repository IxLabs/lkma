#!/bin/bash

#set -x
MAX_THREADS=`awk -F ':' '/processor/{max=$2} END{print max + 2}'  /proc/cpuinfo`
TIME_LOCATION=`whereis time | awk '{print $2}'`

MASTER_KERNEL_CONFIG=`pwd`/master_config
LKMA_KERNEL_CONFIG=`pwd`/lkma_config
RESULT_FOLDER=`pwd`/results                 # Same in run_test.sh
KERNEL_LOCATION=/usr/src/linux_3.9
MASTER_BRANCH=master
LKMA_BRANCH=lkma
TEMP=.temp

function check_return(){
    local return_value=$1

    if ! [[ $return_value -eq 0 ]]
    then
        exit 1
    fi
}

function check_config(){
    local config_file=$1

    if ! [[ -f $config_file ]]
    then
        echo "Missing configuration file for the kernel build : $config_file" 1>&2
        exit 1
    fi
}

# Make sure only root can run our script
if [ "$(id -u)" != "0" ]; then
    echo "This script must be run as root" 1>&2
        exit 1
    fi

check_config $MASTER_KERNEL_CONFIG
check_config $LKMA_KERNEL_CONFIG

mkdir $RESULT_FOLDER
cp $MASTER_KERNEL_CONFIG $KERNEL_LOCATION/.config
cd $KERNEL_LOCATION

git checkout $MASTER_BRANCH > /dev/null
check_return $?

printf "Make clean ......"
make clean
printf "OK\n"
($TIME_LOCATION -f "%e %x" make all KALLSYMS_EXTRA_PASS=1 -j$MAX_THREADS) 2>&1 | tee $TEMP
MASTER_TIME=`awk 'END {print $1}' $TEMP`
check_return `awk 'END {print $2}' $TEMP`


git checkout $LKMA_BRANCH > /dev/null
check_return $?

cp $LKMA_KERNEL_CONFIG $KERNEL_LOCATION/.config

printf "Make clean ......"
make clean
printf "OK\n"
($TIME_LOCATION -f "%e %x" make all KALLSYMS_EXTRA_PASS=1 -j$MAX_THREADS) 2>&1 | tee $TEMP
LKMA_TIME=`awk 'END {print $1}' $TEMP`
check_return `awk 'END {print $2}' $TEMP`

echo -e "\n\n\n\nResults : \n"
echo "Master time : $MASTER_TIME s"
echo "LKMA time : $LKMA_TIME s"

echo "$MASTER_TIME $LKMA_TIME" > ${RESULT_FOLDER}/$(date +"%Y_%m_%d:%H:%M:%S")

rm -rf $TEMP &> /dev/null
#set +x
