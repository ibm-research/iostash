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
/*                                Free fragment list                                        */
/* ---------------------------------------------------------------------------------------- */

/* 
    put a fragment into free fragment list
 */
int _freefraglist_put(sce_t * sce, pfid_t pfid)
{
	frag_t *frag;
	int ret = SCE_ERROR;

	if (sce) {
		frag = _pfid2frag(sce, pfid, NULL);
		if ((frag) && (frag->fragnum != FRAGNUM_WITHINFREELIST)) {
			/* free fragment mark */
			frag->fragnum = FRAGNUM_WITHINFREELIST;

			/* insert into linked list as a header node */
			frag->next = sce->freefraglist;

			/* FRAGDESC_MAPPED is added to make non-zero value */
			sce->freefraglist = pfid | FRAGDESC_MAPPED;

			/* increase number of free fragments */
			sce->nr_freefrag++;

			/* total number of free fragments cannot be bigger than 
			   total number of fragments */
			BUG_ON(sce->nr_freefrag > sce->nr_frag);

			ret = SCE_SUCCESS;
		}
	}
	return ret;
}

/* 
    get a free fragment from the free fragment list
 */
int _freefraglist_get(sce_t * sce, pfid_t * out_pfid)
{
	frag_t *frag;
	pfid_t pfid;
	int ret = SCE_ERROR;

	if ((sce) && (out_pfid)) {
		/* get the first index of the free fragment list */
		pfid = sce->freefraglist;
		if (pfid) {
			/* update the header of free fragment list */
			frag = _pfid2frag(sce, pfid, NULL);

			BUG_ON(!frag);

			sce->freefraglist = frag->next;
			frag->next = 0;
			frag->fragnum = FRAGNUM_FREE;

			/* decrease number of free fragment within list */
			BUG_ON(sce->nr_freefrag == 0);
			sce->nr_freefrag--;

			*out_pfid = pfid;

			ret = SCE_SUCCESS;
		}
	}
	return ret;
}

int _freefraglist_rmcdev(sce_t * sce, uint32_t devnum)
{
	int ret = SCE_ERROR;
	frag_t *parent;
	frag_t *child;
	pfid_t pfid;

	if (sce && (devnum < SCE_MAXCDEV)) {
		for (parent = NULL, pfid = sce->freefraglist; pfid > 0;) {
			child = _pfid2frag(sce, pfid, NULL);
			BUG_ON(!child);

			if (SCE_PFIDDEV(pfid) == devnum) {
				if (parent)
					parent->next = child->next;
				else
					sce->freefraglist = child->next;
				sce->nr_freefrag--;
			} else {
				parent = child;
			}
			pfid = child->next;
		}
		ret = SCE_SUCCESS;
	}
	return ret;
}

#ifdef SCE_AWT
void _awtbt_put(sce_t * sce, awtbt_t * abt)
{
	if (sce && abt) {
		abt->next = sce->free_awtbt;
		sce->free_awtbt = abt;
	}
}

awtbt_t *_awtbt_get(sce_t * sce)
{
	awtbt_t *abt = NULL;

	if ((sce) && (sce->free_awtbt)) {
		abt = sce->free_awtbt;
		sce->free_awtbt = abt->next;

		memset(abt, 0, sizeof(awtbt_t));
	}
	return abt;
}

#endif /* SCE_AWT  */
