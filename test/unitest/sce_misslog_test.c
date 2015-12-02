#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "CUnit/Basic.h"

#include "sce.h"
#include "sce_internal.h"

sce_t *gsce;
cdev_t *gcdev;
lun_t *glun1;
lun_t *glun2;

static int _init_suite(void)
{
	gsce = (sce_t *) sce_create();
	if (!gsce)
		return -1;

	glun1 = (lun_t *) sce_addlun((sce_hndl_t) gsce,
			(SCE_CACHEMISSLOGSIZE + 1) * SCE_SCTRPERFRAG, NULL);
	if (!glun1)
		return -1;

	glun2 = (lun_t *) sce_addlun((sce_hndl_t) gsce,
			  (SCE_CACHEMISSLOGSIZE + 1) * SCE_SCTRPERFRAG, NULL);
	if (!glun2)
		return -1;

	return 0;
}

static int _clean_suite(void)
{
	sce_destroy(gsce);
	return 0;
}

static void test_misslog_put(void)
{
	uint16_t lunidx;
	uint32_t fragnum;

	lunidx = GET_LUNIDX(gsce, glun1);

	CU_ASSERT(SCE_SUCCESS != _misslog_put(NULL, lunidx, 0));

	for (fragnum = 0; fragnum <= SCE_CACHEMISSLOGSIZE; fragnum++) {
		CU_ASSERT(SCE_SUCCESS == _misslog_put(gsce, lunidx, fragnum));
	}
	CU_ASSERT(SCE_CACHEMISSLOGSIZE == gsce->misslog_size);
}

static void test_misslog_get(void)
{
	uint16_t lunidx, l;
	uint32_t fragnum;
	uint32_t i;

	l = GET_LUNIDX(gsce, glun1);

	CU_ASSERT(1 == gsce->misslog_head);

	CU_ASSERT(SCE_CACHEMISSLOGSIZE == gsce->misslog_size);

	CU_ASSERT(SCE_SUCCESS != _misslog_get(NULL, 0, &lunidx, &fragnum));
	CU_ASSERT(SCE_SUCCESS != _misslog_get(gsce, 0, NULL, &fragnum));
	CU_ASSERT(SCE_SUCCESS != _misslog_get(gsce, 0, &lunidx, NULL));
	CU_ASSERT(SCE_SUCCESS !=
		  _misslog_get(gsce, SCE_CACHEMISSLOGSIZE, &lunidx, &fragnum));

	for (i = 0; i < SCE_CACHEMISSLOGSIZE; i++) {
		CU_ASSERT(SCE_SUCCESS ==
			  _misslog_get(gsce, i, &lunidx, &fragnum));
		CU_ASSERT(l == lunidx);
		CU_ASSERT(fragnum == (SCE_CACHEMISSLOGSIZE - i));
	}
	CU_ASSERT(SCE_CACHEMISSLOGSIZE == gsce->misslog_size);
}

static void test_misslog_gc(void)
{
	uint32_t fragnum;
	uint16_t lunidx2;
	uint16_t lunidx;
	int i;

	lunidx2 = GET_LUNIDX(gsce, glun2);

	for (fragnum = 0; fragnum < SCE_CACHEMISSLOGSIZE / 2; fragnum++) {
		CU_ASSERT(SCE_SUCCESS == _misslog_put(gsce, lunidx2, fragnum));
	}

	CU_ASSERT(sce_rmlun((sce_lunhndl_t) glun1) == SCE_SUCCESS);

	CU_ASSERT(SCE_SUCCESS != _misslog_gc(NULL));
	CU_ASSERT(SCE_SUCCESS == _misslog_gc(gsce));

	for (i = 0; i < SCE_CACHEMISSLOGSIZE / 2; i++) {
		CU_ASSERT(SCE_SUCCESS ==
			  _misslog_get(gsce, i, &lunidx, &fragnum));
		CU_ASSERT(lunidx2 == lunidx);
		CU_ASSERT(fragnum == ((SCE_CACHEMISSLOGSIZE / 2) - 1 - i));
	}

	for (i = (SCE_CACHEMISSLOGSIZE / 2); i < SCE_CACHEMISSLOGSIZE; i++) {
		CU_ASSERT(SCE_SUCCESS !=
			  _misslog_get(gsce, i, &lunidx, &fragnum));
	}
}

int addsuite_sce_misslog(void)
{
	CU_pSuite suite;

	suite = CU_add_suite("sce_misslog.c", _init_suite, _clean_suite);
	if (suite == NULL)
		return -1;

	ADDTEST(suite, test_misslog_put);
	ADDTEST(suite, test_misslog_get);
	ADDTEST(suite, test_misslog_gc);

	return 0;
}
