#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "CUnit/Basic.h"

#include "sce.h"
#include "sce_internal.h"

sce_t   gsce;
cdev_t *gcdev;

static int _init_suite(void)
{
	memset(&gsce, 0, sizeof(sce_t));
	gcdev = &gsce.cdevtbl[0];

	return 0;
}

static void test_cdev_init(void)
{
	int ret;

	ret = _cdev_init(&gsce, gcdev, SCE_SCTRPERFRAG - 1);
	CU_ASSERT(ret != SCE_SUCCESS);

	ret = _cdev_init(&gsce, gcdev, 0);
	CU_ASSERT(ret != SCE_SUCCESS);

	ret =
	    _cdev_init(NULL, gcdev,
		       SCE_SCTRPERFRAG * 123 + SCE_SCTRPERFRAG / 2);
	CU_ASSERT(ret != SCE_SUCCESS);

	ret =
	    _cdev_init(&gsce, NULL,
		       SCE_SCTRPERFRAG * 123 + SCE_SCTRPERFRAG / 2);
	CU_ASSERT(ret != SCE_SUCCESS);

	ret =
	    _cdev_init(&gsce, gcdev,
		       SCE_SCTRPERFRAG * 123 + SCE_SCTRPERFRAG / 2);
	CU_ASSERT(ret == SCE_SUCCESS);

	CU_ASSERT(gcdev->scehndl == (sce_hndl_t) & gsce);
	CU_ASSERT(gcdev->nr_frag == 123);
	CU_ASSERT(gcdev->fragtbls != NULL);
}

static void test_cdev_destroy(void)
{
	int ret;

	ret = _cdev_destroy(NULL);
	CU_ASSERT(ret != SCE_SUCCESS);

	ret = _cdev_destroy(gcdev);
	CU_ASSERT(ret == SCE_SUCCESS);

	CU_ASSERT(gcdev->scehndl == NULL);
	CU_ASSERT(gcdev->nr_frag == 0);
	CU_ASSERT(gcdev->fragtbls == NULL);
}

int addsuite_sce_cachedev(void)
{
	CU_pSuite suite;

	suite = CU_add_suite("sce_cachedev.c", _init_suite, NULL);
	if (suite == NULL)
		return -1;

	ADDTEST(suite, test_cdev_init);
	ADDTEST(suite, test_cdev_destroy);

	return 0;
}
