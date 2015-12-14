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
static struct hdd_info *_search_hdd(const dev_t dev_t)
{
	struct hdd_info *p;
	const uint32_t hv = hash_32(dev_t, 32) % IOSTASH_MAXHDD_BCKTS;
	struct list_head *const b = &gctx.hddtbl.bucket[hv];

	if (unlikely(0 == dev_t))
		return NULL;

	list_for_each_entry_rcu(p, b, list) {
		if (dev_t == p->dev_t)
			return p;
	}

	return NULL;
}

/* assumes ctl_mtx is held */
static void _insert_hdd(struct hdd_info *const hdd)
{
	BUG_ON(NULL == hdd || 0 == hdd->dev_t);
	{
		const uint32_t hv = hash_32(hdd->dev_t, 32) % IOSTASH_MAXHDD_BCKTS;
		struct list_head *const b = &gctx.hddtbl.bucket[hv];
		list_add_rcu(&hdd->list, b);
		/* beyond this point hdd might be referenced */
		DBG("inserted hdd struct ptr=%p into bucket %u.\n", hdd, hv);
	}
}

/* assumes ctl_mtx is held */
static struct hdd_info *_alloc_hdd(char *path)
{
	struct hdd_info *hdd;
	struct block_device *const bdev = lookup_bdev(path);
	dev_t dev_t = 0;
	if (IS_ERR(bdev))
		return NULL;
	DBG("bdev %p found for path %s.\n", bdev, path);
	dev_t = bdev->bd_dev;
	bdput(bdev);

	hdd = _search_hdd(dev_t);
	if (NULL != hdd)
		return NULL;	/* EEXIST */

	hdd = kzalloc(sizeof(*hdd), GFP_NOWAIT);
	if (unlikely(NULL == hdd))
		return NULL;	/* ENOMEM */

	strcpy(hdd->path, path);
	hdd->dev_t = dev_t;
	atomic_set(&hdd->io_pending, 0);
	atomic_set(&hdd->nr_ref, 0);
	INIT_LIST_HEAD(&hdd->list);

	DBG("Created hdd struct ptr=%p.\n", hdd);

	return hdd;
}

/* assumes ctl_mtx is held */
static void _unload_hdd(struct hdd_info * hdd)
{
	bool exists;
	if (NULL == hdd)
		return;

	/* first swap back original request fn */
	if (hdd->org_mapreq && hdd->bdev) {
		hdd->bdev->bd_disk->queue->make_request_fn = hdd->org_mapreq;
		wmb();
	}

	exists = hdd == _search_hdd(hdd->dev_t);
	if (exists && hdd->online) {
		hdd->online = 0; /* disable further refs */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,1,0)
		/* we delete in place in older kernels */
		list_del_rcu(&hdd->list);
#endif
		synchronize_rcu(); /* wait for references to quiesce */
		while(atomic_read(&hdd->io_pending))
			schedule();
		while(atomic_read(&hdd->nr_ref))
			schedule();

		gctx.nr_hdd--;
	}

	if (hdd->io_queue)
		destroy_workqueue(hdd->io_queue);
	hdd->io_queue = NULL;

	if (hdd->io_pool)
		mempool_destroy(hdd->io_pool);
	hdd->io_pool = NULL;

	if (hdd->bs)
		bioset_free(hdd->bs);
	hdd->bs = NULL;


	if (hdd->bdev) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
		blkdev_put(hdd->bdev, FMODE_READ | FMODE_WRITE);
#else
		printk("Kernel version < 2.6.28 currently not supported.\n");
#endif
	}
	hdd->bdev = NULL;

	if (hdd->lun)
		sce_rmlun(hdd->lun);
	hdd->lun = NULL;
}

/* assumes ctl_mtx is held */
static void _destroy_hdd(struct hdd_info * hdd)
{
	if(NULL == hdd)
		return;
	_unload_hdd(hdd);
	kfree(hdd);
}

/* KObject section starts here */

struct hdd_attribute
{
        struct attribute attr;
        ssize_t (*show) (struct hdd_info *hdd, char *buffer );
        ssize_t (*store) (struct hdd_info *hdd, const char *buffer, size_t size );
};

static ssize_t name_show(struct hdd_info *hdd, char *buffer)
{
	int len = 0;
	sprintf(buffer, "%s\n", hdd->path);
	len = strlen(buffer);
	BUG_ON(len > PAGE_SIZE);
	return len;
}

static struct hdd_attribute hdd_attr_name = __ATTR_RO(name);

static struct attribute * hdd_attrs[] = {
	&hdd_attr_name.attr,
        NULL,
};

static ssize_t hdd_kobj_show(struct kobject *kobj, struct attribute *attr, char *buffer)
{
        struct hdd_attribute *sa = container_of(attr, struct hdd_attribute, attr);
        return sa->show ? sa->show(container_of(kobj, struct hdd_info, kobj), buffer) : 0;
};

static ssize_t hdd_kobj_store(struct kobject *kobj, struct attribute *attr, const char *buffer, size_t size)
{
	return size;
}

static const struct sysfs_ops hdd_sysfs_ops = {
        .show  = hdd_kobj_show,
        .store = hdd_kobj_store
};

static void hdd_kobj_release( struct kobject *kobj )
{
        printk("released: %s\n", kobject_name(kobj));
}

int _hdd_create_kobj(struct hdd_info *hdd)
{
	int err = 0;

	static struct kobj_type hdd_kobj_type = {
                .release       = hdd_kobj_release,
                .sysfs_ops     = &hdd_sysfs_ops,
                .default_attrs = hdd_attrs,
        };

	memset(&hdd->kobj, 0, sizeof(hdd->kobj));
	err = kobject_init_and_add(
		&hdd->kobj,
		&hdd_kobj_type,
		&gctx.hdd_kset->kobj,
		"%s-%u.%u",
		_basename(hdd->path),
		MAJOR(hdd->bdev->bd_dev),
		MINOR(hdd->bdev->bd_dev));

	return err;
}

/* KObject section ends here */


int hdd_register(char *path)
{
	struct hdd_info *hdd;
	int ret = -1;
	mutex_lock(&gctx.ctl_mtx);
	do {
		hdd = _alloc_hdd(path);
		if (!hdd) {
			printk("iostash: No more HDD\n");
			break;
		}
		printk("Trying to get %s\n", path);

		/* cache from the whole device, the make_request_fn
		 * pointer we are hijacking is for the whole device,
		 * not just the partition */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)
		hdd->bdev = blkdev_get_by_path(path, FMODE_READ | FMODE_WRITE,
				       &gctx.hddtbl);
		if (!IS_ERR(hdd->bdev) && hdd->bdev->bd_contains && hdd->bdev->bd_contains != hdd->bdev) {
			const dev_t dev_t = hdd->bdev->bd_contains->bd_dev;
			struct block_device *const whole =  blkdev_get_by_dev(dev_t, FMODE_READ | FMODE_WRITE,
				       &gctx.hddtbl);
			blkdev_put(hdd->bdev, FMODE_READ | FMODE_WRITE);
			hdd->bdev = whole;
		}
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
		hdd->bdev = lookup_bdev(path);
		if (!IS_ERR(hdd->bdev)) {
			const dev_t dev = hdd->bdev->bd_dev;
			hdd->bdev = open_by_devnum(dev, FMODE_READ | FMODE_WRITE);
			bdput(hdd->bdev);
			if (IS_ERR(hdd->bdev)) {
				printk("iostash: device open failed\n");
				hdd->bdev = NULL;
				break;
			}
			if (hdd->bdev->bd_contains != hdd->bdev) {
				const dev_t whole_dev = hdd->bdev->bd_contains->bd_dev;
				struct block_device *const whole =  open_by_devnum(whole_dev, FMODE_READ | FMODE_WRITE);
				blkdev_put(hdd->bdev, FMODE_READ | FMODE_WRITE);
				hdd->bdev = whole;
			}
		}
#else
		printk("Kernel version < 2.6.28 currently not supported.\n");
		hdd->bdev = ERR_PTR(-ENOENT);
#endif
		if (IS_ERR(hdd->bdev)) {
			printk("iostash: device lookup failed\n");
			hdd->bdev = NULL;
			break;
		}

		rmb();
		if (hdd->bdev->bd_holder) {
			printk("iostash: HDD owned exclusively \n");
			break;
		}

		printk("bdev %p opened for path %s.\n", hdd->bdev, path);

		if (hdd->bdev->bd_disk->queue->make_request_fn == iostash_mkrequest) {
			printk("iostash: driver is already installed\n");
			break;
		}
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
		hdd->nr_sctr = get_capacity(hdd->bdev->bd_disk);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0)
		hdd->nr_sctr = hdd->bdev->bd_part->nr_sects;
#else
		hdd->nr_sctr = part_nr_sects_read(hdd->bdev->bd_part);
#endif
		/* TODO: support caching from partitions */
		hdd->part_start = 0;
		hdd->part_end = hdd->part_start + hdd->nr_sctr - 1;

		printk("iostash: hdd->nr_sctr    = %ld\n", (long)hdd->nr_sctr);
		printk("iostash: hdd->part_start = %ld\n",
		       (long)hdd->part_start);
		printk("iostash: hdd->part_end   = %ld\n", (long)hdd->part_end);

		hdd->io_pool =
		    mempool_create_slab_pool(IOSTASH_MINIOS, gctx.io_pool);
		if (hdd->io_pool == NULL) {
			printk("iostash: failed to create io pool\n");
			break;
		}

		hdd->bs = bioset_create(IOSTASH_MINIOS, 0);
		if (!hdd->bs) {
			printk("iostash: failed to create bioset\n");
			break;
		}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36)
		hdd->io_queue = alloc_workqueue("kiostashd", WQ_MEM_RECLAIM, 1);
#else
		hdd->io_queue = create_singlethread_workqueue("kiostashd");
#endif
		if (!hdd->io_queue) {
			printk("iostash: failed to create workqueue\n");
			break;
		}

		hdd->lun = sce_addlun(gctx.sce, hdd->nr_sctr, hdd);
		if (hdd->lun == NULL) {
			printk("iostash: sce_addlun() failed \n");
			break;
		}

		ret = _hdd_create_kobj(hdd);
		if (ret) {
			printk("hdd_create_kobj failed with %d\n", ret);
			break;
		}

		/* insert it to our hdd hash table, it is ready to service requests */
		_insert_hdd(hdd);

		hdd->online = 1;

		hdd->org_mapreq = hdd->bdev->bd_disk->queue->make_request_fn;
		/* this has to be last, after we are ready to service requests */
		hdd->bdev->bd_disk->queue->make_request_fn = iostash_mkrequest;
		/* beyond this point requests might go to iostash_mkrequest */

		wmb();		/* broadcast change in make_request_fn asap */
		gctx.nr_hdd++;
		ret = 0;

		printk("iostash: HDD %s has been added successfully\n", path);
	} while (0);

	if (ret)
		_destroy_hdd(hdd);

	mutex_unlock(&gctx.ctl_mtx);

	return ret;
}

/* assumes ctl_mtx is held */
void _hdd_remove(struct hdd_info * hdd)
{
	if (NULL == hdd)
		return;

	_unload_hdd(hdd);
	kobject_put(&hdd->kobj);
	printk("iostash: %s has been successfully removed.\n",
		hdd->path);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,1,0)
	kfree(hdd);
#endif
}

void hdd_unregister_by_hdd(struct hdd_info *hdd)
{
	mutex_lock(&gctx.ctl_mtx);
	_hdd_remove(hdd);
	mutex_unlock(&gctx.ctl_mtx);
}

void hdd_unregister(char *path)
{
	struct block_device *const bdev = lookup_bdev(path);
	dev_t dev_t = 0;
	printk("bdev %p found for path %s.\n", bdev, path);
	if (IS_ERR(bdev))
		return;
	dev_t = bdev->bd_dev;
	bdput(bdev);
	mutex_lock(&gctx.ctl_mtx);
	_hdd_remove(_search_hdd(dev_t));
	mutex_unlock(&gctx.ctl_mtx);
}

/* this should only be called in module unload time */
void hdd_unregister_all(void)
{
	int i;
	struct hdd_info *hdd, *q;
#if KERNEL_VERSION(3,2,0) <= LINUX_VERSION_CODE
	LIST_HEAD(l);
#endif

	mutex_lock(&gctx.ctl_mtx);
	for (i = 0; i < IOSTASH_MAXHDD_BCKTS; ++i)
	{
		struct list_head *const b = &gctx.hddtbl.bucket[i];
		list_for_each_entry_safe(hdd, q, b, list) {
			_hdd_remove(hdd);
#if KERNEL_VERSION(3,2,0) <= LINUX_VERSION_CODE
			list_del_rcu(&hdd->list);
			synchronize_rcu();
			list_add_tail(&hdd->list, &l);
#endif
		}
	}
	printk("iostash: wait all HDDs are deleted\n");

#if KERNEL_VERSION(3,2,0) <= LINUX_VERSION_CODE
	rcu_barrier();
	list_for_each_entry_safe(hdd, q, &l, list) {
		list_del(&hdd->list);
		kfree(hdd);
	}
#endif
	mutex_unlock(&gctx.ctl_mtx);

	while (gctx.nr_hdd) {
		msleep(100);
	}
}

/**
 * assumes it's being called from within an rcu read-side critical
 * section, */
struct hdd_info *hdd_search(struct bio *bio)
{
	BUG_ON(NULL == bio || NULL == bio->bi_bdev);

	return _search_hdd(bio->bi_bdev->bd_dev);
}
