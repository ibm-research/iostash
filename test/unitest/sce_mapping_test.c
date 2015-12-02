#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "CUnit/Basic.h"

#include "sce.h"
#include "sce_internal.h"

sce_t *gsce;
lun_t *glun;
cdev_t *gcdev;

static int _init_suite(void)
{
	gsce = (sce_t *) sce_create();
	if (!gsce)
		return -1;

	gcdev = (cdev_t *) sce_addcdev((sce_hndl_t) gsce,
				  567 * SCE_SCTRPERFRAG, &gcdev);
	if (!gcdev)
		return -1;

	glun = (lun_t *) sce_addlun((sce_hndl_t) gsce,
				  456 * SCE_SCTRPERFRAG, &glun);
	return (glun) ? 0 : -1;
}

static int _clean_suite(void)
{
	sce_destroy(gsce);
	return 0;
}

static void test_map_frag(void)
{
	pfid_t pfid;
	pfid_t pfid3;

	CU_ASSERT(SCE_SUCCESS == _freefraglist_get(gsce, &pfid));
	CU_ASSERT(SCE_SUCCESS != _map_frag(NULL, 0, pfid));
	CU_ASSERT(SCE_SUCCESS != _map_frag(&gsce->luntbl[1], 0, pfid));
	CU_ASSERT(SCE_SUCCESS != _map_frag(glun, glun->nr_frag, pfid));
	CU_ASSERT(SCE_SUCCESS != _map_frag(glun, 0, SCE_PFID(1, 0)));
	CU_ASSERT(SCE_SUCCESS != _map_frag(glun, 0, SCE_PFID(0, 600)));

	CU_ASSERT((glun->fragmaps[0][1] & FRAGDESC_MAPPED) == 0);
	glun->fragmaps[0][1] = 123;

	CU_ASSERT(SCE_SUCCESS == _map_frag(glun, 1, pfid));
	CU_ASSERT((glun->fragmaps[0][1] & FRAGDESC_MAPPED) != 0);

	glun->fragmaps[0][1] |= FRAGDESC_VALID;

	CU_ASSERT(FRAGDESC_DATA(glun->fragmaps[0][1]) == FRAGDESC_DATA(pfid));

	glun->fragmaps[0][10] = 456;
	CU_ASSERT(SCE_SUCCESS == _freefraglist_get(gsce, &pfid3));
	CU_ASSERT(SCE_SUCCESS == _map_frag(glun, 10, pfid3));

	CU_ASSERT(SCE_SUCCESS == _freefraglist_get(gsce, &pfid));
	CU_ASSERT(SCE_SUCCESS == _map_frag(glun, 20, pfid));
}

static void test_unmap_frag(void)
{
	frag_t *frag;
	pfid_t pfid;

	pfid = glun->fragmaps[0][10];
	frag = _pfid2frag(gsce, pfid, NULL);

	CU_ASSERT(SCE_SUCCESS != _unmap_frag(NULL, 0));
	CU_ASSERT(SCE_SUCCESS != _unmap_frag(&gsce->luntbl[1], 10));
	CU_ASSERT(SCE_SUCCESS != _unmap_frag(glun, 30));

	frag->nr_service = 1;
	CU_ASSERT(SCE_SUCCESS != _unmap_frag(glun, 10));

	frag->nr_service = 0;
	CU_ASSERT(SCE_SUCCESS == _unmap_frag(glun, 10));

	CU_ASSERT((glun->fragmaps[0][10] & FRAGDESC_MAPPED) == 0);
	CU_ASSERT(frag->fragnum == FRAGNUM_WITHINFREELIST);
}

int addsuite_sce_mapping(void)
{
	CU_pSuite suite;

	suite = CU_add_suite("sce_mapping.c", _init_suite, _clean_suite);
	if (suite == NULL)
		return -1;

	ADDTEST(suite, test_map_frag);
	ADDTEST(suite, test_unmap_frag);

	return 0;
}
