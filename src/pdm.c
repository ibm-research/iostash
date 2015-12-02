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

#include <linux/delay.h>

#include "sce.h"
#include "pdm.h"

typedef struct {
	uint32_t period_ms;        /* sleeping time after a population */
	uint32_t target_missrate;  /* target miss-rate                 */
	uint32_t min_freefragper;  /* for free fragment percent        */
} pdm_cfg_t;

static pdm_cfg_t gpdmcfg[] = {
	{200, 50, 35},
	{300, 45, 30},
	{400, 40, 25},
	{500, 35, 20},

	{600, 50, 0},
	{700, 25, 0},
	{800, 13, 0},
	{5000, 0, 0},              /* always on */
};

#define PDM_NRTHREAD  (sizeof(gpdmcfg) / sizeof(pdm_cfg_t))

typedef struct {
	int id;
	pdm_hndl_t          pdmhndl;
	pdm_cfg_t          *cfg;
	bool                stopped;
	struct task_struct *thread;
} pdm_thread_t;

typedef struct {
	sce_hndl_t          scehndl;
	pdm_thread_t        threads[PDM_NRTHREAD];

	int (*read_poptask)  (sce_poptask_t * poptask, char *fragbuf);
	int (*write_poptask) (sce_poptask_t * poptask, char *fragbuf);
} pdm_t;

static int      _pdm_thread(void *arg);
static uint32_t _calc_missrate(sce_status_t *prev, sce_status_t *now);
static uint32_t _calc_freefragper(sce_status_t * st);
static void     _kill_threads(pdm_t *pdm);

pdm_hndl_t pdm_create(sce_hndl_t scehndl, popio_fn *read_fn, popio_fn *write_fn)
{
	pdm_thread_t *tctx;
	pdm_t        *pdm;
	int           i;

	pdm = NULL;
	if ((!scehndl) || (!read_fn) || (!write_fn))
		goto out;

	pdm = kmalloc(sizeof(pdm_t), GFP_KERNEL);
	if (!pdm)
		goto out;

	memset(pdm, 0, sizeof(pdm_t));

	pdm->scehndl       = scehndl;
	pdm->read_poptask  = read_fn;
	pdm->write_poptask = write_fn;

	for (i = 0; i < PDM_NRTHREAD; i++) {
		tctx          = &pdm->threads[i];
		tctx->id      = i;
		tctx->pdmhndl = (pdm_hndl_t) pdm;
		tctx->stopped = false;
		tctx->cfg     = &gpdmcfg[i];
		tctx->thread  = kthread_run(_pdm_thread, tctx, "pdm");

		if (!tctx->thread)
		{
			_kill_threads(pdm);
			kfree(pdm);
			pdm = NULL;
			goto out;
		}
	}

out:
	return (pdm_hndl_t)pdm;
}

int pdm_destroy(pdm_hndl_t pdmhndl)
{
	pdm_t *pdm;

	if (!pdmhndl)
		return SCE_ERROR;

	pdm = (pdm_t *)pdmhndl;

	_kill_threads(pdm);
	kfree(pdm);

	return SCE_SUCCESS;
}

/* PDM thread: does cache population asynchronously in background */
static int _pdm_thread(void *arg)
{
	sce_poptask_t poptask;
	sce_status_t  cur_st;
	sce_status_t  pri_st;
	int           popcnt;
	pdm_thread_t *pctx;
	char         *fragbuf;
	pdm_t        *pdm;
	pdm_cfg_t    *cfg;

	if (!arg)
		goto out;

	pctx = (pdm_thread_t *)arg;
	memset(&cur_st, 0, sizeof(pri_st));

	fragbuf = (char *)vmalloc(SCE_FRAGSIZE);
	if (!fragbuf)
		goto out;

	popcnt = 0;

	pdm = (pdm_t *) pctx->pdmhndl;
	cfg = (pdm_cfg_t *) pctx->cfg;
	while (!kthread_should_stop()) {
		pri_st = cur_st;

		if (sce_get_status(pdm->scehndl, &cur_st) != SCE_SUCCESS)
			goto skip;

		if (_calc_freefragper(&cur_st) < cfg->min_freefragper)
			goto skip;

		if (_calc_missrate(&pri_st, &cur_st) < cfg->target_missrate)
			goto skip;

		if (sce_get4pop(pdm->scehndl, &poptask) != SCE_SUCCESS)
			goto skip;

		if ((pdm->read_poptask) (&poptask, fragbuf) != SCE_SUCCESS)
			goto skip;

		if ((pdm->write_poptask) (&poptask, fragbuf) == SCE_SUCCESS) {
			sce_put4pop(pdm->scehndl, &poptask, 0);
		} else {
			sce_put4pop(pdm->scehndl, &poptask, 1);
		}
skip:
		if (++popcnt >= SCE_POPCNT) {
			msleep(cfg->period_ms);
			popcnt = 0;
		}
	}

	vfree(fragbuf);
out:
	return 0;
}

static uint32_t _calc_missrate(sce_status_t *prev, sce_status_t *now)
{
	uint32_t missrate;
	uint32_t hitcnt;
	uint32_t misscnt;
	uint32_t total;

	hitcnt   = (now->nr_hit > prev->nr_hit) ?
		(now->nr_hit - prev->nr_hit) : now->nr_hit;
	misscnt  = (now->nr_miss > prev->nr_miss) ?
		(now->nr_miss - prev->nr_miss) : now->nr_miss;
	total    = hitcnt + misscnt;
	missrate = (total) ? (misscnt * 100) / total : 0;

	return missrate;
}

static uint32_t _calc_freefragper(sce_status_t *st)
{
	uint32_t per = 0;

	if (st->nr_totfrag) {
		per = st->nr_freefrag * 100 / st->nr_totfrag;
	}

	return per;
}

static void _kill_threads(pdm_t *pdm)
{
	pdm_thread_t *tctx;
	int           i;

	/* kill threads */
	for (i = 0; i < PDM_NRTHREAD; i++) {
		tctx = &pdm->threads[i];

		if (tctx->thread) {
			tctx->stopped = true;
			kthread_stop(tctx->thread);
			tctx->thread = NULL;
		}
	}
}
