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
/*                           cache population related                                       */
/* ---------------------------------------------------------------------------------------- */

int _alloc4population(lun_t * lun, uint32_t fragnum, pfid_t * out_pfid)
{
	frag_t *frag;
	sce_t *sce;
	int ret = SCE_ERROR;

	do {
		if (!out_pfid)
			break;
		if (!lun)
			break;
		if (!lun->fragmap)
			break;
		if (fragnum >= lun->nr_frag)
			break;

		sce = (sce_t *) lun->scehndl;
		if (!sce)
			break;

		do {
			/* get a free fragment */
			ret = _freefraglist_get(sce, out_pfid);
			if ((SCE_SUCCESS == ret) && (*out_pfid > 0))
				break;

			/* evict one fragment */
			ret = _evict_frag(sce);
			if (SCE_SUCCESS != ret)
				break;

			sce->nr_eviction++;

			/* repeat until fragment allocation succeeds */
		} while (1);

		if (ret == SCE_SUCCESS) {
			/* map the fragment */
			ret = _map_frag(lun, fragnum, *out_pfid);
			if (ret == SCE_SUCCESS) {
				/* to prevent LUN deletion */
				lun->nr_service++;

				/* to prevent fragment eviction */
				frag = _pfid2frag(sce, *out_pfid, NULL);
				frag->nr_service++;
			} else {
				/* for error case, return fragment */
				_freefraglist_put(sce, *out_pfid);
				*out_pfid = 0;
			}
		}
	} while (0);

	return ret;
}

int _complete_population(lun_t * lun, uint32_t fragnum)
{
	sce_t *sce;
	frag_t *frag;
	fragdesc_t fdesc;
	int ret = SCE_ERROR;

	do {
		if (!lun)
			break;
		if (!lun->fragmap)
			break;
		if (fragnum >= lun->nr_frag)
			break;

		sce = (sce_t *) lun->scehndl;
		if (!sce)
			break;

		fdesc = lun->fragmap[fragnum];
		if ((fdesc & FRAGDESC_MAPPED) == 0)
			break;

		frag = _pfid2frag(sce, fdesc, NULL);
		if (!frag)
			break;

		if ((fdesc & FRAGDESC_MAPPED) == 0)
			break;

		/* population for primary */
		if ((fdesc & FRAGDESC_VALID) == 0) {
			if (frag->next > 0)
				break;

			if (frag->nr_service != 1)
				break;	/* error! */
			lun->fragmap[fragnum] |= FRAGDESC_VALID;
			frag->nr_service = 0;
		}

		/* decrease reference count */
		lun->nr_service--;

		/* garbage collection */
		if (SCE_SUCCESS != _lun_gc(lun, frag))
			break;

		ret = SCE_SUCCESS;
	} while (0);

	return ret;
}

int _cancel_population(lun_t * lun, uint32_t fragnum)
{
	sce_t *sce;
	frag_t *frag;
	fragdesc_t fdesc;
	int ret = SCE_ERROR;

	do {
		if (!lun)
			break;
		if (!lun->fragmap)
			break;
		if (fragnum >= lun->nr_frag)
			break;

		sce = (sce_t *) lun->scehndl;
		if (!sce)
			break;

		fdesc = lun->fragmap[fragnum];
		if ((fdesc & FRAGDESC_MAPPED) == 0)
			break;

		frag = _pfid2frag(sce, fdesc, NULL);
		if (!frag)
			break;

		/* cancel for primary */
		if ((fdesc & FRAGDESC_VALID) == 0) {
			if (frag->next > 0)
				break;

			if (frag->nr_service != 1)
				break;
			frag->nr_service = 0;

			_unmap_frag(lun, fragnum);
		}
		/* cancel for secondary */
		else {
			if (frag->next == 0)
				break;

			/* save the secondary fragment id */
			fdesc = frag->next;

			/* disconnect link */
			frag->next = 0;
			frag->nr_service--;

			/* free the secondary fragment */
			_freefraglist_put(sce, fdesc);
		}

		/* decrease reference count */
		lun->nr_service--;

		/* garbage collection */
		ret = _lun_gc(lun, frag);
	} while (0);

	return ret;
}

#ifdef SCE_POPULATION_MRU
int
_choose4population(sce_t * sce, uint16_t * out_lunidx, uint32_t * out_fragnum)
{
	int i;
	int ret = SCE_ERROR;

	do {
		if (!sce)
			break;
		if (!out_lunidx)
			break;
		if (!out_fragnum)
			break;

		for (i = 0; i < SCE_CACHEMISSLOGSIZE; i++) {
			/* get an entry from cache miss log */
			ret = _misslog_get(sce, i, out_lunidx, out_fragnum);

			/* no more entry */
			if (ret != SCE_SUCCESS)
				break;

			if ((SCE_SUCCESS == _lun_isvalididx(sce, *out_lunidx))
			    && (sce->luntbl[*out_lunidx].
				fragmap[*out_fragnum] &	FRAGDESC_MAPPED) == 0)
				break;
		}
	} while (0);

	return ret;
}
#endif /* SCE_POPULATION_MRU */

#ifdef SCE_POPULATION_HOTMRU
int
_choose4population(sce_t * sce, uint16_t * out_lunidx, uint32_t * out_fragnum)
{
	int ret = SCE_ERROR;
	int i;
	uint16_t lunidx;
	uint32_t fragnum;
	pfid_t fdesc;
	uint32_t miss;
	uint32_t maxmiss;

	do {
		if (!sce)
			break;
		if (!out_lunidx)
			break;
		if (!out_fragnum)
			break;

		maxmiss = 0;
		for (i = 0; i < SCE_MISSWINDOW; i++) {
			/* get an entry from cache miss log */
			if (_misslog_get(sce, i, &lunidx, &fragnum) !=
			    SCE_SUCCESS)
				break;

			/* double check the lun is still valid */
			if (SCE_SUCCESS != _lun_isvalididx(sce, lunidx))
				continue;

			fdesc = sce->luntbl[lunidx].fragmap[fragnum];
			if ((fdesc & FRAGDESC_MAPPED) != 0)
				continue;

			miss = FRAGDESC_DATA(fdesc);

			if (miss > maxmiss) {
				maxmiss = miss;
				*out_lunidx = lunidx;
				*out_fragnum = fragnum;
				ret = SCE_SUCCESS;
			}
		}
	} while (0);

	return ret;
}
#endif /* SCE_POPULATION_HOTMRU */
