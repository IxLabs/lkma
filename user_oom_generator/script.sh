#!/bin/bash

# Starts 100 processes, each of them is trying to allocate
# memory as much as possible
for i in $(seq 1 1 100)
do
	./oom_generator $i &
done
