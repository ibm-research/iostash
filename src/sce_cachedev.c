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
/*                            Cache device (SSD) related                                    */
/* ---------------------------------------------------------------------------------------- */

/*
    _cdev_destroy(cdev_t* dev)
        : free fragment descriptor table
 */
int _cdev_destroy(cdev_t *cdev)
{
	int i;

	if (!cdev)
		return SCE_ERROR;

	if (cdev->fragtbls) {
		for (i = 0; i < (int)cdev->nr_fragtbl; i++) {
			if (cdev->fragtbls[i])
				kfree(cdev->fragtbls[i]);
		}
		kfree(cdev->fragtbls);
	}
	memset(cdev, 0, sizeof(cdev_t));

	return SCE_SUCCESS;
}

int _cdev_search(sce_t * sce, void* cdevctx)
{
	cdev_t* cdev;
	int     i;

	cdev = sce->cdevtbl;
	for (i = 0; i < SCE_MAXCDEV; i++, cdev++) {
		if (cdev->cdevctx == cdevctx) return i;
	}

	return -1;
}

/*
    allocate and initialize cache device structure
 */
int _cdev_init(sce_t * sce, cdev_t * cdev, sector_t nr_sctr)
{
	uint32_t nr_frag;
	uint32_t i, n;

	if ((!sce) || (!cdev) || (nr_sctr < SCE_SCTRPERFRAG))
		return SCE_ERROR;

	/* clear memory */
	memset(cdev, 0, sizeof(cdev_t));

	/* set cdev members */
	cdev->scehndl = (sce_hndl_t) sce;

	/* calculate number of fragments */
	cdev->nr_frag = nr_frag = (uint32_t)(nr_sctr / SCE_SCTRPERFRAG);

	/* Because of max-memory allocation limitation, 
	 * we have to separate fragment table into multiple pieces */
	cdev->nr_fragtbl = (nr_frag + MAXFRAGS4FTBL - 1) / MAXFRAGS4FTBL;

	cdev->fragtbls = (frag_t **)
		kmalloc(sizeof(frag_t*) * cdev->nr_fragtbl, (GFP_KERNEL & ~__GFP_WAIT));

	if (!cdev->fragtbls)
		goto error_out;

	memset(cdev->fragtbls, 0, sizeof(frag_t *) * cdev->nr_fragtbl);

	for (i = 0; i < cdev->nr_fragtbl; i++) {
		n = (nr_frag > MAXFRAGS4FTBL) ? MAXFRAGS4FTBL : nr_frag;

		cdev->fragtbls[i] =
			(frag_t *)kmalloc(n * sizeof(frag_t), (GFP_KERNEL & ~__GFP_WAIT));

		if (NULL == cdev->fragtbls[i])
			goto error_out;

		memset(cdev->fragtbls[i], 0, n * sizeof(frag_t));

		nr_frag -= n;
	}
	return SCE_SUCCESS;

error_out:
	_cdev_destroy(cdev);
	return SCE_ERROR;
}
