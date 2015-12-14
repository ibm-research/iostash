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

#ifndef __SCE_INTERNAL_H__
#define __SCE_INTERNAL_H__

typedef uint32_t bitmap_t;
#define BITMAPENT_SIZE          (sizeof(bitmap_t) * 8)	/* Bit counts per bitmap entry  */

/* Fragment Map (Fragment descriptor) */
typedef uint32_t fragdesc_t;	/* Describe one logical fragment space within a LUN     */
#define FRAGDESC_MAPPED         (1 << 31)	/* a fragment is mapped to physical fragment */
#define FRAGDESC_VALID          (1 << 30)	/* a fragment can service read requests      */

#define FRAGDESC_RESERVED       (3 << 28)	/* 2 bits are reserved                       */
#define FRAGDESC_FLAGMASK       (FRAGDESC_MAPPED | FRAGDESC_VALID | FRAGDESC_RESERVED)
#define FRAGDESC_DATAMASK       (~FRAGDESC_FLAGMASK)
#define FRAGDESC_DATA(d)        ((d) & FRAGDESC_DATAMASK)
#define FRAGDESC_INC(d)         (((d) & FRAGDESC_FLAGMASK) | \
                                 ((FRAGDESC_DATA(d) + 1) & FRAGDESC_DATAMASK))

#define FRAGNUM_WITHINFREELIST  (0xffffffff)
#define FRAGNUM_FREE            (0xfffffffe)

#define OS_ALLOCSIZE            (4 * 1024 * 1024)
#define MAXFRAGS4FTBL           (OS_ALLOCSIZE / sizeof(frag_t))
#define MAXFRAGS4FMAP           (OS_ALLOCSIZE / sizeof(fragdesc_t))
#define BITMAPARRAYSIZE         ((SCE_PAGEPERFRAG + BITMAPENT_SIZE - 1) / BITMAPENT_SIZE)

/* Physical fragment id = #dev(4bits) + offset */
typedef uint32_t pfid_t;

#define PFID_BITS4DEV           (SCE_BITS4DEV)
#define PFID_BITS4OFF           (28 - SCE_BITS4DEV)	/* 24 bits x 1MB = 16TB */
#define PFID_DEVMASK            ((1 << PFID_BITS4DEV) - 1)
#define PFID_OFFMASK            ((1 << PFID_BITS4OFF) - 1)

#define SCE_PFIDDEV(pf)         (((pf) >> PFID_BITS4OFF) & PFID_DEVMASK)
#define SCE_PFIDOFF(pf)         ((pf) & PFID_OFFMASK)
#define SCE_PFID(dev, off)      ((((dev) & PFID_DEVMASK) << PFID_BITS4OFF) | \
					((off) & PFID_OFFMASK))


#ifdef SCE_AWT

#define NR_AWTBITMAP                        (512)

/* bitmap_awt  */
typedef union _awtbt {
	bitmap_t      bitmap[BITMAPARRAYSIZE];
	union _awtbt *next;
} awtbt_t;

#endif /* SCE_AWT  */

/* fragment descriptor */
typedef struct frag {
	/* Logical address of Fragment */
	uint16_t lunidx;
	uint32_t fragnum;

	/* invalid page bitmap: 0 - valid, 1 - invalid  */
	bitmap_t bitmap[BITMAPARRAYSIZE];

	/* number of valid pages */
	uint16_t nr_valid;

	/* number of read requests being serviced */
	uint16_t nr_service;

	/* hit count / miss count for this fragment */
	uint32_t nr_hit;
	uint32_t nr_miss;

	/* reference counter */
	uint8_t  nr_ref;

	/* for linked list: e.g., free frag list */
	pfid_t next;

#ifdef SCE_AWT
	uint16_t nr_awt;
	awtbt_t *pending_awt;
#endif				/* SCE_AWT  */

} frag_t;

/* cache device */
typedef struct cdev {
	/* parent pointer */
	sce_hndl_t scehndl;

	/* cdev context */
	void *cdevctx;

	uint32_t nr_frag;
	frag_t *fragtbl;
} cdev_t;

#define GET_LUNIDX(sce, lun)                    (uint16_t)((lun) - (sce->luntbl))
#define GET_CDEVIDX(sce, cdev)                  ((cdev) - (sce->cdevtbl))

/* cache LUN */
typedef struct lun {
	/* parent pointer */
	sce_hndl_t scehndl;

	/* fragment map */
	uint32_t nr_fragmap;
	fragdesc_t **fragmaps;

	/* lun context */
	void *lunctx;

	/* LUN size in # of sectors */
	sector_t nr_sctr;
	uint32_t nr_frag;

	/* Lock for LUN access */
	spinlock_t lock;

	/* number of servicing req. */
	uint32_t nr_service;

	/* deletion flag */
	bool  waiting4deletion;
} lun_t;

/* SCE */
typedef struct sce {
	spinlock_t lock;

	/* cache dev info */
	uint32_t nr_cdev;
	cdev_t cdevtbl[SCE_MAXCDEV];

	/* cache lun info */
	uint32_t nr_lun;
	lun_t luntbl[SCE_MAXLUN];

	/* total number of fragments */
	uint32_t nr_frag;

	/* free fragment list */
	uint32_t nr_freefrag;
	pfid_t freefraglist;

	/* hit/miss count */
	uint32_t nr_hit;
	uint32_t nr_miss;
	uint32_t nr_eviction;

	/* Clock ARM */
	pfid_t arm;

	/* Cache miss log */
	uint32_t misslog_fragnum[SCE_CACHEMISSLOGSIZE];
	uint16_t misslog_lunidx[SCE_CACHEMISSLOGSIZE];

	uint32_t misslog_head;
	uint32_t misslog_size;

#ifdef SCE_AWT
	awtbt_t awtbt_pool[NR_AWTBITMAP];
	awtbt_t *free_awtbt;
#endif				/* SCE_AWT  */

} sce_t;


/* Fragment related  */
	int _frag_init(frag_t * frag);
	int _set_bitmap(bitmap_t * bitmap, uint32_t pagenum, uint32_t pagecnt);
	int _reset_bitmap(bitmap_t * bitmap, uint32_t pagenum,
			  uint32_t pagecnt);
	int _frag_invalidate(frag_t * frag, uint32_t pagenum, uint32_t pagecnt);
	int _frag_isvalid(frag_t * frag, uint32_t pagenum, uint32_t pagecnt);

#ifdef SCE_AWT
	int _frag_writestart(frag_t *frag, uint32_t pgnum, uint32_t pgcnt);
	void _frag_writeend(frag_t *frag, uint32_t pgnum, uint32_t pgcnt, bool failed);
	void _frag_mergebitmap(frag_t *frag);
	int _set_bitmap4awt(bitmap_t *, bitmap_t *, uint32_t, uint32_t);
#endif

/* Cache device (SSD) related */
	int _cdev_init(sce_t * sce, cdev_t * cdev, sector_t nr_sctr);
	int _cdev_search(sce_t * sce, void* cdevctx);
	int _cdev_destroy(cdev_t * cdev);

/* LUN related */
	int _lun_init(sce_t * sce, lun_t * lun, sector_t nr_sctr);
	int _lun_search(sce_t * sce, void* lunctx);
	int _lun_destroy(lun_t * lun);
	int _lun_is_serviceable(lun_t * lun);
	int _lun_purge(lun_t * lun);
	int _lun_gc(lun_t * lun, frag_t * frag);
	int _lun_isvalididx(sce_t * sce, uint16_t lunidx);
	int _lun_rmcdev(lun_t * lun, uint32_t devnum);

/* Utility functions */
	frag_t *_pfid2frag(sce_t * sce, pfid_t pfid, sce_fmap_t* out_fmap);
	int _isvalidpfid(sce_t * sce, pfid_t pfid);

/* Free fragment list */
	int _freefraglist_put(sce_t * sce, pfid_t fid);
	int _freefraglist_get(sce_t * sce, pfid_t * out_pfid);
	int _freefraglist_rmcdev(sce_t * sce, uint32_t devnum);

#ifdef SCE_AWT
	void _awtbt_put(sce_t * sce, awtbt_t * abt);
	awtbt_t *_awtbt_get(sce_t * sce);
#endif				/* SCE_AWT  */

/* Fragment mapping related */
	int _map_frag(lun_t * lun, uint32_t fragnum, pfid_t pfid);
	int _unmap_frag(lun_t * lun, uint32_t fragnum);

/* Cache replacement algorithm */
	frag_t *_get_frag_advance_clockarm(sce_t * sce);
	int _evict_frag(sce_t * sce);

/* Must-have queue */
	int _musthaveq_put(sce_t * sce, uint16_t lunidx, uint32_t fragnum);
	int _musthaveq_get(sce_t * sce, uint16_t * out_lunidx,
			   uint32_t * out_fragnum);
	int _musthaveq_gc(sce_t * sce);

/* Cache miss log */
	int _misslog_put(sce_t * sce, uint16_t lunidx, uint32_t fragnum);
	int _misslog_get(sce_t * sce, uint32_t idx, uint16_t * out_lunidx,
			 uint32_t * out_fragnum);
	int _misslog_gc(sce_t * sce);

/* cache population related */
	int _alloc4population(lun_t * lun, uint32_t fragnum, pfid_t * out_pfid);
	int _complete_population(lun_t * lun, uint32_t fragnum);
	int _cancel_population(lun_t * lun, uint32_t fragnum);
	int _choose4population(sce_t * sce, uint16_t * out_lunidx,
			       uint32_t * out_fragnum);

#endif				/* __SCE_INTERNAL_H__ */
