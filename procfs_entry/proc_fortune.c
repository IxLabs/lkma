/**
 * This module adds an entry (dump_tasks) in procfs, which dump the status of
 * all active processes in terms of oom killer.
 * @author Ghennadi Procopciuc
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/sched.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Adds dump_tasks in /proc");
MODULE_AUTHOR("Ghennadi Procopciuc");

#define LOG_LEVEL           KERN_DEBUG
#define MAX_ENTRY_LENGTH    86
#define DATA_LENGTH         69
#define PROC_FILENAME       "dump_tasks"
#define MIN(A, B)           ((A) < (B) ? (A) : (B))

#define logk(format, ...)\
	printk(LOG_LEVEL "%s : " format "\n", \
	       THIS_MODULE->name, \
##__VA_ARGS__)

static struct proc_dir_entry *proc_entry;

static char *buffer;		// Buffer for output
static int buffer_size;		// Buffer size

static const char *header =
    "[ pid ]   uid  tgid total_vm      rss nr_ptes swapents oom_score_adj name\n";

static inline unsigned long get_mm_counter(struct mm_struct *mm, int member)
{
	long val = atomic_long_read(&mm->rss_stat.count[member]);

#ifdef SPLIT_RSS_COUNTING
	/*
	 * counter is updated in asynchronous manner and may go to minus.
	 * But it's never be expected number for users.
	 */
	if (val < 0)
		val = 0;
#endif
	return (unsigned long)val;
}

/**
 * Returns Resident Set Size (rss).
 * RSS is the number of resident pages for this process. It should be noted that the
 * global zero page is not accounted by RSS.
 */
static inline unsigned long get_mm_rss(struct mm_struct *mm)
{
	return get_mm_counter(mm, MM_FILEPAGES) +
	    get_mm_counter(mm, MM_ANONPAGES);
}

static struct task_struct *find_lock_task_mm(struct task_struct *p)
{
	struct task_struct *t = p;

	do {
		task_lock(t);
		if (likely(t->mm)) {
			return t;
		}
		task_unlock(t);
	} while_each_thread(p, t);

	return NULL;
}

/**
 * Builds an output for current state of all processes.
 * All data will be stored in buffer.
 */
static int build_buffer(void)
{
	struct task_struct *task, *p;
	int count = 0;
	int offset;

	/* Gets the number of active processes */
	rcu_read_lock();
	for_each_process(task) {
		count++;
	}
	rcu_read_unlock();

	buffer = kmalloc(MAX_ENTRY_LENGTH * (count + 1), GFP_KERNEL);
	if (buffer == NULL) {
		return -ENOMEM;
	}

	memcpy(buffer, header, strlen(header));
	offset = strlen(header);

	rcu_read_lock();
	for_each_process(p) {

		task = find_lock_task_mm(p);
		if (!task) {
			continue;
		}

		/* Builds the same output as dump_tasks function from oom_kill.c */
		sprintf(buffer + offset,
			"[%5d] %5d %5d %8lu %8lu %7lu %8lu         %5d %s\n",
			task->pid, from_kuid(&init_user_ns,
					     task_uid(task)),
			task->tgid, task->mm->total_vm,
			get_mm_rss(task->mm), task->mm->nr_ptes,
			get_mm_counter(task->mm, MM_SWAPENTS),
			task->signal->oom_score_adj, task->comm);
		task_unlock(task);
		offset += DATA_LENGTH + strlen(task->comm) + 1;
	}
	rcu_read_unlock();

	return offset;
}

int dump_read(char *page, char **start, off_t off,
	      int count, int *eof, void *data)
{
	int len = MIN(count, buffer_size - off);

	if (buffer == NULL) {
		buffer_size = build_buffer();
		len = MIN(count, buffer_size);
		*start = 0;
	}

	memcpy(page, buffer + off, len);

	if (len == 0) {
		*eof = 1;
		kfree(buffer);
		buffer = NULL;
		return 0;
	}

	return len;
}

int init_dump_module(void)
{
	int ret = 0;

	buffer = NULL;

	proc_entry = create_proc_entry(PROC_FILENAME, 0644, NULL);
	if (proc_entry == NULL) {
		ret = -ENOMEM;
		logk("Couldn't create proc entry\n");
	} else {
		proc_entry->read_proc = dump_read;
		logk("Module loaded.\n");
	}

	return ret;
}

void cleanup_dump_module(void)
{
	if (buffer != NULL) {
		kfree(buffer);
	}

	remove_proc_entry(PROC_FILENAME, NULL);
	logk("Module unloaded.\n");
}

module_init(init_dump_module);
module_exit(cleanup_dump_module);
