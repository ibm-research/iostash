#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "CUnit/Basic.h"

#include "sce.h"
#include "sce_internal.h"

sce_t *gsce;

static int _init_suite(void)
{
	int ret = -1;
	gsce = (sce_t *) sce_create();

	if (gsce) {
		if (_cdev_init(gsce, &gsce->cdevtbl[1], 123 * SCE_SCTRPERFRAG)
		    == SCE_SUCCESS) {
			gsce->nr_frag = 123;
			ret = 0;
		}
	}

	return ret;
}

static int _clean_suite(void)
{
	if (gsce) {
		sce_destroy(gsce);
	}
	return 0;
}

static void test_freefraglist_put(void)
{
	frag_t *frag;
	uint32_t nr_freefrag;
	uint32_t fragnum;
	uint32_t pfid;

	nr_freefrag = gsce->nr_freefrag;

	CU_ASSERT(_freefraglist_put(NULL, SCE_PFID(1, 0)) != SCE_SUCCESS);
	CU_ASSERT(_freefraglist_put(gsce, SCE_PFID(0, 0)) != SCE_SUCCESS);
	CU_ASSERT(_freefraglist_put(gsce, SCE_PFID(1, 123)) != SCE_SUCCESS);

	CU_ASSERT(_freefraglist_put(gsce, SCE_PFID(1, 0)) == SCE_SUCCESS);
	CU_ASSERT(_freefraglist_put(gsce, SCE_PFID(1, 0)) != SCE_SUCCESS);

	for (fragnum = 1; fragnum < 123; fragnum++) {
		pfid = SCE_PFID(1, fragnum);
		frag = _pfid2frag(gsce, pfid, NULL);

		CU_ASSERT(_freefraglist_put(gsce, pfid) == SCE_SUCCESS);
		CU_ASSERT(frag->fragnum == FRAGNUM_WITHINFREELIST);
	}
	CU_ASSERT((nr_freefrag + 123) == gsce->nr_freefrag);
}

static void  test_freefraglist_get(void)
{
	frag_t *frag;
	uint32_t nr_freefrag;
	uint32_t fragnum;
	uint32_t pfid;

	nr_freefrag = gsce->nr_freefrag;

	CU_ASSERT(_freefraglist_get(NULL, &pfid) != SCE_SUCCESS);
	CU_ASSERT(_freefraglist_get(gsce, NULL) != SCE_SUCCESS);

	for (fragnum = 122;; fragnum--) {
		CU_ASSERT(_freefraglist_get(gsce, &pfid) == SCE_SUCCESS);
		frag = _pfid2frag(gsce, pfid, NULL);
		CU_ASSERT(frag->fragnum == FRAGNUM_FREE);

		CU_ASSERT(SCE_PFIDDEV(pfid) == 1);
		CU_ASSERT(SCE_PFIDOFF(pfid) == fragnum);

		if (fragnum == 0)
			break;
	}
	CU_ASSERT(gsce->nr_freefrag == (nr_freefrag - 123));
}

static void test_freefraglist_rmcdev(void)
{
	CU_ASSERT(_freefraglist_rmcdev(NULL, 0) != SCE_SUCCESS);
	CU_ASSERT(_freefraglist_rmcdev(gsce, SCE_MAXCDEV) != SCE_SUCCESS);

	CU_ASSERT(_cdev_init(gsce, &gsce->cdevtbl[2], 100 * SCE_SCTRPERFRAG) ==
		  SCE_SUCCESS);

	CU_ASSERT(_freefraglist_put(gsce, SCE_PFID(1, 55)) == SCE_SUCCESS);
	CU_ASSERT(_freefraglist_put(gsce, SCE_PFID(1, 66)) == SCE_SUCCESS);
	CU_ASSERT(_freefraglist_put(gsce, SCE_PFID(1, 77)) == SCE_SUCCESS);
	CU_ASSERT(_freefraglist_put(gsce, SCE_PFID(2, 88)) == SCE_SUCCESS);
	CU_ASSERT(_freefraglist_put(gsce, SCE_PFID(2, 99)) == SCE_SUCCESS);

	CU_ASSERT(gsce->nr_freefrag == 5);
	CU_ASSERT(_freefraglist_rmcdev(gsce, 1) == SCE_SUCCESS);
	CU_ASSERT(gsce->nr_freefrag == 2);
}

int addsuite_sce_freefraglist(void)
{
	CU_pSuite suite;

	suite = CU_add_suite("sce_freefraglist.c", _init_suite, _clean_suite);
	if (suite == NULL)
		return -1;

	ADDTEST(suite, test_freefraglist_put);
	ADDTEST(suite, test_freefraglist_get);
	ADDTEST(suite, test_freefraglist_rmcdev);

	return 0;
}
