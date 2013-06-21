#!/bin/bash

# ./custom_plot.sh image_data/kmalloc "kmalloc" image_data/kzalloc "kzalloc" image_data/vmalloc "vmalloc"
#set -x
ARGUMENTS=($@)
temp=.temp

MINX=0
MINY=0
MAXX=0
MAXY=0

function check_file(){
    local file=$1
    if ! [ -f $file ]
    then
        echo "No such directory : '$file'" 1>&2
        exit 1
    fi
}

function sanity_checks(){
    [ -z ${ARGUMENTS[0]} ] && echo "No arguments :(" 1>&2 && exit 1

    if [ $(( ${#ARGUMENTS[@]} % 2 )) -eq 1 ]
    then
        echo "Each file must be accompanied by a name." 1>&2
        exit 1
    fi

    for i in $(seq 0 1 ${#ARGUMENTS[@]})
    do
        if [ $(( i % 2 )) -eq 0 ]
        then
            FILES[$(( i / 2 ))]=${ARGUMENTS[$i]}
            check_file ${ARGUMENTS[$i]}
        else
            NAMES[$(( i / 2 ))]=${ARGUMENTS[$i]}
        fi
    done
}

function get_image_name(){
    local result="";

    for name in "${NAMES[@]}"
    do
        result="${result}_${name}"
    done

    echo $result
}

#local plot_cmd="set terminal postscript enhanced color font 'Helvetica,10'; \
function plot(){
    local image=$(echo $(get_image_name)).gif
#local plot_cmd="set terminal postscript enhanced color font 'Helvetica,10'; \
    local plot_cmd="set terminal gif size 2000,2000; \
                    set output \"$image\"; \
                    set size nosquare; \
                    set grid; \
                    set multiplot; \
                    set xrange [$MINX:$MAXX]; \
                    set yrange [$MINY:$MAXY]; \
                    set format x \"%.0s%cB\"; \
                    set format y \"%.0s%c\"; \
                    set xlabel \"Allocated size\"; \
                    set ylabel \"Run Time (cycles)\"; \
                    plot ";
    for i in $(seq 0 1 ${#NAMES[@]})
    do
        plot_cmd="$plot_cmd \
                  \"${FILES[$i]}\"  using 1:2 title \"${NAMES[$i]}\" smooth bezier with lines lw 3"
        if [ $i -eq ${#NAMES[@]} ]
        then
            plot_cmd="$plot_cmd;"
        else
            plot_cmd="$plot_cmd, "
        fi
    done

    plot_cmd="$plot_cmd quit"

    gnuplot <<< $plot_cmd
}

sanity_checks $1

for file in ${FILES[@]}
do
    sort -h $file > $temp
    cat $temp > $file

    limits=$(cat $file | awk '
                     {x=$1;
                      y=$2;
                      if (minx == "") {
                        minx=maxx=x;
                        miny=maxy=y;
                      }
                      if (x < minx)
                          minx=x;
                      if (x > maxx)
                          maxx=x;
                      if (y < miny)
                          miny=y;
                      if (y > maxy)
                          maxy=y;
                      }
                      END{print int(minx)" "int(maxx)" "int(miny)" "int(maxy)}')
    limits=($limits)
        echo "limits : ${limits[@]}"

    if [[ MAXX -eq 0 ]]
    then
        MINX=${limits[0]}
        MAXX=${limits[1]}
        MINY=${limits[2]}
        MAXY=${limits[3]}
    else
        [[ ${limits[0]} -lt $MINX ]] && MINX="${limits[0]}";
        [[ ${limits[1]} -lt $MAXX ]] && MAXX="${limits[1]}";
        [[ ${limits[2]} -lt $MINY ]] && MINY="${limits[2]}";
        [[ ${limits[3]} -lt $MAXY ]] && MAXY="${limits[3]}";

    fi
done

plot

rm $temp
#set +x
