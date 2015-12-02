/*
 * Flash cache solution iostash
 *
 * Authors: Ioannis Koltsidas <iko@zurich.ibm.com>
 *          Nikolas Ioannou   <nio@zurich.ibm.com>
 *
 * Copyright (c) 2014-2015, IBM Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */


#include "iostash.h"
#include "helpers.h"

#include <linux/hash.h>
#include <linux/fs.h>
#include <linux/rculist.h>

/* assumes it's called from within an rcu read-side critical section,
 * or ctl_mtx is held while doing a search in an update operations */
static struct ssd_info *_search_ssd(const dev_t dev_t)
{
	struct ssd_info *p;
	const uint32_t hv = hash_32(dev_t, 32) % IOSTASH_MAXSSD_BCKTS;
	struct list_head *const b = &gctx.ssdtbl.bucket[hv];

	if (unlikely(0 == dev_t))
		return NULL;

	list_for_each_entry_rcu(p, b, list) {
		if (dev_t == p->dev_t)
			return p;
	}

	return NULL;
}

/* assumes ctl_mtx is held */
static void _insert_ssd(struct ssd_info *const ssd)
{
	BUG_ON(NULL == ssd || 0 == ssd->dev_t);
	{
		const uint32_t hv = hash_32(ssd->dev_t, 32) % IOSTASH_MAXSSD_BCKTS;
		struct list_head *const b = &gctx.ssdtbl.bucket[hv];
		list_add_rcu(&ssd->list, b);
		/* beyond this point ssd might be referenced */
		DBG("inserted ssd struct ptr=%p into bucket %u.\n", ssd, hv);
	}
}

static struct ssd_info *_alloc_ssd(char *path)
{
	struct ssd_info *ssd;
	struct block_device *const bdev = lookup_bdev(path);
	dev_t dev_t = 0;
	if (IS_ERR(bdev))
		return NULL;
	DBG("bdev %p found for path %s.\n", bdev, path);
	dev_t = bdev->bd_dev;
	bdput(bdev);

	ssd = _search_ssd(dev_t);
	if (NULL != ssd)
		return NULL;	/* EEXIST */

	ssd = kzalloc(sizeof(*ssd), GFP_NOWAIT);
	if (unlikely(NULL == ssd))
		return NULL;	/* ENOMEM */

	strcpy(ssd->path, path);
	ssd->dev_t = dev_t;
	atomic_set(&ssd->nr_ref, 0);
	INIT_LIST_HEAD(&ssd->list);

	DBG("Created ssd struct ptr=%p.\n", ssd);

	return ssd;
}

/* assumes ctl_mtx is held */
static void _unload_ssd(struct ssd_info * ssd)
{
	if (NULL == ssd)
		return;

	/* make offline */
	if (ssd->online) {
		ssd->online = 0;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,1,0)
		/* we delete in place in older kernels */
		list_del_rcu(&ssd->list);
#endif
		wmb();
		synchronize_rcu(); /* wait for references to quiesce */
		while(atomic_read(&ssd->nr_ref))
			schedule();
	}

	if (ssd->cdev) {
		sce_rmcdev(ssd->cdev);
		ssd->cdev = NULL;
	}

	if (ssd->bdev) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
		blkdev_put(ssd->bdev,
			   FMODE_READ | FMODE_WRITE);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
		close_bdev_exclusive(ssd->bdev,
				     FMODE_READ | FMODE_WRITE);
#else
		printk("Kernel version < 2.6.28 currently not supported.\n");
#endif
		ssd->bdev = NULL;
	}
}

/* assumes ctl_mtx is held */
static void _destroy_ssd(struct ssd_info *const ssd)
{
	if (NULL == ssd)
		return;

	_unload_ssd(ssd);
	kfree(ssd);
}

/* KObject section starts here */
struct ssd_attribute
{
        struct attribute attr;
        ssize_t (*show) (struct ssd_info *ssd, char *buffer );
        ssize_t (*store) (struct ssd_info *ssd, const char *buffer, size_t size );
};

static ssize_t name_show(struct ssd_info *ssd, char *buffer)
{
	int len = 0;
	sprintf(buffer, "%s\n", ssd->path);
	len = strlen(buffer);
	BUG_ON(len > PAGE_SIZE);
	return len;
}

static struct ssd_attribute ssd_attr_name = __ATTR_RO(name);

static struct attribute * ssd_attrs[] = {
	&ssd_attr_name.attr,
        NULL,
};

static ssize_t ssd_kobj_show(struct kobject *kobj, struct attribute *attr, char *buffer)
{
        struct ssd_attribute *sa = container_of(attr, struct ssd_attribute, attr);
        return sa->show ? sa->show(container_of(kobj, struct ssd_info, kobj), buffer) : 0;
};

static ssize_t ssd_kobj_store(struct kobject *kobj, struct attribute *attr, const char *buffer, size_t size)
{
	return size;
}

static const struct sysfs_ops ssd_sysfs_ops = {
        .show  = ssd_kobj_show,
        .store = ssd_kobj_store
};

static void ssd_kobj_release( struct kobject *kobj )
{
        printk("released: %s\n", kobject_name(kobj));
}

int _ssd_create_kobj(struct ssd_info *ssd)
{
	int err = 0;

	static struct kobj_type ssd_kobj_type = {
                .release       = ssd_kobj_release,
                .sysfs_ops     = &ssd_sysfs_ops,
                .default_attrs = ssd_attrs,
        };

	memset(&ssd->kobj, 0, sizeof(ssd->kobj));
	err = kobject_init_and_add(
		&ssd->kobj,
		&ssd_kobj_type,
		&gctx.ssd_kset->kobj,
		"%s-%u.%u",
		_basename(ssd->path),
		MAJOR(ssd->bdev->bd_dev),
		MINOR(ssd->bdev->bd_dev));

	return err;
}

/* KObject section ends here */

int ssd_register(char *path)
{
	struct ssd_info *ssd;
	int ret = -1;

	mutex_lock(&gctx.ctl_mtx);
	do {
		ssd = _alloc_ssd(path);
		if (!ssd) {
			printk("iostash: No more SSD\n");
			break;
		}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
		ssd->bdev = blkdev_get_by_path(path, FMODE_READ | FMODE_WRITE,
				       IOSTASH_NAME);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
		ssd->bdev = open_bdev_exclusive(path, FMODE_READ | FMODE_WRITE,
				       IOSTASH_NAME);
#else
		printk("Kernel version < 2.6.28 currently not supported.\n");
		ssd->bdev = ERR_PTR(-ENOENT);
#endif
		if (IS_ERR(ssd->bdev)) {
			printk("iostash: device lookup failed\n");
			ssd->bdev = NULL;
			break;
		}

		ssd->nr_sctr =
		    to_sector(ssd->bdev->bd_inode->i_size - IOSTASH_HEADERSIZE);
		printk("iostash: ssd->nr_sctr = %ld\n", (long)ssd->nr_sctr);

		ssd->cdev = sce_addcdev(gctx.sce, ssd->nr_sctr, ssd);
		if (ssd->cdev == NULL) {
			printk("iostash: sce_add_device() failed \n");
			break;
		}

		ret = _ssd_create_kobj(ssd);
		if (ret) {
			printk("ssd_create_kobj failed with %d\n", ret);
			break;
		}

		/* insert it to our ssd hash table, it is ready to service requests */
		_insert_ssd(ssd);

		ssd->online = 1;
		gctx.nr_ssd++;
		printk("iostash: SSD %s has been added successfully\n", path);

		ret = 0;
	} while (0);

	if (ret)
		_destroy_ssd(ssd);

	mutex_unlock(&gctx.ctl_mtx);

	return ret;
}

/* assumes ctl_mtx is held */
void _ssd_remove(struct ssd_info *ssd)
{
	_unload_ssd(ssd);
	kobject_put(&ssd->kobj);
	gctx.nr_ssd--;
	printk("iostash: %s has been removed successfully\n", ssd->path);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,1,0)
	kfree(ssd);
#endif
}

void ssd_unregister(char *path)
{
	struct block_device *const bdev = lookup_bdev(path);
	printk("bdev %p found for path %s.\n", bdev, path);
	if (IS_ERR(bdev))
		return;
	bdput(bdev);
	mutex_lock(&gctx.ctl_mtx);
	_ssd_remove(_search_ssd(bdev->bd_dev));
	mutex_unlock(&gctx.ctl_mtx);
}

void ssd_unregister_all(void)
{
	int i;
	struct ssd_info *ssd, *q;
#if KERNEL_VERSION(3,2,0) <= LINUX_VERSION_CODE
	LIST_HEAD(l);
#endif

	mutex_lock(&gctx.ctl_mtx);
	for (i = 0; i < IOSTASH_MAXSSD_BCKTS; ++i)
	{
		struct list_head *const b = &gctx.ssdtbl.bucket[i];
		list_for_each_entry_safe(ssd, q, b, list) {
			_ssd_remove(ssd);
#if KERNEL_VERSION(3,2,0) <= LINUX_VERSION_CODE
			list_del_rcu(&ssd->list);
			synchronize_rcu();
			list_add_tail(&ssd->list, &l);
#endif
		}
	}
	printk("iostash: wait all SSDs are deleted\n");

#if KERNEL_VERSION(3,2,0) <= LINUX_VERSION_CODE
	rcu_barrier();
	list_for_each_entry_safe(ssd, q, &l, list) {
		list_del(&ssd->list);
		kfree(ssd);
	}
#endif
	mutex_unlock(&gctx.ctl_mtx);
}
