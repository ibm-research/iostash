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

#ifndef __IOSTASH_H__
#define __IOSTASH_H__

#ifdef __MAIN_MODULE__
#define GVAR
#else
#define GVAR    extern
#endif

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <linux/device-mapper.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/mutex.h>

#include <linux/dm-io.h>
#include <linux/bio.h>
#include <linux/sunrpc/types.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "sce.h"
#include "pdm.h"

#define IOSTASH_NAME           "iostash"

/* these are for sysfs */
#define CTL_KOBJ_NAME          "iostash-ctl"
#define SSD_KSET_NAME          "caches"
#define HDD_KSET_NAME          "targets"

#define IOSTASH_ADJUST_ALIGNMENT

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
#define BIO_SECTOR(bio)     ((bio)->bi_iter.bi_sector)
#define BIO_SIZE(bio)       ((bio)->bi_iter.bi_size)
#else
#define BIO_SECTOR(bio)     ((bio)->bi_sector)
#define BIO_SIZE(bio)       ((bio)->bi_size)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0)
#define BIO_CLONEBS(bio,bs) bio_clone_bioset((bio), GFP_KERNEL, (bs))
#else
#define BIO_CLONEBS(_bio,bs) ({\
			struct bio *const __clone = bio_alloc_bioset(GFP_KERNEL, bio_segments((_bio)), (bs)); \
			if (NULL != __clone) {				\
				__bio_clone(__clone, (_bio));		\
			}						\
			__clone;})
#endif


#define IOSTASH_HEADERSIZE     (1024 * 1024)
#define IOSTASH_HEADERSCT      (IOSTASH_HEADERSIZE / SCE_SCTRSIZE)
#define IOSTASH_MAXPATH        (80)
#define IOSTASH_MAXSSD         (SCE_MAXCDEV)
#define IOSTASH_MAXSSD_BCKTS   (IOSTASH_MAXSSD * 2)
#define IOSTASH_MAXHDD         (SCE_MAXLUN)
#define IOSTASH_MAXHDD_BCKTS	(SCE_MAXLUN * 2)

#define IOSTASH_MINIOS         (16)

struct ssd_info
{
	struct list_head list;	/* hash table bucket list membership */
	struct kobject kobj;
	struct block_device *bdev;
	dev_t                dev_t;		/* id for lookup */
	char path[IOSTASH_MAXPATH];
	sector_t nr_sctr;
	sce_cdevhndl_t cdev;
	int online;
	atomic_t nr_ref;
};

struct hdd_info
{
	struct list_head list;	/* hash table bucket list membership */
	struct kobject kobj;
	struct request_queue *request_q;
	struct block_device *bdev;
	dev_t                dev_t;		/* id for lookup */
	char path[IOSTASH_MAXPATH];
	sector_t nr_sctr;
	sector_t part_start;
	sector_t part_end;

	sce_lunhndl_t lun;

	volatile int online;	/* guards usage during removal */
	atomic_t     nr_ref;

	mempool_t *io_pool;
	struct bio_set *bs;
	struct workqueue_struct *io_queue;

	make_request_fn *org_mapreq;
	atomic_t io_pending;
};

struct hdd_htable
{
	struct list_head bucket[IOSTASH_MAXHDD_BCKTS];	/* bucket list head */
};

struct ssd_htable
{
	struct list_head bucket[IOSTASH_MAXSSD_BCKTS];	/* bucket list head */
};

typedef struct {
	sce_hndl_t sce;
	pdm_hndl_t pdm;

	/* big fat control op lock */
	struct mutex ctl_mtx;

	struct ssd_htable ssdtbl;
	unsigned nr_ssd;

	struct hdd_htable hddtbl;
	unsigned nr_hdd;

	/* I/O related */
	struct kmem_cache *io_pool;
	struct dm_io_client *io_client;

	/* statistics */
	unsigned st_read;	/* total reads */
	unsigned st_write;	/* total writes */
	unsigned st_cread;	/* cached read (cache hit case) */
	unsigned st_population;	/* total populations */
#ifdef SCE_AWT
	unsigned st_awt;
#endif
	/* related to kobjects */
	struct kobject  ctl_kobj;
	struct kset    *ssd_kset;
	struct kset    *hdd_kset;
} IOSTASHCTX;

struct iostash_bio
{
	struct hdd_info *hdd;
	struct ssd_info *ssd;
	struct bio *base_bio;
	uint32_t fragnum;
	struct work_struct work;
	atomic_t io_pending;
	int error;
	int ssd_werr;
	sector_t psn;
};

/* procfs */
void iostash_proc_create(void);
void iostash_proc_delete(void);

/* pdm I/O */
int poptask_read(sce_poptask_t * poptask, char *fragbuf);
int poptask_write(sce_poptask_t * poptask, char *fragbuf);

/* SSD registration  */
int ssd_register(char *path);
void ssd_unregister(char *path);
void ssd_unregister_all(void);

/* HDD registration  */
int hdd_register(char *path);
void hdd_unregister(char *path);
void hdd_unregister_by_hdd(struct hdd_info *hdd);
void hdd_unregister_all(void);
struct hdd_info *hdd_search(struct bio *bio);

/* Request hooking */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,1,0)
int iostash_mkrequest(struct request_queue *q, struct bio *bio);
#else
void iostash_mkrequest(struct request_queue *q, struct bio *bio);
#endif

/* global variables */
GVAR IOSTASHCTX gctx;

#endif /* __IOSTASH_H__ */
