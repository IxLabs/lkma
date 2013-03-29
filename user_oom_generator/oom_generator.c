/**
 * This program is used to generate an OOM in userspace.
 * It allocates memory forever.
 * @author Ghennadi Procopciuc
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define KB      (1 * 1024)
#define MB      (1 * 1024 * KB)
#define STEP    (200 * KB)
#define TRUE    TRUE

/**
 * Print use case
 */
static void print_usage(char **argv)
{
	printf("Usage : %s [name]", argv[0]);
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
	long int totalAllocated = 0;
	char *a;
	int i;

	if (argc > 2) {
		print_usage(argv);
	}

	while (TRUE) {
        /* Make sure that not only virtual memory is allocated */
		a = calloc(1, STEP);
		totalAllocated += STEP;
		if (argc == 1) {
			printf("Process : %ld MBi\n", totalAllocated / MB);
		} else {
			printf("Process %s : %ld MBi\n", argv[1],
			       totalAllocated / MB);
		}
		sleep(1);
	}
	return EXIT_SUCCESS;
}
