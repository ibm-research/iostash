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
/*                                  LUN related                                             */
/* ---------------------------------------------------------------------------------------- */
/*
    free fragment mapping table
 */
int _lun_destroy(lun_t * lun)
{
	int err = 0;

	if (!lun)
	{
		err = SCE_ERROR;
		goto out;
	}

	vfree(lun->fragmap);

	lun->scehndl = NULL;
	lun->fragmap = NULL;
	lun->lunctx = NULL;
	lun->nr_sctr = 0;
	lun->nr_frag = 0;
	lun->nr_service = 0;
	lun->waiting4deletion = 0;

out:
	return err;
}


int _lun_search(sce_t * sce, void* lunctx)
{
	lun_t* lun;
	int    i;

	lun = sce->luntbl;
	for (i = 0; i < SCE_MAXLUN; i++, lun++) {
		if (lun->lunctx == lunctx) return i;
	}
	return -1;
}

/*
    allocate memory for fragment mapping table, set LUN size.
 */
int _lun_init(sce_t * sce, lun_t * lun, sector_t nr_sctr)
{
	uint32_t nr_frag;
	int err = 0;

	if ((sce) && (lun) && (lun->fragmap == NULL) && (nr_sctr > 0)) {
		/* set sce handle */
		lun->scehndl = (sce_hndl_t) sce;

		spin_lock_init(&lun->lock);

		/* set LUN size */
		lun->nr_sctr = nr_sctr;
		lun->nr_frag = nr_frag =
				(uint32_t) ((nr_sctr + SCE_SCTRPERFRAG - 1) / SCE_SCTRPERFRAG);

		/* allocate memory for fragmap */
		lun->fragmap = vmalloc(lun->nr_frag * sizeof(fragdesc_t));
		if (!lun->fragmap)
		{
			err = ENOMEM;
			goto out;
		}
		/* Initializing to 01010101's */
		memset(lun->fragmap, 85, sizeof(fragdesc_t) * lun->nr_frag);
	}

out:
	return err;
}

/*
    if this lun cannot service for any reasons, return FALSE
 */
int _lun_is_serviceable(lun_t * lun)
{
	BUG_ON(!lun);

	return (lun->waiting4deletion) ? SCE_ERROR : SCE_SUCCESS;
}

/*
    _lun_purge(lun_t* lun)
        : remove and destroy lun structure

 */
int _lun_purge(lun_t * lun)
{
	uint32_t fragnum;
	sce_t *sce;
	int ret = SCE_ERROR;

	if ((lun) && (lun->nr_service == 0)) {
		sce = (sce_t *) lun->scehndl;

		if ((sce) && (sce->nr_lun)) {
			/* release fragments */
			for (fragnum = 0; fragnum < lun->nr_frag; fragnum++) {
				_unmap_frag(lun, fragnum);
			}

			/* free memory allocated for LUN */
			ret = _lun_destroy(lun);

			/* decrease nr_lun */
			if (ret == SCE_SUCCESS) {
				sce->nr_lun--;
			}
		}
	}
	return ret;
}

/*
    clean-up, for unused resource
 */
int _lun_gc(lun_t * lun, frag_t * frag)
{
	int ret = SCE_ERROR;

	if ((lun) && (frag)) {
		/* if a fragment has no more valid page, free it */
		if ((frag->nr_service == 0) && (frag->nr_valid == 0)) {
			_unmap_frag(lun, frag->fragnum);
		}

		/* if LUN is pending for deletion and LUN has just finished servicing */
		if ((lun->waiting4deletion) && (lun->nr_service == 0)) {
			_lun_purge(lun);
		}

		ret = SCE_SUCCESS;
	}
	return ret;
}

/*
    check wheather the given lun index is valid or not
 */
int _lun_isvalididx(sce_t * sce, uint16_t lunidx)
{
	lun_t *lun;
	int ret = SCE_ERROR;

	if ((sce) && (lunidx < SCE_MAXLUN)) {
		lun = &sce->luntbl[lunidx];

		if ((lun->fragmap) && (!lun->waiting4deletion)) {
			ret = SCE_SUCCESS;
		}
	}
	return ret;
}

/*
    when cache device is removed... 
 */
int _lun_rmcdev(lun_t * lun, uint32_t devnum)
{
	fragdesc_t fdesc;
	uint32_t fragnum;
	frag_t *frag;
	uint32_t nr_miss;
	int ret = SCE_ERROR;

	if ((lun) && (lun->fragmap) && (devnum < SCE_MAXCDEV)) {
		for (fragnum = 0; fragnum < lun->nr_frag; fragnum++) {
			fdesc = lun->fragmap[fragnum];

			if ((fdesc & FRAGDESC_MAPPED) == 0)
				continue;
			if (devnum != SCE_PFIDDEV(fdesc))
				continue;

			frag = _pfid2frag((sce_t *) lun->scehndl, fdesc, NULL);
			if (!frag)
				continue;

			nr_miss = frag->nr_miss;
			lun->fragmap[fragnum] = (nr_miss & FRAGDESC_DATAMASK);

		}
		ret = SCE_SUCCESS;
	}
	return ret;
}
