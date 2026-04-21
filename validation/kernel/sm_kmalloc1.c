#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/sprintf.h>

#include "../../check_debug.h"

int frob(struct file *file, const char __user *buf,	size_t len, loff_t *ppos);

int frob(struct file *file, const char __user *buf,	size_t len, loff_t *ppos)
{
	int *p;

	p = kmalloc(sizeof(int) * 10, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	__smatch_buf_size(p);
	kfree(p);

	return 0;
}

/*
 * check-name: smatch: kmalloc #1
 * check-command: validation/kernel/build.sh sm_kmalloc1.c
 *
 * check-output-start
sm_kmalloc1.c:20 frob() buf size: 'p' 10 elements, 40 bytes
 * check-output-end
 */
