#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "CUnit/Basic.h"

#include "sce.h"
#include "sce_internal.h"

sce_t *gsce;
lun_t *glun;

static int _init_suite(void)
{
	gsce = (sce_t *)sce_create();
	if (!gsce)
		return -1;

	if (!sce_addcdev((sce_hndl_t)gsce, 567 * SCE_SCTRPERFRAG, (void*)123)) {
		sce_destroy(gsce);
		return -1;
	}
	glun = &gsce->luntbl[0];
	return 0;
}

static int _clean_suite(void)
{
	sce_destroy(gsce);
	return 0;
}

static void test_lun_init(void)
{
	int ret;

	ret = _lun_init(gsce, glun, 0);
	CU_ASSERT(ret != SCE_SUCCESS);

	ret = _lun_init(NULL, glun, SCE_SCTRPERFRAG * 123
					+ SCE_SCTRPERFRAG / 2);
	CU_ASSERT(ret != SCE_SUCCESS);

	ret = _lun_init(gsce, NULL, SCE_SCTRPERFRAG * 123
					+ SCE_SCTRPERFRAG / 2);
	CU_ASSERT(ret != SCE_SUCCESS);

	ret = _lun_init(gsce, glun, SCE_SCTRPERFRAG * 123
					+ SCE_SCTRPERFRAG / 2);
	CU_ASSERT(ret == SCE_SUCCESS);

	CU_ASSERT(glun->scehndl == (sce_hndl_t) gsce);
	CU_ASSERT(glun->fragmaps != NULL);
	CU_ASSERT(glun->nr_sctr == (SCE_SCTRPERFRAG * 123
					+ SCE_SCTRPERFRAG / 2));
	CU_ASSERT(glun->nr_frag == 124);
	CU_ASSERT(glun->nr_service == 0);
	CU_ASSERT(glun->waiting4deletion == 0);
}

static void test_lun_destroy(void)
{
	int ret;

	ret = _lun_destroy(NULL);
	CU_ASSERT(ret != SCE_SUCCESS);

	ret = _lun_destroy(glun);
	CU_ASSERT(ret == SCE_SUCCESS);

	CU_ASSERT(glun->scehndl == NULL);
	CU_ASSERT(glun->fragmaps == NULL);
	CU_ASSERT(glun->nr_sctr == 0);
	CU_ASSERT(glun->nr_frag == 0);
	CU_ASSERT(glun->nr_service == 0);
	CU_ASSERT(glun->waiting4deletion == 0);
}

static void test_lun_serviceable(void)
{
	int ret;

	glun = (lun_t *) sce_addlun((sce_hndl_t) gsce,
			SCE_SCTRPERFRAG * 123 + SCE_SCTRPERFRAG / 2, &glun);
	CU_ASSERT(glun != NULL);

	CU_ASSERT(glun->nr_frag == 124);

	glun->waiting4deletion = 1;
	CU_ASSERT(_lun_is_serviceable(glun) == SCE_ERROR);

	glun->waiting4deletion = 0;
	CU_ASSERT(_lun_is_serviceable(glun) == SCE_SUCCESS);

	ret = sce_rmlun((sce_lunhndl_t) glun);
	CU_ASSERT(ret == SCE_SUCCESS);
}

static void test_lun_purge(void)
{
	pfid_t pfid;
	uint32_t nr_lun;
	uint32_t nr_freefrag;
	uint16_t lunidx;
	int ret;

	CU_ASSERT(_lun_purge(NULL) != SCE_SUCCESS);

	nr_lun = gsce->nr_lun;
	glun =
	    (lun_t *) sce_addlun((sce_hndl_t) gsce,
				  SCE_SCTRPERFRAG * 123 + SCE_SCTRPERFRAG / 2,
				  &glun);
	CU_ASSERT(glun != NULL);

	lunidx = GET_LUNIDX(gsce, glun);

	CU_ASSERT(gsce->nr_lun == (nr_lun + 1));

	glun->nr_service = 1;
	CU_ASSERT(_lun_purge(glun) != SCE_SUCCESS);
	glun->nr_service = 0;

	nr_freefrag = gsce->nr_freefrag;
	CU_ASSERT(nr_freefrag > 2);

	ret = _freefraglist_get(gsce, &pfid);
	CU_ASSERT(SCE_SUCCESS == ret);

	ret = _map_frag(glun, 100, pfid);
	CU_ASSERT(SCE_SUCCESS == ret);

	ret = _freefraglist_get(gsce, &pfid);
	CU_ASSERT(SCE_SUCCESS == ret);
	ret = _map_frag(glun, 50, pfid);
	CU_ASSERT(SCE_SUCCESS == ret);

	CU_ASSERT(gsce->nr_freefrag == (nr_freefrag - 2));

	CU_ASSERT(gsce->luntbl[lunidx].fragmaps != NULL);

	ret = _lun_purge(glun);
	CU_ASSERT(SCE_SUCCESS == ret);

	CU_ASSERT(gsce->nr_lun == nr_lun);
	CU_ASSERT(gsce->nr_freefrag == nr_freefrag);
	CU_ASSERT(gsce->luntbl[lunidx].fragmaps == NULL);
}

static void test_lun_gc(void)
{
	pfid_t pfid;
	uint32_t nr_lun;
	uint32_t nr_freefrag;
	uint16_t lunidx;
	frag_t *frag50;
	frag_t *frag100;
	int ret;

	nr_lun = gsce->nr_lun;

	glun =
	    (lun_t *) sce_addlun((sce_hndl_t) gsce,
				  SCE_SCTRPERFRAG * 123 + SCE_SCTRPERFRAG / 2,
				  &glun);
	CU_ASSERT(glun != NULL);
	lunidx = GET_LUNIDX(gsce, glun);

	nr_freefrag = gsce->nr_freefrag;

	ret = _freefraglist_get(gsce, &pfid);
	CU_ASSERT(SCE_SUCCESS == ret);
	ret = _map_frag(glun, 100, pfid);
	CU_ASSERT(SCE_SUCCESS == ret);
	frag100 = _pfid2frag(gsce, pfid, NULL);
	CU_ASSERT(NULL != frag100);

	ret = _freefraglist_get(gsce, &pfid);
	CU_ASSERT(SCE_SUCCESS == ret);
	ret = _map_frag(glun, 50, pfid);
	CU_ASSERT(SCE_SUCCESS == ret);

	frag50 = _pfid2frag(gsce, pfid, NULL);
	CU_ASSERT(NULL != frag50);

	CU_ASSERT(_lun_gc(NULL, frag50) != SCE_SUCCESS);
	CU_ASSERT(_lun_gc(glun, NULL) != SCE_SUCCESS);

	CU_ASSERT(frag50->nr_valid > 0);
	CU_ASSERT(_lun_gc(glun, frag50) == SCE_SUCCESS);

	CU_ASSERT(0 == frag50->nr_service);
	frag50->nr_valid = 0;

	CU_ASSERT(_lun_gc(glun, frag50) == SCE_SUCCESS);
	CU_ASSERT((glun->fragmaps[0][50] & FRAGDESC_MAPPED) == 0);
	CU_ASSERT(gsce->nr_freefrag == (nr_freefrag - 1));

	glun->nr_service = 1;
	glun->waiting4deletion = 1;
	CU_ASSERT(_lun_gc(glun, frag100) == SCE_SUCCESS);
	CU_ASSERT((glun->fragmaps[0][100] & FRAGDESC_MAPPED) != 0);

	glun->nr_service = 0;
	CU_ASSERT(_lun_gc(glun, frag100) == SCE_SUCCESS);

	CU_ASSERT(gsce->nr_lun == nr_lun);
	CU_ASSERT(gsce->nr_freefrag == nr_freefrag);
	CU_ASSERT(gsce->luntbl[lunidx].fragmaps == NULL);
}

static void test_lun_isvalididx(void)
{
	uint16_t lunidx;

	glun =
	    (lun_t *) sce_addlun((sce_hndl_t) gsce,
				  SCE_SCTRPERFRAG * 123 + SCE_SCTRPERFRAG / 2,
				  &glun);
	CU_ASSERT(glun != NULL);

	lunidx = GET_LUNIDX(gsce, glun);

	CU_ASSERT(SCE_SUCCESS != _lun_isvalididx(NULL, lunidx));
	CU_ASSERT(SCE_SUCCESS != _lun_isvalididx(gsce, SCE_MAXLUN));
	CU_ASSERT(SCE_SUCCESS != _lun_isvalididx(gsce, lunidx + 1));

	CU_ASSERT(SCE_SUCCESS == _lun_isvalididx(gsce, lunidx));

	glun->waiting4deletion = 1;
	CU_ASSERT(SCE_SUCCESS != _lun_isvalididx(gsce, lunidx));
	glun->waiting4deletion = 0;

	CU_ASSERT(SCE_SUCCESS == sce_rmlun((sce_lunhndl_t) glun));
	CU_ASSERT(SCE_SUCCESS != _lun_isvalididx(gsce, lunidx));
}

static void test_lun_rmcdev(void)
{
	int ret;

	CU_ASSERT(_lun_rmcdev(NULL, 0) != SCE_SUCCESS);

	ret = _lun_init(gsce, glun, SCE_SCTRPERFRAG * 123);
	CU_ASSERT(ret == SCE_SUCCESS);
	CU_ASSERT(_lun_rmcdev(glun, SCE_MAXCDEV) != SCE_SUCCESS);

}

int addsuite_sce_lun(void)
{
	CU_pSuite suite;

	suite = CU_add_suite("sce_lun.c", _init_suite, _clean_suite);
	if (suite == NULL)
		return -1;

	ADDTEST(suite, test_lun_init);
	ADDTEST(suite, test_lun_destroy);
	ADDTEST(suite, test_lun_serviceable);
	ADDTEST(suite, test_lun_purge);
	ADDTEST(suite, test_lun_gc);
	ADDTEST(suite, test_lun_isvalididx);
	ADDTEST(suite, test_lun_rmcdev);

	return 0;
}
