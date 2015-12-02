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

#include "iostash.h"
#include "helpers.h"

/* Perform read and write I/O synchronously: used by PDM */
static int
_do_rw(struct block_device *bdev, int rw, sector_t lsn, uint32_t sctcnt,
       void *data)
{
	struct dm_io_request iorq;
	struct dm_io_region where;
	unsigned long bits;
	int ret = -1;

	if (bdev && gctx.io_client) {
		where.bdev = bdev;
		where.sector = lsn;
		where.count = sctcnt;

		iorq.bi_rw = rw;
		iorq.mem.type = DM_IO_VMA;
		iorq.mem.ptr.vma = data;
		iorq.notify.fn = NULL;
		iorq.client = gctx.io_client;

		ret = dm_io(&iorq, 1, &where, &bits);
	}

	return ret;
}

int poptask_read(sce_poptask_t * poptask, char *fragbuf)
{
	struct hdd_info *hdd;
	sector_t sctrnum;
	uint32_t nr_sctr;
	int ret;

	hdd = (struct hdd_info *) poptask->lunctx;
	sctrnum = (poptask->lun_fragnum * SCE_SCTRPERFRAG);
	nr_sctr = ((sctrnum + SCE_SCTRPERFRAG) >= hdd->nr_sctr) ?
	    hdd->nr_sctr - sctrnum : SCE_SCTRPERFRAG;
	DBG("start read from hdd frag s=%lu len=%u", sctrnum, nr_sctr);
	ret = _do_rw(hdd->bdev->bd_contains, READ, sctrnum, nr_sctr, fragbuf);
	DBG("Finished read from hdd frag s=%lu len=%u", sctrnum, nr_sctr);

	if (ret)
		printk("poptask_read() returns %d", ret);

	return (ret) ? SCE_ERROR : SCE_SUCCESS;
}

int poptask_write(sce_poptask_t * poptask, char *fragbuf)
{
	struct ssd_info *ssd;
	sector_t sctrnum;
	int ret = -1;

	/* increase statistics value */
	gctx.st_population++;
	rcu_read_lock();
	ssd = (struct ssd_info *) poptask->cdevctx;
	sctrnum = poptask->cdev_fragnum * SCE_SCTRPERFRAG + IOSTASH_HEADERSCT;
	if (ssd->cdev && ssd->bdev && ssd->online) {
		atomic_inc(&ssd->nr_ref);
		rcu_read_unlock();
		DBG("start write to ssd frag s=%lu len=%u", sctrnum, SCE_SCTRPERFRAG);
		ret =
		    (_do_rw
		     (ssd->bdev, WRITE, sctrnum, SCE_SCTRPERFRAG,
		      fragbuf)) ? SCE_ERROR : SCE_SUCCESS;
		DBG("finished write to ssd frag s=%lu len=%u", sctrnum, SCE_SCTRPERFRAG);
		atomic_dec(&ssd->nr_ref);
	} else {
		rcu_read_unlock();
	}

	return ret;
}
