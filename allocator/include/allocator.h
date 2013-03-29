#ifndef __ALLOCATOR_H__
#define __ALLOCATOR_H__		1

#include <asm/ioctl.h>

#define ALLOCATOR_IOCTL_ALLOC	_IOC(_IOC_NONE,  'k', 6, 0)

#ifdef __KERNEL__

#define LOG_LEVEL		KERNEL_
#define ALLOCATOR_MAJOR		42
#define ALLOCATOR_MINOR		0
#define NUM_MINORS		1
#define MODULE_NAME		"memory_allocator"

#define logk(format, ...)\
	printk(LOG_LEVEL "%s : " format "\n", \
	       THIS_MODULE->name, \
##__VA_ARGS__)

#endif

#endif
