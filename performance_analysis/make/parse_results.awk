#!/usr/bin/awk -f

/[0-9]+\.[0-9]+/ {
    master+=$1
    lkma+=$2
    counts++
}

END {
    print "\n\nAverage"
    print "Master : " (master / counts) " s"
    print "LKMA   : " (lkma / counts) " s"
}
