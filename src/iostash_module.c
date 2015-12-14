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


#define __MAIN_MODULE__

#include <linux/sysfs.h>

#include "iostash.h"
#include "helpers.h"

static char *_chkcmd(const char *cmdline, char *cmd)
{
	char *arg = strstr(cmdline, cmd);
	char *p;

	if (arg) {
		arg += strlen(cmd);

		/* skip spaces */
		while ((*arg != '\0') && (*arg <= ' '))
			arg++;

		/* get only one word */
		for (p = arg; (*p != '\0') && (*p > ' '); p++) ;
		*p = '\0';

		DBG("iostash: Command[%s] Argument[%s]\n", cmd, arg);
	}

	return arg;
}

/* KObject section starts here */

struct ctl_attribute
{
        struct attribute attr;
        ssize_t (*show) (struct kobject *kobj, char *buffer );
        ssize_t (*store) (struct kobject *kobj, const char *buffer, size_t size );
};

static ssize_t caches_show(struct kobject *kobj, char *buffer)
{
	return 0;
}
static ssize_t caches_store(struct kobject *kobj, const char *buffer, size_t size)
{
	char *arg;

	if ((arg = _chkcmd(buffer, "add")) != NULL)
		ssd_register(arg);
	else if ((arg = _chkcmd(buffer, "rm")) != NULL)
		ssd_unregister(arg);
	else
		ERR("Unknown kobject store to %s\n", kobject_name(kobj));

	return size;
}
static struct ctl_attribute ctl_attr_cache = __ATTR_RW(caches);

static ssize_t targets_show(struct kobject *kobj, char *buffer)
{
	return 0;
}
static ssize_t targets_store(struct kobject *kobj, const char *buffer, size_t size)
{
	char *arg;

	if ((arg = _chkcmd(buffer, "add")) != NULL)
		hdd_register(arg);
	else if ((arg = _chkcmd(buffer, "rm")) != NULL)
		hdd_unregister(arg);
	else
		ERR("Unknown kobject store to %s\n", kobject_name(kobj));

	return size;
}
static struct ctl_attribute ctl_attr_target = __ATTR_RW(targets);

static ssize_t gstats_show(struct kobject *kobj, char *buffer)
{
	sce_status_t st;
	ssize_t len = 0;

	if (gctx.sce)
	{
		sce_get_status(gctx.sce, &st);
#ifdef SCE_AWT
		sprintf(buffer,
			   "l:%d,s:%d,r:%d,w:%d,cr:%d,p:%d,tf:%d,ff:%d,e:%d,awt:%d\n",
			   gctx.nr_hdd, gctx.nr_ssd, gctx.st_read,
			   gctx.st_write, gctx.st_cread, gctx.st_population,
			   st.nr_totfrag, st.nr_freefrag, st.nr_eviction,
			   gctx.st_awt);
#else
		sprintf(buffer,
			   "l:%d,s:%d,r:%d,w:%d,cr:%d,p:%d,tf:%d,ff:%d,e:%d\n",
			   gctx.nr_hdd, gctx.nr_ssd, gctx.st_read,
			   gctx.st_write, gctx.st_cread, gctx.st_population,
			   st.nr_totfrag, st.nr_freefrag, st.nr_eviction);
#endif
	}
	else
	{
		BUG();
	}

	len = strlen(buffer);
	BUG_ON(len > PAGE_SIZE);

	return len;
}
static struct ctl_attribute ctl_attr_gstats = __ATTR_RO(gstats);

static struct attribute * ctl_attrs[] = {
        &ctl_attr_cache.attr,
	&ctl_attr_target.attr,
	&ctl_attr_gstats.attr,
	NULL
};

static ssize_t ctl_show(struct kobject *kobj, struct attribute *attr, char *buffer)
{
        struct ctl_attribute *ca = container_of(attr, struct ctl_attribute, attr);
        return ca->show ? ca->show(kobj, buffer) : 0;
};

static ssize_t ctl_store(struct kobject *kobj, struct attribute *attr, const char *buffer, size_t size)
{
        struct ctl_attribute *ca = container_of(attr, struct ctl_attribute, attr);
        return ca->store ? ca->store(kobj, buffer, size) : size;
}

static const struct sysfs_ops ctl_sysfs_ops =
{
        .show  = ctl_show,
        .store = ctl_store
};

static void ctl_kobj_release(struct kobject *kobj)
{
        DBG("released: %s\n", kobject_name(kobj));
}

static int _init_iostash_kobjects(void)
{
	int err = 0;

	static struct kobj_type ctl_kobj_type = {
                .release       = ctl_kobj_release,
                .sysfs_ops     = &ctl_sysfs_ops,
                .default_attrs = ctl_attrs,
        };

	memset(&gctx.ctl_kobj, 0, sizeof(gctx.ctl_kobj));
	kobject_init(&gctx.ctl_kobj, &ctl_kobj_type);

	err = kobject_add(&gctx.ctl_kobj,
			  (&(THIS_MODULE)->mkobj.kobj),
			  "%s",
			  CTL_KOBJ_NAME);
	if (err)
	{
		kobject_put(&gctx.ctl_kobj);
		err = -ENOMEM;
		goto out;
	}

	gctx.ssd_kset = kset_create_and_add
		(SSD_KSET_NAME, NULL, (&(THIS_MODULE)->mkobj.kobj));
	if (!gctx.ssd_kset)
	{
		err = -ENOMEM;
		goto kobj_del;
	}

	gctx.hdd_kset = kset_create_and_add
		(HDD_KSET_NAME, NULL, (&(THIS_MODULE)->mkobj.kobj));
	if (!gctx.hdd_kset)
	{
		err = -ENOMEM;
		goto unreg_ssd_kset;
	}

	BUG_ON(0 != err);

	return 0;

kobj_del:
	kobject_del(&gctx.ctl_kobj);
	kobject_put(&gctx.ctl_kobj);

unreg_ssd_kset:
	kset_unregister(gctx.ssd_kset);

out:
	return err;
}

static void _destroy_iostash_kobjects(void)
{
	kobject_del(&gctx.ctl_kobj);
	kobject_put(&gctx.ctl_kobj);
	kset_unregister(gctx.ssd_kset);
	kset_unregister(gctx.hdd_kset);

	return;
}

static void _free_resource(void)
{
	ssd_unregister_all();
	hdd_unregister_all();

	if (gctx.pdm) {
		pdm_destroy(gctx.pdm);
		gctx.pdm = NULL;
	}
	if (gctx.sce) {
		sce_destroy(gctx.sce);
		gctx.sce = NULL;
	}
	if (gctx.io_client) {
		dm_io_client_destroy(gctx.io_client);
		gctx.io_client = NULL;
	}
	if (gctx.io_pool) {
		kmem_cache_destroy(gctx.io_pool);
		gctx.io_pool = NULL;
	}
}

static int __init iostash_init(void)
{
    int ret = -1, i;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
	ERR("Kernel version < 2.6.28 not supported.");
	return -ENOENT;
#endif
	DBG("++ iostash_init() ++\n");

	memset(&gctx, 0, sizeof(gctx));
	do {
		gctx.io_pool = KMEM_CACHE(iostash_bio, 0);
		if (!gctx.io_pool) {
			ERR("iostash_init: KMEM_CACHE() failed\n");
			break;
		}

		gctx.io_client = dm_io_client_create();
		if (IS_ERR(gctx.io_client)) {
			ERR("iostash_init: dm_io_client() failed\n");
			break;
		}

		gctx.sce = sce_create();
		if (!gctx.sce) {
			ERR("iostash_init: sce_create() failed\n");
			break;
		}

		gctx.pdm = pdm_create(gctx.sce, poptask_read, poptask_write);

		if (_init_iostash_kobjects()) {
		    ERR("KOBJECT INIT FAILED!");
		    _destroy_iostash_kobjects();
		}

		mutex_init(&gctx.ctl_mtx);

		for (i = 0; i < IOSTASH_MAXHDD_BCKTS; ++i)
		    INIT_LIST_HEAD(&gctx.hddtbl.bucket[i]);

		for (i = 0; i < IOSTASH_MAXSSD_BCKTS; ++i)
		    INIT_LIST_HEAD(&gctx.ssdtbl.bucket[i]);

		ret = 0;

	} while (0);

	if (ret) {
		_free_resource();
	}

	DBG("-- iostash_init() returns = %d --\n", ret);
	return ret;
}

static void iostash_exit(void)
{
	DBG("++ iostash_exit() ++\n");
	_free_resource();
	_destroy_iostash_kobjects();
	DBG("-- iostash_exit() ++\n");
}

module_init(iostash_init);
module_exit(iostash_exit);

MODULE_AUTHOR("IBM");
MODULE_DESCRIPTION("Flash cache solution iostash driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
