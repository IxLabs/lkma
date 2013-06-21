#!/bin/bash

#set -x
temp=.temp

function sanity_checks(){
    local data_dir=$1;

    if ! [[ -d $data_dir ]]
    then
        echo "No such directory : '$data_dir'" 1>&2
        exit 1
    fi

    DATA=$data_dir
    IMAGE_DATA=$data_dir/image_data
    IMAGES=$data_dir/images
}

plot(){
    local file=$1
    local image=$IMAGES/$(echo $file | awk -F '/' '{print $NF}').gif
    #local plot_cmd="set terminal postscript enhanced color font 'Helvetica,10'; \
    local plot_cmd="set terminal gif size 2000,2000; \
                    set output \"$image\"; \
                    set size 1,1; \
                    set grid; \
                    set format x \"%.0s%cB\"; \
                    set format y \"%.0s%c\"; \
                    set xlabel \"Allocated size\"; \
                    set ylabel \"Run Time (cycles)\"; \
                    plot \"$file\"  using 1:2 title \"$(echo $file | awk -F '/' '{print $NF}')\" smooth bezier with lines lw 5;\
                    quit"

    gnuplot <<< $plot_cmd
}

sanity_checks $1

mkdir -p $IMAGE_DATA
mkdir -p $IMAGES

./parse_results.awk -v image_data=$IMAGE_DATA $DATA/*.data

for file in $IMAGE_DATA/*
do
    sort -h $file > $temp
    ./filter_results.awk $temp > $file
    plot $file
done

rm $temp
#set +x
