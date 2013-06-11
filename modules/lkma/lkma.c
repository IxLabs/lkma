#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/sort.h>
#include <linux/bsearch.h>
#include <linux/uaccess.h>
#include <linux/rculist.h>

#include <linux/fs.h>		/* for basic filesystem */
#include <linux/proc_fs.h>	/* for the proc filesystem */
#include <linux/seq_file.h>	/* for sequence files */

MODULE_DESCRIPTION("Linux Kernel Memory Analyzer");
MODULE_AUTHOR("Ghennadi Procopciuc");
MODULE_LICENSE("GPL");

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
     printk(KERN_ALERT "[File %s, Line %d, Function %s] ERROR : " format "\n",\
            __FILE__, __LINE__, __func__, ##__VA_ARGS__);\
     })

#define PROC_FILENAME       "lkma"
#define DEFAULT_DB_SIZE     2000
#define DEFAULT_STACK_SIZE  40

struct stack {
	u8 **data;
	int size;
	int capacity;
};

typedef struct stack buffer_t;

static struct proc_dir_entry *lkma_entry;

/* Buffer for output dump */
static char *buffer;

/* Buffer size */
static int buffer_size;

/* Buffer capacity */
static int buffer_capacity;

/* User defined filter */
static char *filter;

/* Mutex which protect modules list */
extern struct mutex module_mutex;
extern struct list_head modules;

/* Maximum node offset from kallsyms_trie */
static unsigned long kallsyms_trie_size;

/* Exported data from kallsysms */
extern u8 kallsyms_trie[];
extern const unsigned long kallsyms_offsets[];
extern const unsigned long kallsyms_num_syms;

/* db stores all files defined in kallsyms_trie sorted with db_cmp */
buffer_t db = { .size = 0, .capacity = 0, .data = NULL };

/**
 * realloc - reallocate memory allocated with kmalloc and friends
 * @old_ptr:   Old allocated address
 * @old_size:  Old size
 * @new_size:  New size
 * @mode:      Memory flags
 */
void *realloc(void *old_ptr, size_t old_size,
	      size_t new_size, unsigned int mode)
{
	void *buf;

	buf = kmalloc(new_size, mode);
	if (buf) {
		memcpy(buf, old_ptr,
		       ((old_size < new_size) ? old_size : new_size));
		kfree(old_ptr);
	}

	return buf;
}

/**
 * stack_init - Initialize a stack
 * @st:       Stack address
 * @capacity: Initial stack capacity
 */
static int stack_init(struct stack *st, int capacity)
{
	klog("Stack init %p with capacity : %d", st, capacity);
	st->size = 0;
	st->capacity = capacity;
	st->data = kmalloc(sizeof(*st->data) * capacity, GFP_KERNEL);

	if (!st->data) {
		kerr("Unable to allocate memory with kmalloc");
		return -ENOMEM;
	}

	return 0;
}

/**
 * stack_push - Add an element to stack
 * @st:      Stack address
 * @element: Element to push
 */
static int stack_push(struct stack *st, u8 * element)
{
	int ret;

	if (st->data == NULL) {
		ret = stack_init(st, DEFAULT_STACK_SIZE);
		if (ret) {
			kerr("Stack initialization failed");
			return ret;
		}
	}

	if (st->size == st->capacity) {
		klog("Realloc to %d", st->capacity * 2);
		st->data = realloc(st->data, st->capacity * sizeof(*st->data),
				   st->capacity * sizeof(*st->data) * 2,
				   GFP_KERNEL);
		if (!st->data) {
			return -ENOMEM;
		}
		st->capacity *= 2;
	}

	st->data[st->size] = element;
	st->size++;

	return 0;
}

/**
 * stack_empty - Test if stack is empty
 * @st:  Stack address
 *
 * Return :
 * true - Stack is empty
 * false - Stack has a few elements
 */
static bool stack_empty(struct stack *st)
{
	return st->size == 0;
}

/**
 * stack_pop - Remove the value from the top of the stack
 * @st: Stack address
 *
 * Return: Value from the top of the stack
 */
static u8 *stack_pop(struct stack *st)
{
	if (st->size == 0) {
		return NULL;
	}

	return st->data[--st->size];
}

/**
 * stack_destroy - Remove all elements from stack
 * @st: Stack address
 */
static void stack_destroy(struct stack *st)
{
	st->capacity = 0;
	st->size = 0;
	kfree(st->data);
}

/**
 * get_node_filename - Get filename field from a trie node
 * @node: Node address in trie serialization
 */
static u8 *get_node_filename(u8 * node)
{
	return node + sizeof(unsigned long);
}

/**
 * get_node_parent - Get the parent offset from a trie node
 * @node: Node address in trie serialization
 */
static unsigned long get_node_parent(u8 * node)
{
	u8 *tmp_node = node;

	/* Size */
	tmp_node += sizeof(unsigned long);

	/* Skip filename and parent */
	return *(unsigned long *)(tmp_node + strlen(tmp_node) + 1);
}

/**
 * get_node_children - Get the children array from a trie node
 * @node: Node address in trie serialization
 *
 * From returned address you can easily obtain the number of children by
 * decreasing the address with sizeof(unsigned long)
 * @see kallsyms_trie [Serialization]
 */
static unsigned long *get_node_children(u8 * node)
{
	u8 *tmp_node = node;

	/* Size */
	tmp_node += sizeof(unsigned long);

	/* Skip filename and parent */
	tmp_node += strlen(tmp_node) + 1 + sizeof(unsigned long);

	/* Skip children_num */
	tmp_node += sizeof(unsigned long);

	return (unsigned long *)tmp_node;
}

/**
 * get_node_size - Get the allocated size from a trie node
 * @node: Node address in trie serialization
 */
static unsigned long get_node_size(u8 * node)
{
	unsigned long *children = get_node_children(node);

	return (char *)children + (*(children - 1) * sizeof(unsigned long)) -
	    (char *)node;
}

/**
 * update_kallsyms_trie_size - Get the trie size in kallsyms_trie_size
 *                             variable [global]
 */
static void update_kallsyms_trie_size(void)
{
	unsigned long i;

	/* Iterate over all symbols to find the offset of the latest node */
	for (i = 0; i < kallsyms_num_syms; i++) {
		if (kallsyms_trie_size < kallsyms_offsets[i]) {
			kallsyms_trie_size = kallsyms_offsets[i];
		}
	}

	kallsyms_trie_size += get_node_size(kallsyms_trie + kallsyms_trie_size);
}

/**
 * db_cmp - Database comparator.
 * @a: A node from db
 * @b: A node from db
 *
 * It aids to sort db alphabetically.
 */
static int db_cmp(const void *a, const void *b)
{
	return strcmp(*(char **)a, *(char **)b);
}

/**
 * db_add_file - Add file to the db
 * @filename: File to add
 */
static int inline db_add_file(u8 * filename)
{
	return stack_push(&db, filename);
}

/**
 * build_db - Build db from trie content
 */
static int build_db(void)
{
	unsigned long offset;
	int ret;

	update_kallsyms_trie_size();

	/* Add all files name from kallsyms_trie to db */
	for (offset = 0; offset < kallsyms_trie_size;) {
		ret = db_add_file(get_node_filename(kallsyms_trie + offset));

		if (ret) {
			kerr("Failed to add file %s to our database",
			     kallsyms_trie + offset);
			return ret;
		}

		offset += get_node_size(kallsyms_trie + offset);
	}

	/* Sort files by name */
	sort(db.data, db.size, sizeof(*db.data), db_cmp, NULL);

#ifdef DEBUG
	for (offset = 0; offset < db.size; offset++) {
		klog("File %s parent = %lu", db.data[offset],
		     get_node_parent(db.data[offset] - sizeof(unsigned long)));
	}
#endif

	return 0;
}

#ifdef DEBUG
static void print_node(u8 * node)
{
	unsigned long children_num;
	unsigned long i;

	if (!node) {
		kerr("Empty node !!!");
		return;
	}

	klog("\tSize = %lu", *(unsigned long *)node);

	/* Filename */
	node += sizeof(unsigned long);
	klog("\tFilename = %s", node);

	/* Parent */
	node += strlen(node) + 1;
	klog("\tParent = %lu", *(unsigned long *)node);

	/* Children */
	node += sizeof(unsigned long);
	children_num = *(unsigned long *)node;
	klog("\tChildren num = %lu", children_num);

	for (i = 0; i < children_num; i++) {
		node += sizeof(unsigned long);
		klog("\t\tchildren[%lu] = %lu", i, *(unsigned long *)node);
	}
}
#endif

/**
 * get_node_value - Get allocated size from a node
 * @node: Node address in trie serialization
 */
static unsigned long get_node_value(u8 * node)
{
	struct stack st;
	unsigned long children_num;
	unsigned long sum = 0;
	unsigned long i;
	unsigned long *children;
	int ret;

	if (!node) {
		kerr("Given NULL pointer !!!");
		return 0;
	}

	stack_init(&st, DEFAULT_STACK_SIZE);

#ifdef DEBUG
	klog("First node :");
	print_node(node);
#endif
	ret = stack_push(&st, node);
	if (ret) {
		kerr("Failed to push node to stack");
		goto out;
	}

	while (!stack_empty(&st)) {
		node = stack_pop(&st);

		sum += atomic_read((atomic_t *) node);

#ifdef DEBUG
		klog("From stack :");
		print_node(node);
#endif

		children = get_node_children(node);
		children_num = *((unsigned long *)children - 1);

		klog("Children num : %lu", children_num);
		for (i = 0; i < children_num; i++) {
			ret = stack_push(&st, kallsyms_trie + children[i]);

			if (ret) {
				kerr("Failed to push node to stack");
				goto out;
			}
		}
	}

 out:
	stack_destroy(&st);

	return sum;
}

/**
 * get_node_path - Build a node path from the root node in the given stack.
 * @st: Stack
 * @node: Node address in trie serialization
 */
void get_node_path(struct stack *st, u8 * node)
{
	int ret;
	if (!node) {
		kerr("Given NULL pointer !!!");
		return;
	}

	while (strlen(get_node_filename(node)) != 0) {
		klog("Add to stack : %s", get_node_filename(node));

		ret = stack_push(st, get_node_filename(node));
		if (ret) {
			kerr("Failed to push node to stack");
			return;
		}

		node = kallsyms_trie + get_node_parent(node);
	}
}

/**
 * prepare_append_string - Reallocate memory if necessary for a string append
 * @buffer:             Address of the destination buffer
 * @buffer_size:        Destination buffer size
 * @buffer_capacity:    Destination buffer capacity
 * @length:             Length of the string to be appended
 */
static int prepare_append_string(char **buffer, int buffer_size,
				 int buffer_capacity, int length)
{
	if (buffer == NULL) {
		kerr("Given NULL pointer !");
		return -1;
	}
	if (buffer_size + length >= buffer_capacity) {
		*buffer = realloc(*buffer, buffer_capacity,
				  buffer_capacity * 2, GFP_KERNEL);
		if (*buffer == NULL) {
			kerr("Failed to realloc memory for buffer %p", *buffer);
			return -ENOMEM;
		}

		return buffer_capacity * 2;
	}

	return buffer_capacity;
}

/**
 * dump_node_stats - Print node path and amount of memory allocated by all
 *                   children
 * @node: Node address in trie serialization
 * @total: True if you want to summarize memory allocated by children or false
 *         for a dry print
 */
static void dump_node_stats(u8 * node, bool total)
{
	unsigned long mem_amount;
	struct stack st;
	u8 *temp;

	if (total) {
		mem_amount = get_node_value(node);
	} else {
		mem_amount = *(unsigned long *)node;
	}

	if (*get_node_filename(node) == '\0') {
		return;
	}

	buffer_capacity = prepare_append_string(&buffer, buffer_size,
						buffer_capacity, 12);
	sprintf(buffer + buffer_size, "%10lu\t", mem_amount);
	buffer_size += 11;

	stack_init(&st, DEFAULT_STACK_SIZE);
	get_node_path(&st, node);

	while (!stack_empty(&st)) {
		temp = stack_pop(&st);
		buffer_capacity = prepare_append_string(&buffer, buffer_size,
							buffer_capacity,
							strlen(temp) + 2);
		sprintf(buffer + buffer_size, "/%s", temp);
		buffer_size += strlen(temp) + 1;
	}
	stack_destroy(&st);

	buffer_capacity = prepare_append_string(&buffer, buffer_size,
						buffer_capacity, 2);
	sprintf(buffer + buffer_size, "\n");
	buffer_size += 1;
}

/**
 * dump_module - Print module name and amount of memory allocated by it.
 * @mod: Module
 */
static void dump_module(struct module *mod)
{
	buffer_capacity = prepare_append_string(&buffer, buffer_size,
						buffer_capacity, 12);
	sprintf(buffer + buffer_size, "%10ld\t", mod->allocated_size);
	buffer_size += 11;

	buffer_capacity = prepare_append_string(&buffer, buffer_size,
						buffer_capacity,
						strlen(mod->name) + 11);
	sprintf(buffer + buffer_size, "%s\t[module]\n", mod->name);
	buffer_size += strlen(mod->name) + 10;
}

/**
 * dump_all - Print all available statistics on dynamic memory allocation
 */
static void dump_all(void)
{
	struct module *mod;
	int i;

	for (i = 0; i < db.size; i++) {
		dump_node_stats(db.data[i] - sizeof(unsigned long), false);
	}

	mutex_lock(&module_mutex);

	list_for_each_entry(mod, &modules, list) {
		dump_module(mod);
		klog("Name %s size = %ld", mod->name, mod->allocated_size);
	}
	mutex_unlock(&module_mutex);
}

/**
 * apply_filter - Dump all whose names match with given filename
 * @filename: Filter filename
 */
static void apply_filter(char *filename)
{
	u8 **key;
	int i, index;
	struct module *mod;

	/* Search filename over modules ... */
	mutex_lock(&module_mutex);

	list_for_each_entry(mod, &modules, list) {
		if (strcmp(mod->name, filename) == 0) {
			dump_module(mod);
		}
		klog("Name %s size = %ld\n", mod->name, mod->allocated_size);
	}
	mutex_unlock(&module_mutex);

	/* Kernel search */
	key = bsearch(&filename, db.data, db.size, sizeof(*db.data), db_cmp);

	if (!key) {
		klog("Could not found element '%s' through kernel files\n",
		     filename);
		return;
	}

	index = key - db.data;
	klog("File %s parent = %lu index = %d", *key,
	     get_node_parent(*key - sizeof(unsigned long)), key - db.data);

	dump_node_stats(*key - sizeof(unsigned long), true);

	for (i = index - 1; i >= 0; i--) {
		if (strcmp(db.data[i], filename) == 0) {
			klog("Left %s parent = %lu index = %d", db.data[i],
			     get_node_parent(db.data[i] -
					     sizeof(unsigned long)), i);

			dump_node_stats(db.data[i] - sizeof(unsigned long),
					true);
		} else {
			break;
		}
	}

	for (i = index + 1; i < db.size; i++) {
		if (strcmp(db.data[i], filename) == 0) {
			klog("Right %s parent = %lu index = %d", db.data[i],
			     get_node_parent(db.data[i] -
					     sizeof(unsigned long)), i);
			dump_node_stats(db.data[i] - sizeof(unsigned long),
					true);
		} else {
			break;
		}
	}
}

/**
 * set_filter - Read procesure for proc_entry.
 * @file:    File handle
 * @buffer:  Source buffer
 * @count:   Buffer size
 * @data:
 *
 * This is the single way to set filter from userspace
 */
static int set_filter(struct file *file, const char __user * buffer,
		      unsigned long count, void *data)
{

	if (filter != NULL) {
		kfree(filter);
		filter = NULL;
	}

	filter = kmalloc(count, GFP_KERNEL);
	if (!filter) {
		kerr("Unable to alloc memory with kmalloc");
		return -ENOMEM;
	}

	if (copy_from_user(filter, buffer, count)) {
		kerr("copy_from_)user failed");
		return -EFAULT;
	}

	/* Remove the '\n' */
	if (filter[count - 1] == '\n') {
		filter[count - 1] = '\0';
	}

	if (strcmp(filter, "ALL") == 0) {
		klog("Print all ... ");
		kfree(filter);
		filter = NULL;
	}

	klog("Count = %ld Filter : --%s--", count, filter);

	return count;
}

static int dump_read(char *page, char **start, off_t off,
		     int count, int *eof, void *data)
{
	int len = min(count, (int)(buffer_size - off));

	if (buffer == NULL && off == 0) {
		buffer = kmalloc(DEFAULT_DB_SIZE, GFP_KERNEL);
		buffer_capacity = DEFAULT_DB_SIZE;
		buffer_size = 0;

		if (filter != NULL) {
			apply_filter(filter);
		} else {
			dump_all();
		}
		len = min(count, buffer_size);
	}
	*start = page;

	if (off < buffer_size) {
		memcpy(page, buffer + off, len);
	}

	if (len == 0) {
		*eof = 1;
		if (buffer) {
			kfree(buffer);
			buffer = NULL;
		}
		return 0;
	}

	return len;
}

static int lkma_show(struct seq_file *m, void *v)
{
	buffer = kmalloc(DEFAULT_DB_SIZE, GFP_KERNEL);
	buffer_capacity = DEFAULT_DB_SIZE;
	buffer_size = 0;

	if (filter != NULL) {
		apply_filter(filter);
	} else {
		dump_all();
	}

	seq_printf(m, "%s", buffer);

	if (buffer) {
		kfree(buffer);
		buffer = NULL;
	}

	return 0;
}

static int lkma_open(struct inode *inode, struct file *file)
{
	return single_open(file, lkma_show, NULL);
}

static const struct file_operations lkma_fops = {
	.owner = THIS_MODULE,
	.open = lkma_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = set_filter,
};

static int lkma_init(void)
{

	filter = NULL;

	lkma_entry = proc_create(PROC_FILENAME, 0, NULL, &lkma_fops);

	if (lkma_entry == NULL) {
		klog("Couldn't create proc entry");
		return -ENOMEM;
	}

	build_db();
	klog("Module loaded");
	return 0;
}

static void lkma_exit(void)
{
	kfree(db.data);

	if (filter != NULL) {
		kfree(filter);
	}

	if (buffer != NULL) {
		kfree(buffer);
	}

	remove_proc_entry(PROC_FILENAME, NULL);

	klog("Module unloaded");
}

module_init(lkma_init);
module_exit(lkma_exit);
