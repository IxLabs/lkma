#!/usr/bin/awk -f

BEGIN {
	previous=-1
}

/[0-9]+ [0-9]+/{
	if(previous == -1){
		previous=$2
		print $1" "$2
	} else {
		if($2 < previous * 7){
			previous=$2
			print $1" "$2
		}
	}
}
