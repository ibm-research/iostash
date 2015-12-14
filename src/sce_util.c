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
/*                             Utility functions                                            */
/* ---------------------------------------------------------------------------------------- */

/*
    get fragment structure from pfid
 */
frag_t *_pfid2frag(sce_t * sce, pfid_t pfid, sce_fmap_t* out_fmap)
{
	uint32_t devnum;
	uint32_t fragnum;
	frag_t *frag = NULL;
	cdev_t *cdev;

	devnum = SCE_PFIDDEV(pfid);
	fragnum = SCE_PFIDOFF(pfid);

	if ((sce) && (devnum < SCE_MAXCDEV)) {
		/* get cache device structure */
		cdev = &sce->cdevtbl[devnum];
		if (cdev) {
			/* get fragment structure */
			if ((cdev->fragtbl) && (fragnum < cdev->nr_frag)) {
				frag = &cdev->fragtbl[fragnum];
				if (out_fmap) {
					out_fmap->cdevctx = cdev->cdevctx;
					out_fmap->fragnum = fragnum;
				}
			}
		}
	}
	return frag;
}

/* 
    Return SCE_ERROR when given pfid is not valid
 */
int _isvalidpfid(sce_t * sce, pfid_t pfid)
{
	uint32_t devnum;
	uint32_t fragnum;
	int ret = SCE_ERROR;

	devnum = SCE_PFIDDEV(pfid);
	fragnum = SCE_PFIDOFF(pfid);

	if ((sce) &&
	    (devnum < SCE_MAXCDEV) &&
	    (sce->cdevtbl[devnum].fragtbl) &&
	    (sce->cdevtbl[devnum].nr_frag > fragnum)) {
		ret = SCE_SUCCESS;
	}
	return ret;
}
