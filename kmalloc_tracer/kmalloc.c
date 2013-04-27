/*
 * Simple kmalloc probe example
 * @author Ghennadi Procopciuc
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <trace/mm.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ghennadi Procopciuc");
MODULE_DESCRIPTION("kmalloc tracepoint probes sample");

static void probe_mm_my_kmalloc_trace(void *ignore,
				      const char *filename, size_t size)
{
	printk(KERN_ALERT "filename = %s size = %d\n", filename, size);
}

static int __init tp_sample_trace_init(void)
{
	int ret;
	int i;
	char *a;
	printk(KERN_ALERT "Module loaded\n");

	ret =
	    register_trace_mm_my_kmalloc_trace(probe_mm_my_kmalloc_trace, NULL);
	WARN_ON(ret);

	for (i = 0; i < 10000; i++) {
		a = kmalloc(i, GFP_KERNEL);
		kfree(a);
	}
	return 0;
}

static void __exit tp_sample_trace_exit(void)
{
	printk(KERN_ALERT "Module unloaded\n");
	unregister_trace_mm_my_kmalloc_trace(probe_mm_my_kmalloc_trace, NULL);
	tracepoint_synchronize_unregister();
}

module_init(tp_sample_trace_init);
module_exit(tp_sample_trace_exit);
