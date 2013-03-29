/**
 * Simple test program for allocator module.
 * It will try to allocate memory until an oom will be generated.
 */
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "../include/allocator.h"

#define DEVICE_PATH "/dev/memory_allocator"
#define MB          (1 * 1024 * 1024)
#define STEP        (1 * MB)
#define TRUE        1

int main(int argc, char **argv)
{

	int fd = open(DEVICE_PATH, O_RDONLY);
	long allocated = 0;
	long div = 1;

	while(TRUE){
		printf("Allocated memory : %ld MB\n", allocated / MB);
		/* If we really want to get an oom should reduce
		 * the allocation step.*/
		if (ioctl(fd, ALLOCATOR_IOCTL_ALLOC, STEP / div) < 0) {
			printf("Allocation step : %ld\n", STEP / div);
			div *= 2;
		} else {
			allocated += STEP / div;
		}
	}

	close(fd);

	return 0;
}
