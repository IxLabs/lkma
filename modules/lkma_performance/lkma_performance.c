#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>

#include <linux/timex.h>

#define LKMA_PROCFS	1

#if LKMA_PROCFS
#include <linux/fs.h>		/* for basic filesystem */
#include <linux/proc_fs.h>	/* for the proc filesystem */
#include <linux/seq_file.h>	/* for sequence files */
#else
#include <linux/sysfs.h>
#endif

MODULE_DESCRIPTION("LKMA performance test suite");
MODULE_AUTHOR("Ghennadi Procopciuc");
MODULE_LICENSE("GPL");

#define MAKE_KERNEL_TEST	0
#define KB			1024
#define MB			(1024 * KB)
#define PADDING			"....................."

#define LKMA_OOM		-2

#if LKMA_PROCFS
#define PROC_FILENAME		(THIS_MODULE->name)
#else
#define SYSFS_FILENAME		(THIS_MODULE->name)
#endif

#define VMALLOC_MAX_SIZE	(300 * 1024 * 1024)
#define DEFAULT_FLAGS_ID	0

#define NUM_ALLOCATION		array_size(allocators)

#define get_mb(size)		(size / MB)
#define get_kb(size)		((size % MB) / KB)
#define get_b(size)		(size % KB)

#define DEBUG
#define klog(format, ...)			\
	({					\
	 pr_info("[%s] " format "\n",		\
		 __func__, ##__VA_ARGS__);	\
	 })

#define kerr(format, ...) \
	({\
	 pr_alert("[%s] ERROR : " format "\n",	\
		 __func__, ##__VA_ARGS__);	\
	 })

#define function_name(function) (#function)
#define array_size(array)		(sizeof(array) / sizeof(*array))

enum {
	KERNEL_TEST,
	MODULE_TEST
};

extern void *lkma_kernel_kmalloc(size_t size, gfp_t flags);
extern void *lkma_kernel_kzalloc(size_t size, gfp_t flags);
extern void *lkma_kernel_vmalloc(size_t size, gfp_t flags);
inline void *lkma_vmalloc(size_t size, gfp_t flags);

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

struct allocator {
	void *(*alloc_func) (size_t, gfp_t);
	void (*free_func) (const void *);
	const char *function_name;
	unsigned long max_size;
};

struct allocator allocators[] = {
	{
	 .alloc_func = kmalloc,
	 .free_func = kfree,
	 .function_name = function_name(kmalloc_module),
	 .max_size = KMALLOC_MAX_SIZE},
	{
	 .alloc_func = kzalloc,
	 .free_func = kfree,
	 .function_name = function_name(kzalloc_module),
	 .max_size = KMALLOC_MAX_SIZE},
	{
	 .alloc_func = lkma_vmalloc,
	 .free_func = vfree,
	 .function_name = function_name(vmalloc_module),
	 .max_size = VMALLOC_MAX_SIZE},
	{
	 .alloc_func = lkma_kernel_kmalloc,
	 .free_func = kfree,
	 .function_name = function_name(kmalloc_kernel),
	 .max_size = KMALLOC_MAX_SIZE},
	{
	 .alloc_func = lkma_kernel_kzalloc,
	 .free_func = kfree,
	 .function_name = function_name(kzalloc_kernel),
	 .max_size = KMALLOC_MAX_SIZE},
	{
	 .alloc_func = lkma_kernel_vmalloc,
	 .free_func = vfree,
	 .function_name = function_name(vmalloc_kernel),
	 .max_size = VMALLOC_MAX_SIZE}

};

#if LKMA_PROCFS
static struct proc_dir_entry *proc_entry;
#else
static struct kobject *lkma_kobj;
#endif

static unsigned long step = 100;
static unsigned long lower_limit = 1;
static unsigned long upper_limit = 1 * MB;
static int kernel_test;

void *lkma_vmalloc(size_t size, gfp_t flags)
{
	return vmalloc(size);
}

static unsigned long long generic_lkma_alloc(struct allocator *alloc,
					     size_t alloc_size, int flag_id)
{
	void *address;
	unsigned long long start, end;

	if (!alloc) {
		kerr("NULL pointer");
		return -1;
	}

	start = get_cycles();
	address = alloc->alloc_func(alloc_size, lkma_flags[flag_id]);
	end = get_cycles();

	if (address == NULL) {
		kerr("Failed to allocate memory with : %pf [size = %zu]",
		     alloc->alloc_func, alloc_size);
		return LKMA_OOM;
	}

	alloc->free_func(address);

	return end - start;
}

#if LKMA_PROCFS
static ssize_t test(struct seq_file *m,
		    struct allocator *alloc, size_t size, size_t offset)
#define lkma_sprintf(buffer, offset, ...) seq_printf(buffer, ##__VA_ARGS__)
#else
static ssize_t test(char *m,
		    struct allocator *alloc, size_t size, size_t offset)
#define lkma_sprintf(buffer, offset, ...) sprintf(buffer + offset, ##__VA_ARGS__)
#endif
{

	unsigned long long time;
	size_t mb, kb, b;
	int padding = 0;

	mb = get_mb(size);
	kb = get_kb(size);
	b = get_b(size);

	time = generic_lkma_alloc(alloc, size, DEFAULT_FLAGS_ID);

	offset += lkma_sprintf(m, offset, "Test %s", alloc->function_name);
	offset += lkma_sprintf(m, offset, " %zu%.*s.......%llu\n",
			       size, padding, PADDING, time);
	return offset;
#undef sprintf
}

#if LKMA_PROCFS
static int lkma_tests(struct seq_file *m, void *v)
#else
static struct {
	bool end;
	int allocator_id;
	size_t start;
	size_t limit;
} test_offset = {
	.end = false,
	.allocator_id = 0,
	.start = -1,
	.limit = -1,
};

static ssize_t lkma_tests(struct kobject *kobj, struct kobj_attribute *attr,
			  char *m)
#endif
{
	int i;
	size_t size, limit, start;
	ssize_t offset = 0;
	int allocator_id = (kernel_test ? NUM_ALLOCATION / 2 : 0);

#if !LKMA_PROCFS
	if (test_offset.end == false) {
		klog("Allocator start test : %d", allocator_id);
		allocator_id = test_offset.allocator_id;
	}
#endif

	for (i = allocator_id; i < allocator_id + NUM_ALLOCATION / 2; i++) {
#if LKMA_PROCFS
		limit = min(allocators[i].max_size, upper_limit);
		start = min(allocators[i].max_size, lower_limit);
#else
		if (test_offset.start != -1) {
			start = test_offset.start;
			limit = test_offset.limit;
			klog("Set start = %zu limit = %zu", start, limit);
			test_offset.start = -1;
			test_offset.limit = -1;
		} else {
			limit = min(allocators[i].max_size, upper_limit);
			start = min(allocators[i].max_size, lower_limit);
		}
#endif

		for (size = start; size <= limit; size += step) {
#if !LKMA_PROCFS
			if (offset + 60 > PAGE_SIZE) {
				test_offset.end = false;
				test_offset.start = size;
				test_offset.end = limit;
				goto out;
			}
#endif
			offset = test(m, &allocators[i], size, offset);
		}
	}

#if LKMA_PROCFS
	return 0;
#else
 out:
	klog("Offset = %zu", offset);
	if (offset + 60 > PAGE_SIZE) {
		klog("Return PAGE_SIZE");
		return offset;
	}

	test_offset.end = true;
	return offset;
#endif
}

#if LKMA_PROCFS
static ssize_t lkma_config(struct file *file, const char __user *buffer,
			   size_t count, loff_t *data)
#else
static ssize_t lkma_config(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buffer, size_t count)
#endif
{
	char *temp;
#if LKMA_PROCFS
	temp = kmalloc(count, GFP_KERNEL);

	temp = kmalloc(count, GFP_KERNEL);
	if (!temp) {
		kerr("Unable to alloc memory with kmalloc");
		return -ENOMEM;
	}

	if (copy_from_user(temp, buffer, count)) {
		kerr("copy_from_)user failed");
		return -EFAULT;
	}
#else
	temp = (char *)buffer;
#endif

	sscanf(temp, "%lu %lu %lu %d", &lower_limit, &upper_limit, &step,
	       &kernel_test);

	/* Make sure that lower_limit is at least 1,
	   otherwise vmalloc will fail */
	if (!lower_limit)
		lower_limit = 1;

#if LKMA_PROCFS
	kfree(temp);
#endif
	return count;
}

#if LKMA_PROCFS
static int lkma_open(struct inode *inode, struct file *file)
{
	return single_open(file, lkma_tests, NULL);
}

static const struct file_operations lkma_fops = {
	.owner = THIS_MODULE,
	.open = lkma_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = lkma_config,
};
#else
static struct kobj_attribute lkma_attribute =
__ATTR(performance, 0666, lkma_tests, lkma_config);

static struct attribute *attrs[] = {
	&lkma_attribute.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};
#endif

static int lkma_test_init(void)
{
#if LKMA_PROCFS
	proc_entry = proc_create(PROC_FILENAME, 0, NULL, &lkma_fops);

	if (proc_entry == NULL) {
		kerr("Couldn't create proc entry");
		return -ENOMEM;
	}
#else
	int ret;

	lkma_kobj = kobject_create_and_add("lkma", kernel_kobj);
	if (!lkma_kobj)
		return -ENOMEM;

	ret = sysfs_create_group(lkma_kobj, &attr_group);
	if (ret)
		kobject_put(lkma_kobj);
#endif

	pr_debug("[%s] Module %s loaded\n", THIS_MODULE->name,
	       THIS_MODULE->name);
	return 0;
}

static void lkma_test_exit(void)
{
#if LKMA_PROCFS
	remove_proc_entry(PROC_FILENAME, NULL);
#else
	kobject_put(lkma_kobj);
#endif

	pr_debug("[%s] Module %s unloaded\n", THIS_MODULE->name,
	       THIS_MODULE->name);
}

module_init(lkma_test_init);
module_exit(lkma_test_exit);
