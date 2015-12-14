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
/*                             Cache replacement algorithm                                  */
/* ---------------------------------------------------------------------------------------- */

/*
    move clock's arm, and return current current frag
 */
frag_t *_get_frag_advance_clockarm(sce_t * sce)
{
	frag_t *frag = NULL;
	cdev_t *cdev;
	pfid_t devnum;
	pfid_t fragnum;

	if ((sce) && (sce->nr_cdev > 0)) {
		/* device number */
		devnum = SCE_PFIDDEV(sce->arm) % sce->nr_cdev;

		/* fragment number */
		fragnum = SCE_PFIDOFF(sce->arm);

		/* device pointer */
		cdev = &sce->cdevtbl[devnum];
		if (cdev->nr_frag > 0) {
			fragnum %= cdev->nr_frag;

			/* get fragment */
			frag = &cdev->fragtbl[fragnum];

			/* move arm */
			if (++fragnum >= cdev->nr_frag) {
				/* move to next device */
				fragnum = 0;
				devnum++;
			}
		} else {
			devnum++;
		}

		/* update arm value */
		sce->arm = SCE_PFID(devnum, fragnum);
	}
	return frag;
}

/*
    evict a fragment 
    (CLOCK algorithm with reference counter)
 */
int _evict_frag(sce_t * sce)
{
	frag_t *frag;
	int i;
	int ret = SCE_ERROR;

	if ((sce) && (sce->nr_freefrag < sce->nr_frag)) {
		for (i = sce->nr_frag * SCE_REFCEILING; i > 0; i--) {
			/* get a fragment pointer pointed by current arm pointer, and move arm one step */
			frag = _get_frag_advance_clockarm(sce);

			/* error situation */
			if (!frag)
				break;

			/* if this fragment is being used */
			if ((frag->fragnum < FRAGNUM_FREE) &&
			    (frag->nr_service == 0)) {
				/* is this fragment evictable? */
				if (frag->nr_ref == 0) {
					/* let's evict! */
					_unmap_frag(&sce->luntbl[frag->lunidx],
						    frag->fragnum);
					ret = SCE_SUCCESS;
					break;
				} else {
					/* decrease reference count */
					frag->nr_ref--;
				}
			}
		}
	}
	return ret;
}

