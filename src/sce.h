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

#ifndef _SCE_H
#define _SCE_H

#ifndef _SCE_CROSSTEST
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/vmalloc.h>

#define	PRIVATE                 static

#else
#include "sce_crosstest.h"
#define	PRIVATE
#endif

#define SCE_BITS4DEV            (4)  /* support maximum 16 SSDs */
#define SCE_MAXLUN              (256)
#define SCE_REFCEILING          (4)
#define SCE_MUSTHAVEQSIZE       (1024 * 100)	/* 100 GB - 100 extents    */
#define SCE_CACHEMISSLOGSIZE    (1024)

#define SCE_MISSWINDOW          (100)

#define SCE_MAXCDEV             (1 << SCE_BITS4DEV)

//#define SCE_AWT			/* Asynchronous write through support */
//#define SCE_POPULATION_MRU    /* populate MRU fragment */
#define SCE_POPULATION_HOTMRU	/* populate the hottest MRU fragment within cache miss window */

#define SCE_FRAGSIZE            (1024 * 1024)	/* Fragment size: 1MB   */
#define SCE_POPCNT              (1024 * 1024 / SCE_FRAGSIZE)
#define SCE_PAGESIZE            (4096)	/* Page     size: 4KB   */
#define SCE_SCTRSIZE            (512)	/* Sector   size: 512B  */

#define SCE_PAGEPERFRAG         (SCE_FRAGSIZE / SCE_PAGESIZE)
#define SCE_SCTRPERFRAG         (SCE_FRAGSIZE / SCE_SCTRSIZE)
#define SCE_SCTRPERPAGE         (SCE_PAGESIZE / SCE_SCTRSIZE)

/* return code from SCE */
#define SCE_SUCCESS             (0)
#define SCE_ERROR               (1)

typedef void *sce_hndl_t;
typedef void *sce_cdevhndl_t;
typedef void *sce_lunhndl_t;

typedef struct {
	uint32_t nr_hit;
	uint32_t nr_miss;
	uint32_t nr_freefrag;
	uint32_t nr_totfrag;
	uint32_t nr_eviction;
} sce_status_t;

typedef struct {
	void *lunctx;
	void *cdevctx;
	uint32_t lun_fragnum;
	uint32_t cdev_fragnum;
} sce_poptask_t;

typedef struct {
	void*    cdevctx;
	uint32_t fragnum;
} sce_fmap_t;

/* APIs for initialization / termination / configuration */
extern sce_hndl_t    sce_create(void);
extern int           sce_destroy(sce_hndl_t scehndl);

extern sce_cdevhndl_t sce_addcdev(sce_hndl_t, sector_t, void *);
extern int sce_rmcdev(sce_cdevhndl_t devhndl);
extern sce_lunhndl_t sce_addlun(sce_hndl_t, sector_t, void *);
extern int sce_rmlun(sce_lunhndl_t lunhndl);

/* APIs for read and write I/O handling */
extern int sce_get4read(sce_lunhndl_t, sector_t, uint32_t, sce_fmap_t*);
extern int sce_put4read(sce_lunhndl_t, sector_t, uint32_t);
extern int sce_invalidate(sce_lunhndl_t, sector_t, uint32_t);

#ifdef SCE_AWT
extern int sce_get4write(sce_lunhndl_t, sector_t, uint32_t, sce_fmap_t*);
extern int sce_put4write(sce_lunhndl_t, sector_t, uint32_t, int);
#endif

/* APIs for cache population */
extern int sce_get_status(sce_hndl_t scehndl, sce_status_t * out_status);
extern int sce_get4pop(sce_hndl_t scehndl, sce_poptask_t *);
extern int sce_put4pop(sce_hndl_t scehndl, sce_poptask_t * poptask, int);

#endif /* _SCE_H */
