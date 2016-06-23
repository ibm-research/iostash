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
static struct iostash_bio *_io_alloc(struct hdd_info * hdd, struct ssd_info * ssd, uint32_t fragnum,
				    struct bio *bio, sector_t psn);
static int _clone_init(struct iostash_bio *io, struct bio *clone, int is4ssd,
		       void *endiofn);
static void _inc_pending(struct iostash_bio *io);
static void _dec_pending(struct iostash_bio *io);
#if KERNEL_VERSION(4,4,0) <= LINUX_VERSION_CODE
#include <linux/blkdev.h>
static blk_qc_t _io_worker_run(struct work_struct *work);
#endif
static void _io_worker(struct work_struct *work);
static void _io_queue(struct iostash_bio *io);

#if KERNEL_VERSION(4,2,0) <= LINUX_VERSION_CODE
static void _endio4read(struct bio *clone)
#else
static void _endio4read(struct bio *clone, int error)
#endif
{
	struct iostash_bio *io = clone->bi_private;
	struct hdd_info *hdd = io->hdd;
	int ssd_online_to_be = 0;
	DBG("Got end_io (%lu) %p s=%lu l=%u base_bio=%p base_bio s=%lu l=%u.",
		clone->bi_rw, clone, BIO_SECTOR(clone), bio_sectors(clone), io->base_bio,
		BIO_SECTOR(io->base_bio), bio_sectors(io->base_bio));

	do {
#if KERNEL_VERSION(4,2,0) <= LINUX_VERSION_CODE
		const int error = clone->bi_error;
#else
		if (unlikely(!bio_flagged(clone, BIO_UPTODATE) && !error))
		{
			ERR("cloned bio not UPTODATE.");
			error = -EIO;
			ssd_online_to_be = 1;	/* because this error does not mean SSD failure */
		}
#endif
		io->error = error;

		/* if this bio is for SSD: common case */
		if (clone->bi_bdev != io->base_bio->bi_bdev) {
			DBG("SSD cloned bio endio.");
			if (unlikely(error)) {	/* Error handling */
				ERR("iostash: SSD read error: error = %d, sctr = %ld :::\n",
				     error, io->psn);
				io->ssd->online = ssd_online_to_be;

				_inc_pending(io);	/* to prevent io from releasing */
				_io_queue(io);
				break;
			}

			sce_put4read(hdd->lun, io->psn, io->nr_sctr);
			gctx.st_cread++;
			break;
		}
		DBG("iostash: Retried HDD read return = %d, sctr = %ld :::\n",
		       error, io->psn);
		_dec_pending(io);

	} while (0);

	bio_put(clone);
	_dec_pending(io);
}

#if KERNEL_VERSION(4,2,0) <= LINUX_VERSION_CODE
static void _endio4write(struct bio *clone)
#else
static void _endio4write(struct bio *clone, int error)
#endif
{
	struct iostash_bio *io = clone->bi_private;
#if KERNEL_VERSION(4,2,0) <= LINUX_VERSION_CODE
	const int error = clone->bi_error;
#else
	if (unlikely(!bio_flagged(clone, BIO_UPTODATE) && !error))
		error = -EIO;
#endif
	if (unlikely(error)) {
		if (clone->bi_bdev != io->base_bio->bi_bdev) {
			ERR("iostash: SSD write error: error = %d, sctr = %ld :::\n",
			     error, io->psn);
			io->ssd_werr = error;
		} else {
			ERR("iostash: HDD write error: error = %d, sctr = %ld :::\n",
			     error, io->psn);
			io->error = error;
		}
	}
	bio_put(clone);
	_dec_pending(io);
}

static struct iostash_bio *_io_alloc(struct hdd_info * hdd, struct ssd_info * ssd, uint32_t fragnum,
				    struct bio *bio, sector_t psn)
{
	struct iostash_bio *io = mempool_alloc(hdd->io_pool, GFP_NOIO);

	if (io) {
		atomic_inc(&hdd->io_pending);

		io->hdd = hdd;
		io->ssd = ssd;
		io->fragnum = fragnum;
		io->base_bio = bio;
		io->psn = psn;
		io->nr_sctr = to_sector(BIO_SIZE(bio));
		io->error = 0;
		io->ssd_werr = 0;	/* SSD write error */
		atomic_set(&io->io_pending, 0);
	}
	return io;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)
static void _clone_destructor(struct bio *const bio)
{
	struct iostash_bio *const io = bio->bi_private;
	struct hdd_info *const hdd = io->hdd;
	bio_free(bio, hdd->bs);
}
#endif

static int
_clone_init(struct iostash_bio *io, struct bio *clone, int is4ssd,
	    void *endiofn)
{
	int ret = 0;

	clone->bi_private = io;
	clone->bi_end_io = endiofn;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)
	clone->bi_destructor = _clone_destructor;
#endif

	if (is4ssd) {
		if (io->ssd->online) {
			clone->bi_bdev = io->ssd->bdev;
			BIO_SECTOR(clone) =
			    (sector_t) (io->fragnum * SCE_SCTRPERFRAG) + 
			    (io->psn % SCE_SCTRPERFRAG) + IOSTASH_HEADERSCT;
			BIO_SIZE(clone) = BIO_SIZE(io->base_bio);
		} else {
			ret = -1;
		}
	}

	return ret;
}

static void _inc_pending(struct iostash_bio *io)
{
	atomic_inc(&io->io_pending);
}

static void _dec_pending(struct iostash_bio *io)
{
	if (atomic_dec_and_test(&io->io_pending)) {
		struct hdd_info *hdd = io->hdd;
		struct ssd_info *ssd = io->ssd;
		struct bio *base_bio = io->base_bio;
		int error = io->error;

#ifdef SCE_AWT
		if (bio_data_dir(base_bio) != READ) {
			sce_put4write(hdd->lun, io->psn,
				io->nr_sctr, io->ssd_werr | io->error);
			gctx.st_awt++;
		}
#endif
		mempool_free(io, hdd->io_pool);
#if KERNEL_VERSION(4,2,0) <= LINUX_VERSION_CODE
		(void) error;
		bio_endio(base_bio);
#else
		bio_endio(base_bio, error);
#endif
		atomic_dec(&hdd->io_pending);
		BUG_ON(NULL == ssd);
		atomic_dec(&ssd->nr_ref);
	}
}

#if KERNEL_VERSION(4,4,0) <= LINUX_VERSION_CODE
static void _io_worker(struct work_struct *work)
{
	_io_worker_run(work);
}

static blk_qc_t _io_worker_run(struct work_struct *work)
#else
static void _io_worker(struct work_struct *work)
#endif
{
	struct iostash_bio *io = container_of(work, struct iostash_bio, work);
	struct hdd_info *hdd = io->hdd;
	struct bio *base_bio = io->base_bio;
	struct bio *clone4ssd;
	struct bio *clone4hdd;
	void *hddendfunc;
#if KERNEL_VERSION(4,4,0) <= LINUX_VERSION_CODE
	blk_qc_t ret = BLK_QC_T_NONE;
#endif
	_inc_pending(io);
	do {
		if (bio_data_dir(base_bio) == READ) {	/* Read handling */
			/* First trial */
			if (io->error == 0) {
				clone4ssd = BIO_CLONEBS(base_bio, hdd->bs);

				if (!clone4ssd) {
					io->error = -ENOMEM;
					break;
				}

				/* _clone_init() may fail when SSD became offline */
				if (_clone_init(io, clone4ssd, 1, _endio4read) == 0) {
					_inc_pending(io);
#if KERNEL_VERSION(4,4,0) <= LINUX_VERSION_CODE
					ret = generic_make_request(clone4ssd);
#else
					generic_make_request(clone4ssd);
#endif
					break;
				}

				/* when bio cannot be initialized for SSD for some reason flow to HDD */
				bio_put(clone4ssd);
				sce_put4read(hdd->lun, io->psn, io->nr_sctr);
			}
			hddendfunc = _endio4read;
		} else {	/* Write handling */

			hddendfunc = _endio4write;

			/* create a request to SSD */
			clone4ssd = BIO_CLONEBS(base_bio, hdd->bs);
			if (clone4ssd) {
				if (_clone_init(io, clone4ssd, 1, _endio4write)
				    == 0) {
					_inc_pending(io);
#if KERNEL_VERSION(4,4,0) <= LINUX_VERSION_CODE
					ret = generic_make_request(clone4ssd);
#else
					generic_make_request(clone4ssd);
#endif
				}
			}
		}

		/* I/O handling for HDD */
		clone4hdd = BIO_CLONEBS(base_bio, hdd->bs);
		if (!clone4hdd) {
			io->error = -ENOMEM;
			break;
		}

		/* clone_init() will never fail for HDD */
		_clone_init(io, clone4hdd, 0, hddendfunc);

		/* Call HDD */
		_inc_pending(io);
#if KERNEL_VERSION(4,4,0) <= LINUX_VERSION_CODE
		ret = (*hdd->org_mapreq) (hdd->request_q, clone4hdd);
#else
		(*hdd->org_mapreq) (hdd->request_q, clone4hdd);
#endif
	} while (0);

	_dec_pending(io);
#if KERNEL_VERSION(4,4,0) <= LINUX_VERSION_CODE
	return ret;
#endif
}

static void _io_queue(struct iostash_bio *io)
{
	struct hdd_info *hdd = io->hdd;

	INIT_WORK(&io->work, _io_worker);
	queue_work(hdd->io_queue, &io->work);
}


#if KERNEL_VERSION(4,4,0) <= LINUX_VERSION_CODE
blk_qc_t iostash_mkrequest(struct request_queue *q, struct bio *bio)
#elif LINUX_VERSION_CODE <= KERNEL_VERSION(3,1,0)
int iostash_mkrequest(struct request_queue *q, struct bio *bio)
#else
void iostash_mkrequest(struct request_queue *q, struct bio *bio)
#endif
{
	struct hdd_info *hdd;
	struct ssd_info *ssd;
	struct iostash_bio *io;
        sce_fmap_t fmap;
	uint32_t nr_sctr;
	sector_t psn;
	make_request_fn *org_mapreq = NULL;
#if KERNEL_VERSION(4,4,0) <= LINUX_VERSION_CODE
	blk_qc_t ret = BLK_QC_T_NONE;
#endif

	DBG("Got bio=%p bio->bi_rw(%lu) request at s=%lu l=%u.\n",
		bio, bio->bi_rw, BIO_SECTOR(bio), bio_sectors(bio));

	rcu_read_lock();
	hdd = hdd_search(bio);
	if (hdd) {
		atomic_inc(&hdd->nr_ref);
		org_mapreq = hdd->org_mapreq;
	}
	rcu_read_unlock();

	if (unlikely(NULL == hdd)) {
		/* have to requeue the request, somebody was holding a
		 * dangling reference */
		ERR("Request holding a dangling make_request_fn pointer\n.");

#if KERNEL_VERSION(4,4,0) <= LINUX_VERSION_CODE
		bio->bi_error = -EAGAIN;
		return ret;
#elif LINUX_VERSION_CODE <= KERNEL_VERSION(3,1,0)
		rmb();		/* read the change in make_request_fn */
		return -EAGAIN; /* retry */
#else
		/* no retry possible in newer kernels since the return
		 * of make_request_fn is no longer checked and retried
		 * if not zero, we cannot unload the module */
		BUG();
		return;
#endif
	}

	if (!hdd->online) {
		ERR("request re-routed due to hdd not being online.\n");
		/* being unloaded, re-route */
		goto out;
	}

	hdd->request_q = q;
	/* calculate physical sector number -- offset partition information */
	psn = BIO_SECTOR(bio) + bio->bi_bdev->bd_part->start_sect;
	nr_sctr = to_sector(BIO_SIZE(bio));
	do {
		if (bio_sectors(bio) == 0)
			break;

		/* partition boundary check */
		if ((psn < hdd->part_start) ||
			((psn + nr_sctr) > hdd->part_end))
			break;

		if (bio_data_dir(bio) == WRITE) {
			gctx.st_write++;

#ifdef SCE_AWT
			/* make sure the request is only for one fragment */
			if (((psn + nr_sctr - 1) / SCE_SCTRPERFRAG) !=
				(psn / SCE_SCTRPERFRAG)) {
				sce_invalidate(hdd->lun, psn, nr_sctr);
				break;
			}
			rcu_read_lock();
			if (sce_get4write(hdd->lun, psn, nr_sctr, &fmap) 
				== SCE_SUCCESS) {
				ssd = (struct ssd_info *)fmap.cdevctx;
				atomic_inc(&ssd->nr_ref);
				rcu_read_unlock();
				if (!ssd->online) {
					sce_put4write(hdd->lun, psn,
						nr_sctr, 1);
					atomic_dec(&ssd->nr_ref);
				} else {
					io = _io_alloc(hdd, ssd, fmap.fragnum, bio, psn);
					if (NULL == io) {
						atomic_dec(&ssd->nr_ref);
						break;
					}
#if KERNEL_VERSION(4,4,0) <= LINUX_VERSION_CODE
					ret = _io_worker_run(&io->work);
#else
					_io_queue(io);
#endif
					/* lose the reference to hdd, not needed anymore */
					atomic_dec(&hdd->nr_ref);
#if KERNEL_VERSION(4,4,0) <= LINUX_VERSION_CODE
					return ret;
#elif LINUX_VERSION_CODE <= KERNEL_VERSION(3,1,0)
					return 0;
#else
					return;
#endif
				}
			} else
				rcu_read_unlock();
#else
			sce_invalidate(hdd->lun, psn, nr_sctr);
#endif
			break;
		}
		else
		{
			/* Read handling */
			gctx.st_read++;

			/* make sure the request is only for one fragment */
			if (((psn + nr_sctr - 1) / SCE_SCTRPERFRAG) !=
				(psn / SCE_SCTRPERFRAG))
				break;

			/* cache hit/miss check */
			rcu_read_lock();
			if (sce_get4read(hdd->lun, psn, nr_sctr, &fmap) != SCE_SUCCESS) {
				rcu_read_unlock();
				break;
			}
			BUG_ON(NULL == fmap.cdevctx);
			ssd = (struct ssd_info *) fmap.cdevctx;
			atomic_inc(&ssd->nr_ref);
			rcu_read_unlock();
			/* make sure the request is within the SSD limits and the SSD is online */
			if (!ssd->online || ssd->queue_max_hw_sectors < nr_sctr) {
				sce_put4read(hdd->lun, psn, nr_sctr);
				atomic_dec(&ssd->nr_ref);
				break;
			}

			/* cache hit */
			io = _io_alloc(hdd, ssd, fmap.fragnum, bio, psn);
			if (NULL == io) {
				atomic_dec(&ssd->nr_ref);
				break;
			}

#if KERNEL_VERSION(4,4,0) <= LINUX_VERSION_CODE
			ret = _io_worker_run(&io->work);
#else
			_io_queue(io);
#endif
			/* lose the reference to hdd , not needed anymore */
			atomic_dec(&hdd->nr_ref);
		}

#if KERNEL_VERSION(4,4,0) <= LINUX_VERSION_CODE
		return ret;
#elif LINUX_VERSION_CODE <= KERNEL_VERSION(3,1,0)
		return 0;
#else
		return;
#endif
	} while (0);

out:
	/* lose the reference to hdd , not needed anymore */
	atomic_dec(&hdd->nr_ref);

	return (org_mapreq) (q, bio);
}
