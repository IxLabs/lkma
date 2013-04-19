#!/bin/bash

TMP_FIRST=.meminfo_first_tmp
TMP_LAST=.meminfo_last_tmp

function usage(){
cat << EOF
Usage $0 [OPTIONS]
Options :
    -i              Initialize database
    -c              Clear all stats
    -f file1 file2  Difference between file1 and file2.
                    Given files must be a result of cat /proc/meminfo command

If you run this script without an initialized database, it will consider current
session as first session and the last, the next run statistics will be updated.

Table header :
    * Column   - Memory type
    * Current  - Current values from /proc/meminfo
    * PrevDiff - Difference between current values and previous values
    * InitDiff - Difference between current values and initial values
EOF
}

#
# Reads data column (second column) from /proc/meminfo
#
function read_meminfo(){
    if [[ -z $1 ]]
    then
        local db=$(cat /proc/meminfo | awk '{print $2}')
    else
        local file=$1
        if ! [[ $(wc -l $file | awk '{print $1}') == 1 ]]
        then
            local db=$(cat /proc/meminfo | awk '{print $2}')
        else
            local db=$(cat $file)
        fi
    fi
    echo $db
}

#
# Gets columns from /proc/meminfo
#
function get_columns(){
    local columns=$(cat /proc/meminfo | awk '{print $1}')
    echo $columns
}

#
# Gets the unit of measure for each column from /proc/meminfo
#
function get_units(){
    local columns=$(cat /proc/meminfo | awk '{print $3";"}')
    echo $columns
}

#
# Initializes first values
#
function init_db(){
    echo $(read_meminfo) > $TMP_FIRST
}

#
# Saves current values in file
#
function update_db(){
    echo $(read_meminfo) > $TMP_LAST
}

#
# Clears all statistics
#
function clear_stats(){
    rm $TMP_FIRST &> /dev/null
    rm $TMP_LAST &> /dev/null
}

#
# Checks if given file has a valid format
#
function sanity_check(){
    local file=$1
    local columns=$(head -n 1 $file | awk '{print NF}')

    if ! [[ $(wc -l $file | awk '{print $1}') == 1 ]]
    then
        if ! [[ $columns == $(head -n 1 /proc/meminfo | awk '{print NF}') ]]
        then
            echo "Given file '$file' have not an valid output like /proc/meminfo"
            exit 1
        fi
    else
        # Same number of columns
        if ! [[ $(wc -w $file | awk '{print $1}') == $(wc -l /proc/meminfo | awk '{print $1}') ]]
        then
            echo "Given file '$file' have not an valid output like /proc/meminfo"
            exit 1
        fi
    fi
}

#
# Show difference between 2 files given as arguments
#
function diff_stats(){
    local file1=$1
    local file2=$2

    # Sanity checks
    sanity_check $file1
    sanity_check $file2

    # Get data from files
    local file1_db=($(read_meminfo $file1))
    local file2_db=($(read_meminfo $file2))
    local columns=($(get_columns))
    local units=($(get_units))

    printf "%-18s %10s %10s %10s\n" \
            "Columns" \
            "File1" \
            "File2" \
            "Diff"

    for i in $(seq 1 1 $((${#columns[@]} - 1)))
    do
        printf "%-18s " "${columns[$i]}"
        printf "%10s " "${file1_db[$i]}"
        printf "%10s " "${file2_db[$i]}"
        printf "%10s " "$((${file1_db[$i]} - ${file2_db[$i]}))"
        printf "%s\n" \
                "${units[$i]/;/}"
    done
}

#
# Prints statistics for /proc/meminfo
# Table header :
#     * Column = Memory type
#     * Current = Current values from /proc/meminfo
#     * PrevDiff = Difference between current values and previous values
#     * InitDiff = Difference between current values and initial values
#
function dump_stats(){
    # Check if previous stats are available
    if [[ -f $TMP_FIRST ]]
    then
        local init_db=($(cat $TMP_FIRST))
    else
        local init_db=()
    fi

    if [[ -f $TMP_LAST ]]
    then
        local last_db=($(cat $TMP_LAST))
    else
        local last_db=()
    fi

    local current_db=($(read_meminfo))
    local columns=($(get_columns))
    local units=($(get_units))

    # Save current stats for next runs
    if ! [[ -f $TMP_FIRST ]]
    then
        echo ${current_db[@]} > $TMP_FIRST
    fi
    echo ${current_db[@]} > $TMP_LAST

    # Print table header
    printf "%-18s %10s %10s %10s\n" \
            "Columns" \
            "Current" \
            "PrevDiff" \
            "InitDiff"

    for i in $(seq 1 1 $((${#columns[@]} - 1)))
    do
        printf "%-18s %10s " "${columns[$i]}" "${current_db[$i]}"

        if [[ -z $init_db ]]
        then
            printf "%10s " "0"
        else
            printf "%10s " "$((${current_db[$i]} - ${init_db[$i]}))"
        fi

        if [[ -z $last_db ]]
        then
            printf "%10s " "0"
        else
            printf "%10s " "$((${current_db[$i]} - ${last_db[$i]}))"
        fi
        printf "%s\n" \
                "${units[$i]/;/}"
    done
}

while getopts "ichf:" opt;
do
  case $opt in
    i)
        clear_stats
        ;;
    c)
        echo "Clear stats"
        clear_stats
        exit 0
        ;;
    h)
        usage
        exit 0
        ;;
    f)
        FILE_DIFF1=$OPTARG
        ;;
    \?)
        usage
        exit 1
        ;;
  esac
done

if ! [[ -z $FILE_DIFF1 ]]
then
        if ! [[ -f $FILE_DIFF1 ]]
        then
            echo "No such file $FILE_DIFF1"
            exit 1
        fi

        shift $(( OPTIND - 1 ))
        i=0
        for option in "$@"; do
            if [[ $i > 0 ]]
            then
                usage
                exit 1
            fi
            i=$i+1
            FILE_DIFF2=$option
            if ! [[ -f $FILE_DIFF2 ]]
            then
                echo "No such file $FILE_DIFF2"
                exit 1
            fi
        done
fi

if [[ -z $FILE_DIFF1 ]]
then
    dump_stats
else
    diff_stats $FILE_DIFF1 $FILE_DIFF2
fi
