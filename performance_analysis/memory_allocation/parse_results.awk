#!/usr/bin/awk -f

BEGIN {
    FS="."
}

/.+[^.]/ {
    gsub(/Test[ \t]+/, "", $1);
    counts[$1]++;
    sum[$1] += $NF;
}

END {
    for(test in counts){
        split(test, array, " ");
        print array[2] " " (sum[test] / counts[test]) > image_data"/"array[1]
#printf("--%s-- %.3f ++%s++ ==%s==\n", test, sum[test] / counts[test],
#              array[1], array[2]);
    }
}
