#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/net.h>

MODULE_DESCRIPTION("LKMA Test suite");
MODULE_AUTHOR("Ghennadi Procopciuc");
MODULE_LICENSE("GPL");

#define MAX_ALLOCATIONS	    	1000
#define MAKE_KERNEL_TEST	0

#define LKMA_OOM		-2
#define FULL_BUFFER		-1

#define VMALLOC_MAX_SIZE	300 * 1024 * 1024
#define DEFAULT_FLAGS_ID	0

#define KB 1024
#define MB 1024 * KB

#define NUM_ALLOCATION	  	array_size(lkma_alloc_func)

#define DEBUG
#ifdef DEBUG
#define klog(format, ...)				\
	({						\
	 printk(KERN_ALERT "[%s] " format "\n",		\
			__func__, ##__VA_ARGS__);	\
	 })
#else
#define klog(format, ...)
#endif

#define kerr(format, ...) \
	({\
	 printk(KERN_ALERT "[%s] ERROR : " format "\n",	\
			__func__, ##__VA_ARGS__);	\
	 })

#define array_size(array)		(sizeof(array) / sizeof(*array))
#define check_array_size(array, size)	(array_size(array) == size)

#define zero_memory_allocated()		(THIS_MODULE->allocated_size == 0)

#define check_test(ret, test_name)						\
	({									\
	   if (ret == 0 && zero_memory_allocated()) {				\
		 printk(KERN_ALERT "[%s] Test %20s .............. passed\n",	\
				THIS_MODULE->name, test_name);			\
	   } else {								\
		 printk(KERN_ALERT "[%s] Test %20s .............. failed\n",	\
				THIS_MODULE->name, test_name);			\
	   }									\
	 })

#define BUILD_TEST(function, test_name)					\
static int function##_##test_name(void){				\
	return function_test(function, test_name##_test_handler);	\
}

#define start_test(test_name)			\
	({					\
		int ret = test_name();		\
		check_test(ret, #test_name);	\
	 })

enum {
	KERNEL_TEST,
	MODULE_TEST
};

void *lkma_vmalloc(size_t size, gfp_t flags);

struct {
	void *address;
	void (*free_funct) (const void *);
	bool empty;
} allocations[MAX_ALLOCATIONS] = {
	[0 ... MAX_ALLOCATIONS - 1] = {.empty = true}
};

/* kmalloc flags ... */
gfp_t lkma_flags[] = {
	GFP_KERNEL,
	GFP_ATOMIC,
	GFP_NOWAIT,
	GFP_NOIO,
	GFP_NOFS,
	GFP_TEMPORARY,
	GFP_USER,
	GFP_HIGHUSER,
	GFP_HIGHUSER_MOVABLE,
	GFP_IOFS,
	GFP_TRANSHUGE,
};

void *(*lkma_alloc_func[]) (size_t, gfp_t) = {
	kmalloc,
	kzalloc,
	lkma_vmalloc,
};

void (*lkma_free_func[]) (const void *) = {
	kfree,
	kfree,
	vfree,
};

int lkma_max_size[] = {
	KMALLOC_MAX_SIZE,
	KMALLOC_MAX_SIZE,
	VMALLOC_MAX_SIZE,
};

void *lkma_vmalloc(size_t size, gfp_t flags)
{
	return vmalloc(size);
}

static int get_empty_position(void)
{
	int i;

	for (i = 0; i < MAX_ALLOCATIONS; i++) {
		if (allocations[i].empty)
			return i;
	}

	return -1;
}

static int add_address(void *address, void (*free_funct) (const void *))
{
	int index;

	index = get_empty_position();
	if (index == -1)
		return -1;

	allocations[index].address = address;
	allocations[index].free_funct = free_funct;
	allocations[index].empty = false;

	return index;
}

static int del_address(int index)
{
	if (index < 0 || index > MAX_ALLOCATIONS) {
		kerr("Wrong index %d\n", index);
		return -1;
	}

	if (allocations[index].empty) {
		kerr("Empty position  %d ..", index);
		return 0;
	}

	allocations[index].free_funct(allocations[index].address);
	allocations[index].empty = true;
	return 0;
}

static int delete_all(void)
{
	int i;
	int ret;

	/* Free all elements */
	for (i = 0; i < MAX_ALLOCATIONS; i++) {
		ret = del_address(i);
		if (ret) {
			return ret;
		}
	}

	return 0;
}

static int generic_lkma_alloc(int alloc_id, size_t alloc_size, int flag_id)
{
	int ret;
	void *address =
	    lkma_alloc_func[alloc_id] (alloc_size, lkma_flags[flag_id]);

	if (address == NULL) {
		kerr("Failed to allocate memory with : %pf",
		     lkma_alloc_func[alloc_id]);
		return LKMA_OOM;
	}

	ret = add_address(address, lkma_free_func[alloc_id]);
	if (ret < 0) {
		lkma_free_func[alloc_id] (address);
		kerr("Failed to add address %p to `allocations'\n", address);
		return FULL_BUFFER;
	}

	return ret;
}

static int function_test(void *(*alloc_funct) (size_t, gfp_t),
			 int (*handler) (void *))
{
	int i;
	for (i = 0; i < NUM_ALLOCATION; i++) {
		if (alloc_funct == lkma_alloc_func[i]) {
			return handler((void *)i);
		}
	}

	kerr("Function %pf not found\n", alloc_funct);
	return -1;
}

static int sanity_checks(void)
{
	int i, flag_id;
	int ret;
	size_t alloc_size = 1;

	for (i = 0; i < NUM_ALLOCATION; i++) {
		for (flag_id = 0; flag_id < NUM_ALLOCATION; flag_id++) {
			ret = generic_lkma_alloc(i, alloc_size, flag_id);

			ret = del_address(ret);
			if (ret) {
				return ret;
			}
		}
	}

	return 0;
}

static int simple_test_handler(void *data)
{
	int i;
	int ret;
	int alloc_id = (int)data;

	for (i = 0; i < MAX_ALLOCATIONS; i++) {
		ret = generic_lkma_alloc(alloc_id, PAGE_SIZE, DEFAULT_FLAGS_ID);
	}

	delete_all();
	return 0;
}

static int max_test_handler(void *data)
{
	int i;
	int ret;
	int alloc_id = (int)data;

	for (i = 0; i < array_size(lkma_flags); i++) {
		if (lkma_flags[i] & GFP_HIGHUSER) {
			continue;
		}
		ret = generic_lkma_alloc(alloc_id, lkma_max_size[alloc_id], i);
		ret = del_address(ret);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int var_size_test_handler(void *data)
{
	int i;
	int ret;
	int alloc_id = (int)data;
	int pages = 1;

	for (i = 0; i < MAX_ALLOCATIONS; i++) {
 try_again:
		pages %= 6;
		pages++;

		ret =
		    generic_lkma_alloc(alloc_id, pages * PAGE_SIZE,
				       DEFAULT_FLAGS_ID);
		if (ret == LKMA_OOM) {
			if (pages == 1) {
				return -1;
			}
			pages--;
			goto try_again;
		}

		if (ret < 0) {
			return ret;
		}
	}

	delete_all();

	return 0;
}

static int mixed_allocations(void)
{
	int i, flag_id, ret;

	while (true) {
		for (i = 0; i < NUM_ALLOCATION; i++) {
			for (flag_id = 0; flag_id < NUM_ALLOCATION; flag_id++) {
				ret = generic_lkma_alloc(i, PAGE_SIZE, flag_id);
				if (ret == LKMA_OOM || ret == FULL_BUFFER) {
					break;
				} else {
					if (ret < 0) {
						return ret;
					}
				}
			}

			if (ret == LKMA_OOM || ret == FULL_BUFFER) {
				break;
			}
		}

		if (ret == LKMA_OOM || ret == FULL_BUFFER) {
			break;
		}
	}

	delete_all();
	return 0;
}

BUILD_TEST(kmalloc, simple);
BUILD_TEST(kzalloc, simple);
BUILD_TEST(lkma_vmalloc, simple);

BUILD_TEST(kmalloc, max);
BUILD_TEST(kzalloc, max);
BUILD_TEST(lkma_vmalloc, max);

BUILD_TEST(kmalloc, var_size);
BUILD_TEST(kzalloc, var_size);
BUILD_TEST(lkma_vmalloc, var_size);

static int lkma_test_init(void)
{

	int func_number;
	bool bret;

	func_number = NUM_ALLOCATION;

	bret = check_array_size(lkma_free_func, func_number);
	if (!bret) {
		kerr("The number of functions that allocate memory should be "
				"the same as the number of functions that "
				"release it.");
		return -1;
	}

	bret = check_array_size(lkma_max_size, func_number);
	if (!bret) {
		kerr("Please check the maximum sizes array.");
		return -1;
	}

    size_t i,j;
    void *address;
    for(i = 0 * MB; i < 60 * MB; i += 1 * MB){
        for(j = i; j < i + 1 * MB; j += KB){
            klog("Allocating : %zu\n", j);
            address = vmalloc(j);
            vfree(address);
        }
    }
#if 0
	printk(KERN_DEBUG "[%s] Module %s loaded\n", THIS_MODULE->name,
	       THIS_MODULE->name);

	start_test(sanity_checks);

	start_test(kmalloc_simple);
	start_test(kzalloc_simple);
	start_test(lkma_vmalloc_simple);

	start_test(kmalloc_max);
	start_test(kzalloc_max);
	start_test(lkma_vmalloc_max);

	start_test(kmalloc_var_size);
	start_test(kzalloc_var_size);
	start_test(lkma_vmalloc_var_size);

	start_test(mixed_allocations);
#endif
	return 0;
}

static void lkma_test_exit(void)
{
	/* Just to be sure ... */
	delete_all();
	printk(KERN_DEBUG "[%s] Module %s unloaded\n", THIS_MODULE->name,
	       THIS_MODULE->name);
}

module_init(lkma_test_init);
module_exit(lkma_test_exit);
