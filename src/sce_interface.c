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


#include "sce.h"
#include "sce_internal.h"

/* ---------------------------------------------------------------------------------------- */
/*               APIs for initialization / termination / configuration                      */
/* ---------------------------------------------------------------------------------------- */
sce_hndl_t sce_create(void)
{
	sce_t *sce;
	int    i;

	sce = kmalloc(sizeof(sce_t), GFP_KERNEL);
	if (!sce)
		goto out;

	memset(sce, 0, sizeof(sce_t));
	spin_lock_init(&sce->lock);

	/* initialize spin-locks for LUNs */
	for (i = 0; i < SCE_MAXLUN; i++) {
		spin_lock_init(&sce->luntbl[i].lock);
	}

#ifdef SCE_AWT
	for (i = 0; i < NR_AWTBITMAP; i++) {
		_awtbt_put(sce, &sce->awtbt_pool[i]);
	}
#endif

out:
	return (sce_hndl_t) sce;
}

int sce_destroy(sce_hndl_t scehndl)
{
	sce_t *sce;
	int    i;

	if (!scehndl)
		return SCE_ERROR;

	sce = (sce_t *) scehndl;

	for (i = 0; i < SCE_MAXCDEV; i++) {
		_cdev_destroy(&sce->cdevtbl[i]);
	}
	for (i = 0; i < SCE_MAXLUN; i++) {
		_lun_destroy(&sce->luntbl[i]);
	}
	kfree(sce);

	return SCE_SUCCESS;
}

sce_cdevhndl_t sce_addcdev(sce_hndl_t scehndl, sector_t nr_sctr, void *cdctx)
{
	unsigned long flags;
	sce_t        *sce;
	cdev_t       *cdev;
	pfid_t        devnum;
	uint32_t      fragnum;

	if (!scehndl)
		return NULL;

	sce = (sce_t *) scehndl;

	if ((!nr_sctr) || (sce->nr_cdev >= SCE_MAXCDEV))
		return NULL;

	if (_cdev_search(sce, cdctx) >= 0)
		return NULL;

	spin_lock_irqsave(&sce->lock, flags);

	for (cdev = sce->cdevtbl, devnum = 0;
	     devnum < SCE_MAXCDEV; cdev++, devnum++) 
	    {
		    if (cdev->fragtbls == NULL)
			    break;
	    }

	if ((devnum < SCE_MAXCDEV) &&
	    (_cdev_init(sce, cdev, nr_sctr) == SCE_SUCCESS)) {
		cdev->cdevctx = cdctx;
		sce->nr_cdev++;
		sce->nr_frag += cdev->nr_frag;
		fragnum = cdev->nr_frag - 1;

		do {
			_freefraglist_put(sce, SCE_PFID(devnum, fragnum));
		} while (fragnum-- > 0);

	} else {
		cdev = NULL;
	}
	spin_unlock_irqrestore(&sce->lock, flags);

	return (sce_cdevhndl_t) cdev;
}

int sce_rmcdev(sce_cdevhndl_t devhndl)
{
	unsigned long sflags;
	unsigned long lflags;
	cdev_t       *cdev;
	sce_t        *sce;
	lun_t        *lun;
	uint32_t      devnum;
	int           i;

	if (!devhndl)
		return SCE_ERROR;

	cdev = (cdev_t *)devhndl;
	sce  = (sce_t  *)cdev->scehndl;

	if (!sce)
		return SCE_ERROR;

	spin_lock_irqsave(&sce->lock, sflags);

	devnum = cdev - sce->cdevtbl;
	BUG_ON(devnum >= SCE_MAXCDEV);

	_freefraglist_rmcdev(sce, devnum);

	for (i = 0, lun = sce->luntbl; i < SCE_MAXLUN; i++, lun++) {

		spin_lock_irqsave(&lun->lock, lflags);
		if (lun->fragmaps) {
			_lun_rmcdev(lun, devnum);
		}
		spin_unlock_irqrestore(&lun->lock, lflags);
	}

	sce->nr_cdev--;
	sce->nr_frag -= cdev->nr_frag;
	_cdev_destroy(cdev);

	spin_unlock_irqrestore(&sce->lock, sflags);

	return SCE_SUCCESS;
}

sce_lunhndl_t sce_addlun(sce_hndl_t scehndl, sector_t nr_sctr, void *lunctx)
{
	sce_t        *sce;
	lun_t        *lun;
	unsigned long flags;
	int           i;

	if ((!scehndl) || (!nr_sctr))
		return NULL;

	sce = (sce_t *)scehndl;

	spin_lock_irqsave(&sce->lock, flags);

	for (lun = sce->luntbl, i = 0; i < SCE_MAXLUN; lun++, i++) {
		if (!lun->fragmaps)
			break;
	}

	if ((i < SCE_MAXLUN) && (_lun_init(sce, lun, nr_sctr) == SCE_SUCCESS)) {
		lun->lunctx = lunctx;
		sce->nr_lun++;
	} else {
		lun = NULL;
	}
	spin_unlock_irqrestore(&sce->lock, flags);

	return (sce_lunhndl_t) lun;
}

int sce_rmlun(sce_lunhndl_t lunhndl)
{
	unsigned long sflags;
	unsigned long lflags;
	lun_t        *lun;
	sce_t        *sce;
	int           ret;

	if (!lunhndl)
		return SCE_ERROR;

	lun = (lun_t *)lunhndl;
	sce = (sce_t *)lun->scehndl;
	if (!sce)
		return SCE_ERROR;

	spin_lock_irqsave(&sce->lock, sflags);
	spin_lock_irqsave(&lun->lock, lflags);

	ret = SCE_ERROR;
	if (lun->fragmaps) {
		if (lun->nr_service > 0) {
			lun->waiting4deletion = true;
			ret = SCE_SUCCESS;
		} else {
			ret = _lun_purge(lun);
		}
	}

	spin_unlock_irqrestore(&lun->lock, lflags);
	spin_unlock_irqrestore(&sce->lock, sflags);

	return ret;
}

/* ---------------------------------------------------------------------------------------- */
/*                        APIs for read and write I/O handling                              */
/* ---------------------------------------------------------------------------------------- */

/* Check validity of arguments */
static lun_t* _lock_lun(sce_lunhndl_t lunhndl, sector_t sctrnum,
			uint32_t nr_sctr, unsigned long* flags, bool svcchk)
{
	lun_t *lun;

	if ((!flags) || (!lunhndl))
		goto out;

	lun = (lun_t *) lunhndl;
	if (!lun->scehndl)
		goto out;

	spin_lock_irqsave(&lun->lock, *flags);

	if ((!lun->fragmaps) || (!nr_sctr) ||
	    ((sctrnum + nr_sctr) > lun->nr_sctr))
		goto unlock_and_out;

	if ((svcchk) && (_lun_is_serviceable(lun) != SCE_SUCCESS))
		goto unlock_and_out;

	return lun;

unlock_and_out:
	spin_unlock_irqrestore(&lun->lock, *flags);

out:
	return NULL;
}

int sce_get4read(sce_lunhndl_t lunhndl, sector_t sctrnum, uint32_t nr_sctr, 
		sce_fmap_t* out_fmap)
{
	lun_t        *lun;
	sce_t        *sce;
	frag_t       *frag;
	unsigned long flags;
	sector_t      firstpg;
	sector_t      lastpg;
	uint32_t      fragnum;
	fragdesc_t    fdesc;
	uint32_t      pgcnt;
	uint32_t      pgoff;
	int           ret = SCE_ERROR;

	if (!out_fmap) 
		return ret;
	
	lun = _lock_lun(lunhndl, sctrnum, nr_sctr, &flags, true);
	if (!lun)
		return ret;
	sce = (sce_t *) lun->scehndl;

	firstpg =  sctrnum                / SCE_SCTRPERPAGE;
	lastpg  = (sctrnum + nr_sctr - 1) / SCE_SCTRPERPAGE;
	fragnum = (uint32_t) (firstpg / SCE_PAGEPERFRAG);
	pgoff   = (uint32_t) (firstpg % SCE_PAGEPERFRAG);
	pgcnt   = (uint32_t) (lastpg - firstpg + 1);

	if ((pgoff + pgcnt) > SCE_PAGEPERFRAG)
		goto unlock_and_out;

	fdesc = lun->fragmaps[fragnum / MAXFRAGS4FMAP][fragnum % MAXFRAGS4FMAP];
	if (!(fdesc & FRAGDESC_MAPPED))
	{
		lun->fragmaps[fragnum / MAXFRAGS4FMAP][fragnum % MAXFRAGS4FMAP]
			= FRAGDESC_INC(fdesc);
		fdesc = 0;
	} else {
		frag = _pfid2frag(sce, fdesc, out_fmap);
		if ((fdesc & FRAGDESC_VALID) &&
	            (SCE_SUCCESS == _frag_isvalid(frag, pgoff, pgcnt))) {
			frag->nr_hit++;
			frag->nr_service++;
			lun->nr_service++;
			if (frag->nr_ref < SCE_REFCEILING)
				frag->nr_ref++;
			sce->nr_hit++;
			ret = SCE_SUCCESS;
		} else {
			frag->nr_miss++;
			fdesc = 0;
		}
	}

	if (fdesc == 0) {
		sce->nr_miss++;
		_misslog_put(sce, GET_LUNIDX(sce, lun), fragnum);
	}
unlock_and_out:
	spin_unlock_irqrestore(&lun->lock, flags);

	return ret;
}

int sce_put4read(sce_lunhndl_t lunhndl, sector_t sctrnum, uint32_t nr_sctr)
{
	unsigned long flags;
	lun_t        *lun;
	sce_t        *sce;
	frag_t       *frag;
	uint32_t      fragnum;
	fragdesc_t    fdesc;
	int           ret;

	lun = _lock_lun(lunhndl, sctrnum, nr_sctr, &flags, false);
	if (!lun)
		return SCE_ERROR;
	sce = (sce_t *) lun->scehndl;

	ret = SCE_ERROR;
	fragnum = (uint32_t) (sctrnum / SCE_SCTRPERFRAG);

	if (((sctrnum + nr_sctr - 1) / SCE_SCTRPERFRAG) != fragnum)
		goto unlock_and_out;

	fdesc = lun->fragmaps[fragnum / MAXFRAGS4FMAP][fragnum % MAXFRAGS4FMAP];

	if (((fdesc & FRAGDESC_MAPPED) == 0) ||
	    ((fdesc & FRAGDESC_VALID ) == 0))
		goto unlock_and_out;

	frag = _pfid2frag(sce, fdesc, NULL);
	if (!frag)
		goto unlock_and_out;

	if ((frag->nr_service == 0) ||
	    (lun->nr_service  == 0))
		goto unlock_and_out;

	frag->nr_service--;
	lun->nr_service--;

	if (frag->nr_service == 0)
		_lun_gc(lun, frag);

	ret = SCE_SUCCESS;

unlock_and_out:
	spin_unlock_irqrestore(&lun->lock, flags);

	return ret;
}

int sce_invalidate(sce_lunhndl_t lunhndl, sector_t sctrnum, uint32_t nr_sctr)
{
	unsigned long flags;
	lun_t *lun;
	sce_t *sce;
	frag_t *frag;
	sector_t end;
	sector_t start;
	uint32_t count;
	uint32_t fragnum;
	fragdesc_t fdesc;
	uint32_t pgcnt;
	uint32_t pgoff;
	int ret;


	ret = SCE_ERROR;

	if (!lunhndl)
		goto out;

	lun = (lun_t *)lunhndl;
	sce = (sce_t *)lun->scehndl;

	if (!sce)
		goto out;

	spin_lock_irqsave(&lun->lock, flags);

	if ((!lun->fragmaps) || (!nr_sctr) ||
	    ((sctrnum + nr_sctr) > lun->nr_sctr))
		goto unlock_and_out;

	if (_lun_is_serviceable(lun) != SCE_SUCCESS)
		goto unlock_and_out;

	/* sector number to page number */
	start = sctrnum / SCE_SCTRPERPAGE;
	end   = (sctrnum + nr_sctr - 1) / SCE_SCTRPERPAGE;
	count = (uint32_t)(end - start) + 1;

	/* calculate fragment id and page offset within a fragment */
	fragnum = (uint32_t) (start / SCE_PAGEPERFRAG);
	pgoff   = (uint32_t) (start % SCE_PAGEPERFRAG);

	/* visit fragments one by one */
	for (; count > 0; fragnum++) {
		/* number of pages to check:
		   adjust the number of pages to check considering fragment boundary */
		pgcnt = SCE_PAGEPERFRAG - pgoff;
		if (pgcnt > count)
			pgcnt = count;

		/* get fragment descriptor */
		fdesc = lun->fragmaps[fragnum / MAXFRAGS4FMAP]
			             [fragnum % MAXFRAGS4FMAP];
		if (fdesc & FRAGDESC_MAPPED) {
			frag = _pfid2frag(sce, fdesc, NULL);
			if (!frag)
				break;

			_frag_invalidate(frag, pgoff, pgcnt);

			if (fdesc & FRAGDESC_VALID) {
				_lun_gc(lun, frag);
			}
		}

		/* move to the next fragment */
		pgoff  = 0;
		count -= pgcnt;
	}
	if (count == 0)
		ret = SCE_SUCCESS;

unlock_and_out:
	spin_unlock_irqrestore(&lun->lock, flags);

out:
	return ret;
}

/* ---------------------------------------------------------------------------------------- */
/*                              APIs for cache population                                   */
/* ---------------------------------------------------------------------------------------- */
int sce_get_status(sce_hndl_t scehndl, sce_status_t * out_status)
{
	unsigned long flags;
	sce_t        *sce;

	if ((!scehndl) || (!out_status))
		return SCE_ERROR;

	sce = (sce_t *)scehndl;
	spin_lock_irqsave(&sce->lock, flags);

	out_status->nr_hit      = sce->nr_hit;
	out_status->nr_miss     = sce->nr_miss;
	out_status->nr_freefrag = sce->nr_freefrag;
	out_status->nr_totfrag  = sce->nr_frag;
	out_status->nr_eviction = sce->nr_eviction;

	spin_unlock_irqrestore(&sce->lock, flags);

	return SCE_SUCCESS;
}

int sce_get4pop(sce_hndl_t scehndl, sce_poptask_t * out_poptask)
{
	unsigned long sflags;
	unsigned long lflags;
	sce_t        *sce;
	uint16_t      lunidx;
	uint32_t      fragnum;
	pfid_t        pfid;
	int           ret;

	if ((!scehndl) || (!out_poptask))
		return SCE_ERROR;

	sce = (sce_t *)scehndl;

	spin_lock_irqsave(&sce->lock, sflags);

	ret = _choose4population(sce, &lunidx, &fragnum);
	if (ret != SCE_SUCCESS)
		goto unlock_and_out;

	spin_lock_irqsave(&sce->luntbl[lunidx].lock, lflags);
	ret = _alloc4population(&sce->luntbl[lunidx], fragnum, &pfid);
	spin_unlock_irqrestore(&sce->luntbl[lunidx].lock, lflags);

	if (ret != SCE_SUCCESS)
		goto unlock_and_out;

	out_poptask->lunctx       = sce->luntbl[lunidx].lunctx;
	out_poptask->cdevctx      = sce->cdevtbl[SCE_PFIDDEV(pfid)].cdevctx;
	out_poptask->lun_fragnum  = fragnum;
	out_poptask->cdev_fragnum = SCE_PFIDOFF(pfid);

unlock_and_out:
	spin_unlock_irqrestore(&sce->lock, sflags);
	return ret;
}

int sce_put4pop(sce_hndl_t scehndl, sce_poptask_t *poptask, int failed)
{
	unsigned long flags;
	sce_t *sce;
	lun_t *lun;
	int    lunidx;
	int    ret;

	if ((!scehndl) || (!poptask))
		return SCE_ERROR;
	sce    = (sce_t *)scehndl;
	lunidx = _lun_search(sce, poptask->lunctx);
	if (lunidx < 0)
		return SCE_ERROR;
	lun = &sce->luntbl[lunidx];

	spin_lock_irqsave(&lun->lock, flags);
	if (!failed) {
		ret = _complete_population(lun, poptask->lun_fragnum);
	} else {
		ret = _cancel_population(lun, poptask->lun_fragnum);
	}
	spin_unlock_irqrestore(&lun->lock, flags);

	return ret;
}

#ifdef SCE_AWT
int sce_get4write(sce_lunhndl_t lunhndl, sector_t sctrnum, uint32_t nr_sctr,
		sce_fmap_t* out_fmap)
{
	lun_t        *lun;
	sce_t        *sce;
	frag_t       *frag;
	unsigned long flags;
	sector_t      firstpg;
	sector_t      lastpg;
	uint32_t      fragnum;
	fragdesc_t    fdesc;
	uint32_t      pgcnt;
	uint32_t      pgoff;
	int           ret = SCE_ERROR;

	if (!out_fmap)
		return ret;
	lun = _lock_lun(lunhndl, sctrnum, nr_sctr, &flags, true);
	if (!lun)
		return ret;
	sce = (sce_t *) lun->scehndl;

	firstpg =  sctrnum                / SCE_SCTRPERPAGE;
	lastpg  = (sctrnum + nr_sctr - 1) / SCE_SCTRPERPAGE;
	fragnum = (uint32_t) (firstpg / SCE_PAGEPERFRAG);
	pgoff   = (uint32_t) (firstpg % SCE_PAGEPERFRAG);
	pgcnt   = (uint32_t) (lastpg - firstpg + 1);

	BUG_ON((pgoff + pgcnt) > SCE_PAGEPERFRAG);

	fdesc = lun->fragmaps[fragnum / MAXFRAGS4FMAP][fragnum % MAXFRAGS4FMAP];
	if (!(fdesc & FRAGDESC_MAPPED))
	{
		/* if the fragment is not mapped:
		   just increase cache miss count by 1 */
		lun->fragmaps[fragnum / MAXFRAGS4FMAP][fragnum % MAXFRAGS4FMAP]
			= FRAGDESC_INC(fdesc);
		goto unlock_and_out;
	}

	frag = _pfid2frag(sce, fdesc, out_fmap);

	/* miss-aligned request: do not write-through!! */
	if (((sctrnum % SCE_SCTRPERPAGE) != 0) ||
	    ((nr_sctr % SCE_SCTRPERPAGE) != 0)) {
		/* existing cache entries must be invalidated */
		_frag_invalidate(frag, pgoff, pgcnt);
		goto unlock_and_out;
	}

	/* additional bitmap is needed to keep status */
	if (!frag->pending_awt)
		frag->pending_awt = _awtbt_get(sce);

	if (_frag_writestart(frag, pgoff, pgcnt) != SCE_SUCCESS) {
		if ((frag->pending_awt) && (frag->nr_awt == 0)) {
			_frag_mergebitmap(frag);
			_awtbt_put(sce, frag->pending_awt);
			frag->pending_awt = NULL;
		}
		goto unlock_and_out;
	}

	frag->nr_awt++;
	frag->nr_service++;
	lun->nr_service++;

	ret = SCE_SUCCESS;

unlock_and_out:
	spin_unlock_irqrestore(&lun->lock, flags);

	return ret;
}

int sce_put4write(sce_lunhndl_t lunhndl, sector_t sctrnum, uint32_t nr_sctr,
		  int failed)
{
	lun_t        *lun;
	sce_t        *sce;
	frag_t       *frag;
	unsigned long flags;
	sector_t      firstpg;
	sector_t      lastpg;
	uint32_t      fragnum;
	fragdesc_t    fdesc;
	uint32_t      pgcnt;
	uint32_t      pgoff;
	int           ret;

	lun = _lock_lun(lunhndl, sctrnum, nr_sctr, &flags, false);
	if (!lun)
		return SCE_ERROR;

	sce     = (sce_t *) lun->scehndl;
	firstpg =  sctrnum                / SCE_SCTRPERPAGE;
	lastpg  = (sctrnum + nr_sctr - 1) / SCE_SCTRPERPAGE;
	fragnum = (uint32_t) (firstpg / SCE_PAGEPERFRAG);
	pgoff   = (uint32_t) (firstpg % SCE_PAGEPERFRAG);
	pgcnt   = (uint32_t) (lastpg - firstpg + 1);

	ret     = SCE_ERROR;
	if (((sctrnum + nr_sctr - 1) / SCE_SCTRPERFRAG) != fragnum)
		goto unlock_and_out;

	fdesc = lun->fragmaps[fragnum / MAXFRAGS4FMAP][fragnum % MAXFRAGS4FMAP];

	if (((fdesc & FRAGDESC_MAPPED) == 0) ||
	    ((fdesc & FRAGDESC_VALID ) == 0))
		goto unlock_and_out;

	frag = _pfid2frag(sce, fdesc, NULL);
	if (!frag)
		goto unlock_and_out;

	BUG_ON(!frag->pending_awt);

	if ((frag->nr_service == 0) ||
	    (frag->nr_awt     == 0) ||
	    (lun->nr_service  == 0))
		goto unlock_and_out;

	_frag_writeend(frag, pgoff, pgcnt, failed);
	frag->nr_awt--;
	if (frag->nr_awt == 0) {
		_frag_mergebitmap(frag);
		_awtbt_put(sce, frag->pending_awt);
		frag->pending_awt = NULL;
	}
	frag->nr_service--;
	lun->nr_service--;

	if (frag->nr_service == 0)
		_lun_gc(lun, frag);

	ret = SCE_SUCCESS;

unlock_and_out:
	spin_unlock_irqrestore(&lun->lock, flags);

	return ret;
}

#endif /* SCE_AWT */
