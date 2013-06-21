#!/bin/bash

BRANCH_MASTER=performance
BRANCH_LKMA=lkma

MASTER_FOLDER=branches/$BRANCH_MASTER/data
LKMA_FOLDER=branches/$BRANCH_LKMA/data

./parse_results.sh $MASTER_FOLDER/kernel
./parse_results.sh $LKMA_FOLDER/kernel

for file in ${MASTER_FOLDER}/kernel/image_data/*
do
    echo "File is : $file"
    file_short=$(echo $file | awk -F '/' '{print $NF}')
    echo "File is : $file_short"
    ./custom_plot.sh \
        ${MASTER_FOLDER}/kernel/image_data/$file_short "master_$file_short" \
        ${LKMA_FOLDER}/kernel/image_data/$file_short "lkma_$file_short"
done


./parse_results.sh $MASTER_FOLDER/module
./parse_results.sh $LKMA_FOLDER/module

for file in ${MASTER_FOLDER}/module/image_data/*
do
    echo "File is : $file"
    file_short=$(echo $file | awk -F '/' '{print $NF}')
    echo "File is : $file_short"
    ./custom_plot.sh \
        ${MASTER_FOLDER}/module/image_data/$file_short "master_$file_short" \
        ${LKMA_FOLDER}/module/image_data/$file_short "lkma_$file_short"
done
