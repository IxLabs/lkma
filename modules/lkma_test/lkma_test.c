#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>

MODULE_DESCRIPTION("LKMA Test suite");
MODULE_AUTHOR("Ghennadi Procopciuc");
MODULE_LICENSE("GPL");

#define MAX_ALLOCATIONS	10
#define MAKE_KERNEL_TEST	0

#ifdef DEBUG
#define klog(format, ...) \
	({\
	printk(KERN_ALERT "[%s] " format "\n",\
	       __func__, ##__VA_ARGS__);\
	})
#else
#define klog(format, ...)
#endif

#define kerr(format, ...) \
	({\
	printk(KERN_ALERT "[%s] ERROR : " format "\n",\
	       __func__, ##__VA_ARGS__);\
	})

#ifndef ALLOCATION_TYPES
enum {
	KMALLOC_KERNEL,
	KMALLOC_ATOMIC,
	KZALLOC_KERNEL,
	KZALLOC_ATOMIC,
	VMALLOC
};
#if MAKE_KERNEL_TEST
extern void *allocation_test(size_t size, int type);
#else
#define allocation_test(size, type) (NULL)
#endif /* MAKE_KERNEL_TEST */
#else  /* ALLOCATION_TYPES */
extern void *allocation_test(size_t size, int type);
#endif

enum {
	KERNEL_TEST,
	MODULE_TEST
};

struct {
	void *address;
	int type;
	bool empty;
} allocations[MAX_ALLOCATIONS] = {
	[0 ... MAX_ALLOCATIONS - 1] = {.empty = true}
};

static int get_empty_position(void)
{
	int i;

	for (i = 0; i < MAX_ALLOCATIONS; i++) {
		if (allocations[i].empty)
			return i;
	}

	return -1;
}

static int add_address(void *address, int type)
{
	int index;

	index = get_empty_position();
	if (index == -1)
		return -1;

	allocations[index].address = address;
	allocations[index].type = type;
	allocations[index].empty = false;

	return 0;
}

static int del_address(int index)
{
	if (index < 0 || index > MAX_ALLOCATIONS) {
		kerr(KERN_ALERT "Wrong index %d\n", index);
		return -1;
	}

	if (allocations[index].empty)
		return 0;

	if (allocations[index].type != VMALLOC)
		kfree(allocations[index].address);
	else
		vfree(allocations[index].address);

	allocations[index].empty = true;
	return 0;
}

static void delete_all(void)
{
	int i;

	/* Free all elements */
	for (i = 0; i < MAX_ALLOCATIONS; i++)
		del_address(i);
}

static void kmalloc_test(size_t size, int mem_type, int test_type)
{
	void *address = NULL;
	int flags;

	if (mem_type == KMALLOC_KERNEL)
		flags = GFP_KERNEL;

	if (mem_type == KMALLOC_ATOMIC)
		flags = GFP_ATOMIC;

	if (test_type == KERNEL_TEST)
		address = allocation_test(size, mem_type);

	if (test_type == MODULE_TEST)
		address = kmalloc(size, flags);

	add_address(address, mem_type);
}

static void vmalloc_test(size_t size, int test_type)
{
	void *address = NULL;

	if (test_type == KERNEL_TEST)
		address = allocation_test(size, VMALLOC);

	if (test_type == MODULE_TEST)
		address = vmalloc(size);

	add_address(address, VMALLOC);
}

static void generic_test(int test_type)
{
	int i;

	kmalloc_test(1, KMALLOC_KERNEL, test_type);
	delete_all();
	vmalloc_test(12, test_type);
	delete_all();

	for (i = 0; i < MAX_ALLOCATIONS; i++)
		kmalloc_test(100 - i, KMALLOC_KERNEL, test_type);
	delete_all();

	for (i = 0; i < MAX_ALLOCATIONS; i++)
		kmalloc_test(100 + i, KMALLOC_KERNEL, test_type);
	delete_all();

	for (i = 0; i < MAX_ALLOCATIONS; i++)
		vmalloc_test(8 * 1024 * 1024 - i * 100, test_type);
	delete_all();

	for (i = 0; i < MAX_ALLOCATIONS; i++)
		vmalloc_test(8 * 1024 * 1024 - i * 100, test_type);
	delete_all();

	for (i = 0; i < MAX_ALLOCATIONS / 2; i++) {
		vmalloc_test(8 * 1024 * 1024 - i * 100, test_type);
		kmalloc_test(i * 10, KMALLOC_KERNEL, test_type);
	}
	delete_all();

	for (i = 0; i < MAX_ALLOCATIONS; i++)
		kmalloc_test(100 - i, KMALLOC_KERNEL, test_type);
	delete_all();
}

static int lkma_test_init(void)
{

	generic_test(MODULE_TEST);
	generic_test(KERNEL_TEST);
	return 0;
}

static void lkma_test_exit(void)
{
	delete_all();
}

module_init(lkma_test_init);
module_exit(lkma_test_exit);
