#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "CUnit/Basic.h"

#include "sce.h"
#include "sce_internal.h"

sce_t  *gsce;
lun_t  *glun;
cdev_t *gcdev;

static int _init_suite(void)
{
	gsce = (sce_t *) sce_create();
	if (!gsce)
		return -1;

	gcdev = (cdev_t *) sce_addcdev((sce_hndl_t) gsce,
				  4 * SCE_SCTRPERFRAG, &gcdev);
	if (!gcdev)
		return -1;

	glun = (lun_t *) sce_addlun((sce_hndl_t) gsce,
				  100 * SCE_SCTRPERFRAG, &glun);

	return (glun) ? 0 : -1;
}

static int _clean_suite(void)
{
	sce_destroy(gsce);
	return 0;
}

static void test_alloc4population(void)
{
	sce_fmap_t fmap;
	pfid_t pfid;
	frag_t *frag;
	uint16_t lunidx;
	uint32_t fragnum;

	lunidx = GET_LUNIDX(gsce, glun);

	CU_ASSERT(SCE_SUCCESS != _alloc4population(NULL, 0, &pfid));
	CU_ASSERT(SCE_SUCCESS !=
		  _alloc4population(&gsce->luntbl[lunidx + 1], 0, &pfid));
	CU_ASSERT(SCE_SUCCESS != _alloc4population(glun, 100, &pfid));
	CU_ASSERT(SCE_SUCCESS != _alloc4population(glun, 0, NULL));

	for (fragnum = 0; fragnum < 10; fragnum++) {
		CU_ASSERT(SCE_SUCCESS ==
			  _alloc4population(glun, fragnum, &pfid));
		CU_ASSERT(glun->fragmaps[0][fragnum] & FRAGDESC_MAPPED);
		CU_ASSERT(FRAGDESC_DATA(pfid) ==
			  FRAGDESC_DATA(glun->fragmaps[0][fragnum]));

		frag = _pfid2frag(gsce, pfid, NULL);
		CU_ASSERT(glun->nr_service == 1);
		CU_ASSERT(frag->nr_service == 1);
		CU_ASSERT(SCE_SUCCESS != sce_get4read(glun, fragnum * SCE_SCTRPERFRAG, 1, &fmap));

		CU_ASSERT(SCE_SUCCESS !=
			  _alloc4population(glun, fragnum, &pfid));
		glun->fragmaps[0][fragnum] |= FRAGDESC_VALID;
		glun->nr_service = 0;
		frag->nr_service = 0;
	}
}

static void test_complete_population(void)
{
	pfid_t pfid;
	uint16_t lunidx;
	uint32_t fragnum;

	lunidx = GET_LUNIDX(gsce, glun);

	CU_ASSERT(SCE_SUCCESS != _complete_population(NULL, 0));
	CU_ASSERT(SCE_SUCCESS !=
		  _complete_population(&gsce->luntbl[lunidx + 1], 0));
	CU_ASSERT(SCE_SUCCESS != _complete_population(glun, glun->nr_frag));
	CU_ASSERT(SCE_SUCCESS != _complete_population(glun, 20));

	for (fragnum = 20; fragnum < 30; fragnum++) {
		CU_ASSERT(SCE_SUCCESS ==
			  _alloc4population(glun, fragnum, &pfid));
		CU_ASSERT(SCE_SUCCESS == _complete_population(glun, fragnum));
	}
}

static void test_cancel_population(void)
{
	pfid_t pfid;
	frag_t *frag;
	uint16_t lunidx;
	uint32_t fragnum;

	lunidx = GET_LUNIDX(gsce, glun);

	CU_ASSERT(SCE_SUCCESS != _cancel_population(NULL, 0));
	CU_ASSERT(SCE_SUCCESS !=
		  _cancel_population(&gsce->luntbl[lunidx + 1], 0));
	CU_ASSERT(SCE_SUCCESS != _cancel_population(glun, glun->nr_frag));
	CU_ASSERT(SCE_SUCCESS != _cancel_population(glun, 29));

	for (fragnum = 30; fragnum < 40; fragnum++) {
		glun->fragmaps[0][fragnum] = 1234;
		CU_ASSERT(SCE_SUCCESS ==
			  _alloc4population(glun, fragnum, &pfid));
		CU_ASSERT(SCE_SUCCESS == _cancel_population(glun, fragnum));

		frag = _pfid2frag(gsce, pfid, NULL);
		CU_ASSERT(FRAGNUM_WITHINFREELIST == frag->fragnum);
	}
}

static void test_choose4population(void)
{
	uint16_t lunidx;
	uint32_t fragnum;
	frag_t *frag;
	fragdesc_t fdesc;
	pfid_t pfid;
	int i;

	CU_ASSERT(SCE_SUCCESS != _choose4population(NULL, &lunidx, &fragnum));
	CU_ASSERT(SCE_SUCCESS != _choose4population(gsce, NULL, &fragnum));
	CU_ASSERT(SCE_SUCCESS != _choose4population(gsce, &lunidx, NULL));

	lunidx = GET_LUNIDX(gsce, glun);
	for (i = (SCE_CACHEMISSLOGSIZE * 100); i > 0; i--) {
		fragnum = rand() % glun->nr_frag;
		fdesc = glun->fragmaps[0][fragnum];
		if ((fdesc & FRAGDESC_MAPPED) && (fdesc & FRAGDESC_VALID)) {
			frag = _pfid2frag(gsce, fdesc, NULL);
			frag->nr_hit++;
			frag->nr_service--;
		} else {
			if (SCE_SUCCESS != _misslog_put(gsce, lunidx, fragnum))
				break;
		}
		if (((i % 10) == 0) && (gsce->misslog_size > 0)) {
			if (SCE_SUCCESS ==
			    _choose4population(gsce, &lunidx, &fragnum)) {
				if (SCE_SUCCESS ==
				    _alloc4population(&gsce->luntbl[lunidx],
						      fragnum, &pfid)) {
					CU_ASSERT(SCE_SUCCESS ==
					  _complete_population(&gsce-> luntbl
							[lunidx], fragnum));
				}
			}
		}
	}
	CU_ASSERT(i == 0);
}

int addsuite_sce_population(void)
{
	CU_pSuite suite;

	suite = CU_add_suite("sce_population.c", _init_suite, _clean_suite);
	if (suite == NULL)
		return -1;

	ADDTEST(suite, test_alloc4population);
	ADDTEST(suite, test_complete_population);
	ADDTEST(suite, test_cancel_population);
	ADDTEST(suite, test_choose4population);

	return 0;
}
