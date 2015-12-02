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
/*                                Fragment mapping related                                  */
/* ---------------------------------------------------------------------------------------- */

/* 
    create a fragment mapping 
*/
int _map_frag(lun_t * lun, uint32_t fragnum, pfid_t pfid)
{
	sce_t *sce;
	frag_t *frag;
	fragdesc_t fdesc;
	int ret = SCE_ERROR;

	do {			/* dummy do to exit on error cases without using goto */
		/* validity check */
		if (!lun)
			break;
		if (!lun->fragmaps)
			break;
		if (fragnum >= lun->nr_frag)
			break;

		sce = (sce_t *) lun->scehndl;
		if (!sce)
			break;

		if (SCE_SUCCESS != _isvalidpfid(sce, pfid))
			break;

		fdesc =
		    lun->fragmaps[fragnum / MAXFRAGS4FMAP][fragnum %
							   MAXFRAGS4FMAP];

		/* if already mapped */
		if ((fdesc & FRAGDESC_MAPPED) != 0)
			break;

		/* get fragment structure */
		frag = _pfid2frag(sce, pfid, NULL);

		/* something is wrong */
		if (!frag)
			break;

		/* initialize frag structure */
		if (SCE_SUCCESS != _frag_init(frag))
			break;

		/* for the last fragment */
		if (fragnum == (lun->nr_frag - 1)) {
			sector_t nr_sctr = lun->nr_frag * SCE_SCTRPERFRAG;
			if (lun->nr_sctr < nr_sctr) {
				uint32_t pagenum, pagecnt;

				pagecnt =
				    (uint32_t) (nr_sctr - lun->nr_sctr +
						SCE_SCTRPERPAGE - 1)
				    / SCE_SCTRPERPAGE;
				pagenum = SCE_PAGEPERFRAG - pagecnt;
				_frag_invalidate(frag, pagenum, pagecnt);
			}
		}

		/* keep */
		frag->lunidx = GET_LUNIDX(sce, lun);
		frag->fragnum = fragnum;

		/* prepare new fragment descriptor */
		fdesc = FRAGDESC_MAPPED | (pfid & FRAGDESC_DATAMASK);

		/* update mapping */
		lun->fragmaps[fragnum / MAXFRAGS4FMAP][fragnum %
						       MAXFRAGS4FMAP] = fdesc;

		ret = SCE_SUCCESS;
	} while (0);

	return ret;
}

/*
    destroy mapping and free fragment
 */
int _unmap_frag(lun_t * lun, uint32_t fragnum)
{
	sce_t *sce;
	frag_t *frag;
	fragdesc_t fdesc;
	uint32_t nr_miss;
	int ret = SCE_ERROR;

	do {			/* dummy do to exit on error cases without using goto */
		/* validity check */
		if (!lun)
			break;
		if (!lun->fragmaps)
			break;
		if (fragnum >= lun->nr_frag)
			break;

		sce = (sce_t *) lun->scehndl;
		if (!sce)
			break;

		/* get a fragment descriptor */
		fdesc =
		    lun->fragmaps[fragnum / MAXFRAGS4FMAP][fragnum %
							   MAXFRAGS4FMAP];

		/* if the fragment is mapped to a physical fragment */
		if ((fdesc & FRAGDESC_MAPPED) == 0)
			break;

		/* get fragment structure */
		frag = _pfid2frag(sce, fdesc, NULL);
		if (!frag)
			break;

		if (frag->nr_service > 0)
			break;

		/* get heat information */
		nr_miss = frag->nr_miss;

		/* put the fragment into free fragment list */
		if (SCE_SUCCESS != _freefraglist_put(sce, fdesc))
			break;

		/* update fragmap with heat information */
		lun->fragmaps[fragnum / MAXFRAGS4FMAP][fragnum %
						       MAXFRAGS4FMAP] =
		    (nr_miss & FRAGDESC_DATAMASK);

		ret = SCE_SUCCESS;
	} while (0);

	return ret;
}
