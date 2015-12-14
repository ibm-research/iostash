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

	if ((!sce) || (!cdev) || (nr_sctr < SCE_SCTRPERFRAG))
		return SCE_ERROR;

	/* clear memory */
	memset(cdev, 0, sizeof(cdev_t));

	/* set cdev members */
	cdev->scehndl = (sce_hndl_t) sce;

	/* calculate number of fragments */
	cdev->nr_frag = nr_frag = (uint32_t)(nr_sctr / SCE_SCTRPERFRAG);

	cdev->fragtbl = (frag_t *) vmalloc(sizeof(frag_t) * cdev->nr_frag);
	if (!cdev->fragtbl)
		goto error_out;

	/* Initializing to 01010101's */
	memset(cdev->fragtbl, 85, sizeof(frag_t) * cdev->nr_frag);

	return SCE_SUCCESS;

error_out:
	_cdev_destroy(cdev);
	return SCE_ERROR;
}

/*
    _cdev_destroy(cdev_t* dev)
        : free fragment descriptor table
 */
int _cdev_destroy(cdev_t *cdev)
{
	if (!cdev)
		return SCE_ERROR;

	if (cdev->fragtbl) {
		vfree(cdev->fragtbl);
	}

	memset(cdev, 0, sizeof(cdev_t));

	return SCE_SUCCESS;
}
