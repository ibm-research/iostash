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

#define INVALID_MISSLOG_LUN (0xffff)

/* ---------------------------------------------------------------------------------------- */
/*                                      Cache miss log                                      */
/* ---------------------------------------------------------------------------------------- */

int _misslog_put(sce_t * sce, uint16_t lunidx, uint32_t fragnum)
{
	int ret = SCE_ERROR;

	if (sce) {
		/* insert item @ head */
		sce->misslog_fragnum[sce->misslog_head] = fragnum;
		sce->misslog_lunidx[sce->misslog_head] = lunidx;

		/* advance head pointer */
		if (++sce->misslog_head == SCE_CACHEMISSLOGSIZE)
			sce->misslog_head = 0;

		/* make sure that the queue is not full */
		if (sce->misslog_size < SCE_CACHEMISSLOGSIZE) {
			/* increase queue size */
			sce->misslog_size++;
		}
		ret = SCE_SUCCESS;
	}
	return ret;
}

int
_misslog_get(sce_t * sce, uint32_t idx, uint16_t * out_lunidx,
	     uint32_t * out_fragnum)
{
	int ret = SCE_ERROR;
	uint32_t i;

	/* if there is something */
	if ((sce) && (out_lunidx) && (out_fragnum) && (idx < sce->misslog_size)) {
		/* index for the most recent miss */
		i = sce->misslog_head;

		/* when idx == 0, return the entry @ (head - 1) */
		idx++;

		/* move to the entry we are interested in */
		if (i >= idx) {
			i -= idx;
		} else {
			i = SCE_CACHEMISSLOGSIZE - (idx - i);
		}

		/* return entry */
		*out_lunidx = sce->misslog_lunidx[i];
		*out_fragnum = sce->misslog_fragnum[i];

		if (SCE_SUCCESS == _lun_isvalididx(sce, *out_lunidx)) {
			ret = SCE_SUCCESS;
		}
	}

	return ret;
}

int
_misslog_gc(sce_t * sce)
{
	int i;
	int ret = SCE_ERROR;

	if (sce) {
		for (i = 0; i < SCE_CACHEMISSLOGSIZE; i++) {
			if (SCE_SUCCESS !=
			    _lun_isvalididx(sce, sce->misslog_lunidx[i])) {
				sce->misslog_lunidx[i] = INVALID_MISSLOG_LUN;
			}
		}
		ret = SCE_SUCCESS;
	}
	return ret;
}
