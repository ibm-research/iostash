#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "CUnit/Basic.h"

#include "sce.h"
#include "sce_internal.h"

sce_t *gsce;
lun_t *glun;
cdev_t *gcdev1;
cdev_t *gcdev2;

static int _init_suite(void)
{
	gsce = (sce_t *) sce_create();
	if (!gsce)
		return -1;

	glun = (lun_t *)sce_addlun((sce_hndl_t) gsce,
				  100 * SCE_SCTRPERFRAG, &glun);
	return (glun) ? 0 : -1;
}

static int _clean_suite(void)
{
	sce_destroy(gsce);
	return 0;
}

static void test_get_frag_advance_clockarm(void)
{
#ifndef SCE_EVICTION_RANDOM
	frag_t *frag;
	int     i, j;

	CU_ASSERT(NULL == _get_frag_advance_clockarm(NULL));
	CU_ASSERT(NULL == _get_frag_advance_clockarm(gsce));
#endif /* SCE_EVICTION_RANDOM */

	gcdev1 = (cdev_t *) sce_addcdev((sce_hndl_t) gsce,
					   10 * SCE_SCTRPERFRAG, &gcdev1);
	CU_ASSERT(gcdev1 != NULL);
	gcdev2 = (cdev_t *) sce_addcdev((sce_hndl_t) gsce, 
					   20 * SCE_SCTRPERFRAG, &gcdev2);
	CU_ASSERT(gcdev2 != NULL);

#ifndef SCE_EVICTION_RANDOM
	for (j = 0; j < 10; j++) {
		for (i = 0; i < 10; i++) {
			frag = _get_frag_advance_clockarm(gsce);
			CU_ASSERT(frag == &gcdev1->fragtbls[0][i]);
		}

		for (i = 0; i < 20; i++) {
			frag = _get_frag_advance_clockarm(gsce);
			CU_ASSERT(frag == &gcdev2->fragtbls[0][i]);
		}
	}
#endif /* SCE_EVICTION_RANDOM */
}

static void test_evict_frag(void)
{
#ifndef SCE_EVICTION_RANDOM
	frag_t *frag;
#endif
	uint32_t fragnum;
	pfid_t pfid;

	CU_ASSERT(SCE_SUCCESS != _evict_frag(NULL));
	CU_ASSERT(SCE_SUCCESS != _evict_frag(gsce));	/* because all fragments are already free */

	for (fragnum = 0; fragnum < 30; fragnum++) {
		CU_ASSERT(SCE_SUCCESS ==
			  _alloc4population(glun, fragnum, &pfid));
	}
	CU_ASSERT(0 == gsce->nr_freefrag);

	CU_ASSERT(SCE_SUCCESS != _evict_frag(gsce));	/* nothing to evict */
	for (fragnum = 0; fragnum < 30; fragnum++) {
		CU_ASSERT(SCE_SUCCESS == _complete_population(glun, fragnum));
	}

	CU_ASSERT(SCE_SUCCESS == _evict_frag(gsce));
	CU_ASSERT(1 == gsce->nr_freefrag);

#ifndef SCE_EVICTION_RANDOM
	frag = _pfid2frag(gsce, gsce->arm, NULL);
	CU_ASSERT(frag->fragnum < FRAGNUM_FREE);
	CU_ASSERT(SCE_SUCCESS == _evict_frag(gsce));
	CU_ASSERT(2 == gsce->nr_freefrag);
	CU_ASSERT(FRAGNUM_WITHINFREELIST == frag->fragnum);

	frag = _pfid2frag(gsce, gsce->arm, NULL);
	CU_ASSERT(frag->fragnum < FRAGNUM_FREE);
	frag->nr_ref++;
	CU_ASSERT(SCE_SUCCESS == _evict_frag(gsce));
	CU_ASSERT(3 == gsce->nr_freefrag);
	CU_ASSERT(frag->fragnum < FRAGNUM_FREE);
#endif
}

int addsuite_sce_eviction(void)
{
	CU_pSuite suite;

	suite = CU_add_suite("sce_eviction.c", _init_suite, _clean_suite);
	if (suite == NULL)
		return -1;

	ADDTEST(suite, test_get_frag_advance_clockarm);
	ADDTEST(suite, test_evict_frag);

	return 0;
}
