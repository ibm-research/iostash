#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "CUnit/Basic.h"

#include "sce.h"
#include "sce_internal.h"

sce_hndl_t     gsce;
sce_lunhndl_t  glun;
sce_cdevhndl_t gcdev;

static void test_sce_create(void)
{
	sce_t *sce;

	gsce = sce_create();
	CU_ASSERT(gsce != NULL);

	sce = (sce_t *) gsce;

	CU_ASSERT(0 == sce->nr_cdev);
	CU_ASSERT(0 == sce->nr_lun);
	CU_ASSERT(0 == sce->nr_freefrag);
}

static void test_sce_destroy(void)
{
	CU_ASSERT(SCE_SUCCESS != sce_destroy(NULL));
	CU_ASSERT(SCE_SUCCESS == sce_destroy(gsce));
}

static void test_sce_addcdev(void)
{
	sce_t *sce;
	int i;

	gsce = sce_create();
	sce = (sce_t *) gsce;

	CU_ASSERT(NULL == sce_addcdev(NULL, SCE_SCTRPERFRAG * 10, (void*)123));
	CU_ASSERT(NULL == sce_addcdev(gsce, 0, (void*)123));
	CU_ASSERT(NULL == sce_addcdev(gsce, SCE_SCTRPERFRAG - 1, (void*)123));

	for (i = 0; i < SCE_MAXCDEV; i++) {
		CU_ASSERT(NULL !=
			  sce_addcdev(gsce, SCE_SCTRPERFRAG * 10, (void*)(sce + i)));
		CU_ASSERT(((i + 1) * 10) == sce->nr_freefrag);
		CU_ASSERT(((i + 1) * 10) == sce->nr_frag);
	}
	CU_ASSERT(NULL == sce_addcdev(gsce, SCE_SCTRPERFRAG * 10, (void*)200));

	CU_ASSERT(SCE_SUCCESS == sce_destroy(gsce));
}

static void test_sce_addlun(void)
{
	sce_t *sce;
	lun_t *lun;
	int i;

	gsce = sce_create();
	sce = (sce_t *) gsce;

	CU_ASSERT(NULL == sce_addlun(NULL, SCE_SCTRPERFRAG * 10, (void*)300));
	CU_ASSERT(NULL == sce_addlun(gsce, 0, (void*)400));

	for (i = 0; i < SCE_MAXLUN; i++) {
		lun = (lun_t *) sce_addlun(gsce, SCE_SCTRPERFRAG * 10, &lun);
		CU_ASSERT(NULL != lun);
		CU_ASSERT(10 == lun->nr_frag);
		CU_ASSERT(NULL != lun->fragmaps);
	}
	CU_ASSERT(NULL == sce_addlun(gsce, SCE_SCTRPERFRAG * 10, (void*)500));
	CU_ASSERT(sce->nr_lun == SCE_MAXLUN);

	CU_ASSERT(SCE_SUCCESS == sce_destroy(gsce));
}

static void test_sce_rmlun(void)
{
	sce_lunhndl_t l[SCE_MAXLUN];
	sce_t *sce;
	int i;

	gsce = sce_create();
	sce = (sce_t *) gsce;

	for (i = 0; i < SCE_MAXLUN; i++) {
		l[i] = sce_addlun(gsce, SCE_SCTRPERFRAG * 10, l + i);
		CU_ASSERT(NULL != l[i]);
	}
	CU_ASSERT(sce->nr_lun == SCE_MAXLUN);

	for (i = 0; i < SCE_MAXLUN; i++) {
		CU_ASSERT(SCE_SUCCESS == sce_rmlun(l[i]));
		CU_ASSERT(SCE_SUCCESS != sce_rmlun(l[i]));

		CU_ASSERT(sce->nr_lun == SCE_MAXLUN - i - 1);
	}
	CU_ASSERT(SCE_SUCCESS == sce_destroy(gsce));
}

static void test_sce_get4read(void)
{
	sce_fmap_t fmap;
	sce_t   *sce;
	lun_t   *lun;
	frag_t  *frag;
	uint32_t fragnum;
	pfid_t   pfid;
	sector_t s;
	uint32_t c;

	gsce  = sce_create();
	sce   = (sce_t *) gsce;
	gcdev = sce_addcdev(gsce, SCE_SCTRPERFRAG * 10, &gcdev);
	glun  = sce_addlun(gsce, SCE_SCTRPERFRAG * 100, &glun);
	lun   = (lun_t *) glun;

	for (fragnum = 10; fragnum < 15; fragnum++) {
		CU_ASSERT(SCE_SUCCESS ==
			  _alloc4population(lun, fragnum, &pfid));
		CU_ASSERT(SCE_SUCCESS == _complete_population(lun, fragnum));
	}

	for (fragnum = 20; fragnum < 25; fragnum++) {
		CU_ASSERT(SCE_SUCCESS ==
			  _alloc4population(lun, fragnum, &pfid));
		CU_ASSERT(SCE_SUCCESS == _complete_population(lun, fragnum));
	}

	CU_ASSERT(SCE_ERROR == sce_get4read(NULL, 0, 1, &fmap));
	CU_ASSERT(SCE_ERROR == sce_get4read(glun, lun->nr_sctr, 1, &fmap));
	CU_ASSERT(SCE_ERROR == sce_get4read(glun, 0, 0, &fmap));
	CU_ASSERT(SCE_ERROR == sce_get4read(glun, 0, 1, &fmap));

	/* cache hit */
	s = 10 * SCE_SCTRPERFRAG;
	c = 1;

	CU_ASSERT(SCE_SUCCESS == sce_get4read(glun, s, c, &fmap));
	CU_ASSERT(lun->nr_service == 1);

	CU_ASSERT(SCE_SUCCESS == sce_put4read(glun, s, c));
	CU_ASSERT(lun->nr_service  == 0);

	s   = 10 * SCE_SCTRPERFRAG;
	c   = 5  * SCE_SCTRPERFRAG;
	CU_ASSERT(SCE_ERROR == sce_get4read(glun, s, c, &fmap));
	CU_ASSERT(SCE_SUCCESS == sce_destroy(gsce));
}

static void test_sce_put4read(void)
{
	sce_fmap_t fmap;
	lun_t   *lun;
	uint32_t fragnum;
	pfid_t   pfid;
	sector_t s;
	uint32_t c;

	gsce  = sce_create();
	gcdev = sce_addcdev(gsce, SCE_SCTRPERFRAG * 10, &gcdev);
	glun  = sce_addlun(gsce, SCE_SCTRPERFRAG * 100, &glun);
	lun   = (lun_t *) glun;

	for (fragnum = 10; fragnum < 15; fragnum++) {
		CU_ASSERT(SCE_SUCCESS ==
			  _alloc4population(lun, fragnum, &pfid));
		CU_ASSERT(SCE_SUCCESS == _complete_population(lun, fragnum));
	}

	for (fragnum = 20; fragnum < 25; fragnum++) {
		CU_ASSERT(SCE_SUCCESS ==
			  _alloc4population(lun, fragnum, &pfid));
		CU_ASSERT(SCE_SUCCESS == _complete_population(lun, fragnum));
	}

	CU_ASSERT(SCE_SUCCESS != sce_put4read(NULL, 0, 1));
	CU_ASSERT(SCE_SUCCESS != sce_put4read(glun, lun->nr_sctr, 1));
	CU_ASSERT(SCE_SUCCESS != sce_put4read(glun, 0, 0));
	CU_ASSERT(SCE_SUCCESS != sce_put4read(glun, 0, 1));

	s = 10 * SCE_SCTRPERFRAG;
	c = 1  * SCE_SCTRPERFRAG;

	CU_ASSERT(SCE_SUCCESS == sce_get4read(glun, s, c, &fmap));
	CU_ASSERT(SCE_SUCCESS == sce_put4read(glun, s, c));
	CU_ASSERT(SCE_SUCCESS == sce_destroy(gsce));
}

static void test_sce_invalidate(void)
{
	sce_fmap_t fmap;
	sce_t   *sce;
	lun_t   *lun;
	frag_t  *frag;
	uint32_t fragnum;
	pfid_t   pfid;
	sector_t s;
	uint32_t c;

	gsce  = sce_create();
	sce   = (sce_t *) gsce;
	gcdev = sce_addcdev(gsce, SCE_SCTRPERFRAG * 10, &gcdev);
	glun  = sce_addlun(gsce, SCE_SCTRPERFRAG * 100, &glun);
	lun   = (lun_t *) glun;

	for (fragnum = 10; fragnum < 15; fragnum++) {
		CU_ASSERT(SCE_SUCCESS ==
			  _alloc4population(lun, fragnum, &pfid));
		CU_ASSERT(SCE_SUCCESS == _complete_population(lun, fragnum));
	}

	for (fragnum = 20; fragnum < 25; fragnum++) {
		CU_ASSERT(SCE_SUCCESS ==
			  _alloc4population(lun, fragnum, &pfid));
		CU_ASSERT(SCE_SUCCESS == _complete_population(lun, fragnum));
	}

	CU_ASSERT(SCE_SUCCESS != sce_invalidate(NULL, 0, 1));
	CU_ASSERT(SCE_SUCCESS != sce_invalidate(glun, lun->nr_sctr, 1));
	CU_ASSERT(SCE_SUCCESS != sce_invalidate(glun, 0, 0));

	/* cache hit */
	s = 10 * SCE_SCTRPERFRAG;
	c = SCE_SCTRPERFRAG;

	CU_ASSERT(SCE_SUCCESS == sce_get4read(glun, s, c, &fmap));
	CU_ASSERT(SCE_SUCCESS == sce_put4read(glun, s, c));

	CU_ASSERT(SCE_SUCCESS == sce_invalidate(glun, s + 1, 1));
	CU_ASSERT(SCE_SUCCESS != sce_get4read(glun, s, c, &fmap));
	CU_ASSERT(SCE_SUCCESS == sce_invalidate(glun, s, c));

	CU_ASSERT(SCE_SUCCESS == sce_destroy(gsce));
}

static void test_sce_get_status(void)
{
	sce_fmap_t  fmap;
	lun_t       *lun;
	uint32_t     fragnum;
	pfid_t       pfid;
	sce_status_t st;

	gsce  = sce_create();
	gcdev = sce_addcdev(gsce, SCE_SCTRPERFRAG * 10, &gcdev);
	glun  = sce_addlun(gsce, SCE_SCTRPERFRAG * 100, &glun);
	lun   = (lun_t *) glun;

	for (fragnum = 10; fragnum < 15; fragnum++) {
		CU_ASSERT(SCE_SUCCESS ==
			  _alloc4population(lun, fragnum, &pfid));
		CU_ASSERT(SCE_SUCCESS == _complete_population(lun, fragnum));
	}

	CU_ASSERT(SCE_SUCCESS != sce_get_status(NULL, &st));
	CU_ASSERT(SCE_SUCCESS != sce_get_status(gsce, NULL));

	CU_ASSERT(SCE_SUCCESS == sce_get_status(gsce, &st));
	CU_ASSERT(0 == st.nr_hit);
	CU_ASSERT(0 == st.nr_miss);
	CU_ASSERT(5 == st.nr_freefrag);

	sce_get4read(glun, 10 * SCE_SCTRPERFRAG, 1, &fmap);
	sce_get4read(glun, 10 * SCE_SCTRPERFRAG, 1, &fmap);
	sce_get4read(glun, 10 * SCE_SCTRPERFRAG, 1, &fmap);

	sce_put4read(glun, 10 * SCE_SCTRPERFRAG, 1);
	sce_put4read(glun, 10 * SCE_SCTRPERFRAG, 1);
	sce_put4read(glun, 10 * SCE_SCTRPERFRAG, 1);

	sce_get4read(glun, 20 * SCE_SCTRPERFRAG, 1, &fmap);
	sce_get4read(glun, 20 * SCE_SCTRPERFRAG, 1, &fmap);

	CU_ASSERT(SCE_SUCCESS == sce_get_status(gsce, &st));
	CU_ASSERT(3 == st.nr_hit);
	CU_ASSERT(2 == st.nr_miss);
	CU_ASSERT(5 == st.nr_freefrag);

	CU_ASSERT(SCE_SUCCESS == sce_destroy(gsce));
}

static void test_sce_get4pop(void)
{
	sce_fmap_t    fmap;
	sce_t        *sce;
	lun_t        *lun;
	uint32_t      fragnum;
	pfid_t        pfid;
	sce_poptask_t ptask;

	gsce  = sce_create();
	sce   = (sce_t *) gsce;
	gcdev = sce_addcdev(gsce, SCE_SCTRPERFRAG * 10, &gcdev);
	glun  = sce_addlun(gsce, SCE_SCTRPERFRAG * 100, &glun );
	lun   = (lun_t *) glun;

	for (fragnum = 10; fragnum < 15; fragnum++) {
		CU_ASSERT(SCE_SUCCESS ==
			  _alloc4population(lun, fragnum, &pfid));
		CU_ASSERT(SCE_SUCCESS == _complete_population(lun, fragnum));
	}

	CU_ASSERT(SCE_SUCCESS != sce_get4pop(NULL, &ptask));
	CU_ASSERT(SCE_SUCCESS != sce_get4pop(gsce, NULL));

	sce_get4read(glun, 30 * SCE_SCTRPERFRAG, 1, &fmap);
	sce_get4read(glun, 40 * SCE_SCTRPERFRAG, 1, &fmap);
	sce_get4read(glun, 40 * SCE_SCTRPERFRAG, 1, &fmap);
	sce_get4read(glun, 40 * SCE_SCTRPERFRAG, 1, &fmap);

	CU_ASSERT(SCE_SUCCESS  == sce_get4pop(gsce, &ptask));
	CU_ASSERT(ptask.lun_fragnum == 40);
	CU_ASSERT(SCE_SUCCESS == sce_put4pop(gsce, &ptask, 0));

	CU_ASSERT(SCE_SUCCESS == sce_get4pop(gsce, &ptask));
	CU_ASSERT(ptask.lun_fragnum == 30);
	CU_ASSERT(SCE_SUCCESS == sce_put4pop(gsce, &ptask, 0));

	CU_ASSERT(SCE_SUCCESS == sce_destroy(gsce));
}

static void test_sce_put4pop(void)
{
	sce_fmap_t fmap;
	lun_t *lun;
	sce_poptask_t ptask;

	gsce  = sce_create();
	gcdev = sce_addcdev(gsce, SCE_SCTRPERFRAG * 10, &gcdev);
	glun  = sce_addlun(gsce, SCE_SCTRPERFRAG * 100, &glun);
	lun   = (lun_t *) glun;

	CU_ASSERT(SCE_SUCCESS != sce_put4pop(NULL, &ptask, 0));
	CU_ASSERT(SCE_SUCCESS != sce_put4pop(gsce,  NULL,  0));

	ptask.lun_fragnum  = 0;
	ptask.cdev_fragnum = 0;
	ptask.lunctx       = &glun;
	ptask.cdevctx      = &gcdev;
	CU_ASSERT(SCE_SUCCESS != sce_put4pop(gsce, &ptask, 0));

	sce_get4read(glun, 20 * SCE_SCTRPERFRAG, 1, &fmap);

	CU_ASSERT(SCE_SUCCESS == sce_get4pop(gsce, &ptask));
	CU_ASSERT(SCE_SUCCESS == sce_put4pop(gsce, &ptask, 0));

	CU_ASSERT(lun->fragmaps[0][20] & FRAGDESC_MAPPED);

	CU_ASSERT(SCE_SUCCESS == sce_destroy(gsce));

	gsce  = sce_create();
	gcdev = sce_addcdev(gsce, SCE_SCTRPERFRAG * 10, &gcdev);
	glun  = sce_addlun(gsce, SCE_SCTRPERFRAG * 100, &glun);
	lun   = (lun_t *) glun;

	ptask.lunctx       = &glun;
	ptask.cdevctx      = &gcdev;
	ptask.lun_fragnum  = 0;
	ptask.cdev_fragnum = 0;
	CU_ASSERT(SCE_SUCCESS != sce_put4pop(gsce, &ptask, 1));

	sce_get4read(glun, 20 * SCE_SCTRPERFRAG, 1, &fmap);

	CU_ASSERT(SCE_SUCCESS == sce_get4pop(gsce, &ptask));
	CU_ASSERT(SCE_SUCCESS == sce_put4pop(gsce, &ptask, 1));

	CU_ASSERT((lun->fragmaps[0][20] & FRAGDESC_MAPPED) == 0);

	CU_ASSERT(SCE_SUCCESS == sce_destroy(gsce));
}

int addsuite_sce_interface(void)
{
	CU_pSuite suite;

	suite = CU_add_suite("sce_interface.c", NULL, NULL);
	if (suite == NULL)
		return -1;

	ADDTEST(suite, test_sce_create);
	ADDTEST(suite, test_sce_destroy);
	ADDTEST(suite, test_sce_addcdev);
	ADDTEST(suite, test_sce_addlun);
	ADDTEST(suite, test_sce_rmlun);
	ADDTEST(suite, test_sce_get4read);
	ADDTEST(suite, test_sce_put4read);
	ADDTEST(suite, test_sce_invalidate);
	ADDTEST(suite, test_sce_get_status);
	ADDTEST(suite, test_sce_get4pop);
	ADDTEST(suite, test_sce_put4pop);

	return 0;
}
