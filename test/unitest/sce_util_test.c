#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "CUnit/Basic.h"

#include "sce.h"
#include "sce_internal.h"

sce_t  *gsce;
cdev_t *gcdev;

static int _init_suite(void)
{
	int ret = -1;
	gsce = (sce_t *) sce_create();

	if (gsce) {
		gcdev =
		    (cdev_t *) sce_addcdev((sce_hndl_t) gsce,
					      567 * SCE_SCTRPERFRAG, &gcdev);
		if (gcdev) {
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

static void test_pfid2frag(void)
{
	frag_t *frag;
	int cdevidx;
	pfid_t pfid;

	cdevidx = GET_CDEVIDX(gsce, gcdev);
	pfid = SCE_PFID(cdevidx, 100);

	frag = _pfid2frag(gsce, pfid, NULL);
	CU_ASSERT(frag == &gsce->cdevtbl[cdevidx].fragtbls[0][100]);

	CU_ASSERT(_pfid2frag(NULL, pfid, NULL) == NULL);

	pfid = SCE_PFID(cdevidx, 567);
	CU_ASSERT(_pfid2frag(gsce, pfid, NULL) == NULL);

	pfid = SCE_PFID(cdevidx + 1, 100);
	CU_ASSERT(_pfid2frag(gsce, pfid, NULL) == NULL);
}

static void test_isvalidpfid(void)
{
	int cdevidx;

	cdevidx = GET_CDEVIDX(gsce, gcdev);

	CU_ASSERT(SCE_SUCCESS != _isvalidpfid(NULL, SCE_PFID(cdevidx, 0)));
	CU_ASSERT(SCE_SUCCESS != _isvalidpfid(gsce, SCE_PFID(cdevidx + 1, 0)));
	CU_ASSERT(SCE_SUCCESS != _isvalidpfid(gsce, SCE_PFID(cdevidx, 567)));
	CU_ASSERT(SCE_SUCCESS == _isvalidpfid(gsce, SCE_PFID(cdevidx, 566)));
}

int addsuite_sce_util(void)
{
	CU_pSuite suite;

	suite = CU_add_suite("sce_util.c", _init_suite, _clean_suite);
	if (suite == NULL)
		return -1;

	ADDTEST(suite, test_pfid2frag);
	ADDTEST(suite, test_isvalidpfid);

	return 0;
}
