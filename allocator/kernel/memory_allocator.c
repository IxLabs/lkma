/**
 * This module is intended for testing purpose, it allocates kernel memory
 * on demand, using IOCTL calls (ALLOCATOR_IOCTL_ALLOC).
 * @author Ghennadi Procopciuc
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/slab.h>

#include "../include/allocator.h"

MODULE_DESCRIPTION("Character device for memory allocation");
MODULE_AUTHOR("Ghennadi Procopciuc");
MODULE_LICENSE("GPL");

struct allocator_device_data {
	struct cdev cdev;
};

static int do_open(struct inode *inode, struct file *file);
static int do_release(struct inode *inode, struct file *file);
static long do_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

struct allocator_device_data device;

const struct file_operations do_fops = {
	.owner = THIS_MODULE,
	.open = do_open,
	.release = do_release,
	.unlocked_ioctl = do_ioctl
};

static int allocator_init(void)
{
	int err;
	err = register_chrdev_region(MKDEV(ALLOCATOR_MAJOR, ALLOCATOR_MINOR),
				     NUM_MINORS, "memory_allocation_driver");

	if (err != 0) {
		logk(KERN_DEBUG
		       "Error at : register_chrdev_region ERROR = %d\n", err);
		return err;
	}

	logk(KERN_DEBUG "Module registered\n");

	cdev_init(&device.cdev, &do_fops);
	cdev_add(&device.cdev, MKDEV(ALLOCATOR_MAJOR, 0), 1);

	return 0;
}

static int do_open(struct inode *inode, struct file *file)
{
	logk(LOG_LEVEL "Open ...\n");

	return 0;
}

static int do_release(struct inode *inode, struct file *file)
{
	logk(LOG_LEVEL "Release ...\n");

	return 0;
}

static long do_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case ALLOCATOR_IOCTL_ALLOC:
		logk(LOG_LEVEL "Allocates %lu bytes\n", arg);

		/* Allocates memory on demand */
		if (kmalloc(arg, GFP_KERNEL) == NULL) {
			logk(LOG_LEVEL "Out of memory\n");
			return -ENOMEM;
		}
		break;
	default:
		return -ENOTTY;
	}

	return 0;
}

static void allocator_exit(void)
{
	logk(LOG_LEVEL "Deleting device ...");
	cdev_del(&device.cdev);

	unregister_chrdev_region(MKDEV(ALLOCATOR_MAJOR, ALLOCATOR_MINOR),
				 NUM_MINORS);
	logk(KERN_DEBUG "Module unregistered\n");
}

module_init(allocator_init);
module_exit(allocator_exit);
