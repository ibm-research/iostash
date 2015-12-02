#include <stdio.h>
#include <string.h>
#include "CUnit/Basic.h"

#define _TEST_MAIN_
#include "sce.h"

int addsuite_sce_fragment(void);
int addsuite_sce_cachedev(void);
int addsuite_sce_lun(void);
int addsuite_sce_util(void);
int addsuite_sce_freefraglist(void);
int addsuite_sce_misslog(void);
int addsuite_sce_mapping(void);
int addsuite_sce_eviction(void);
int addsuite_sce_population(void);
int addsuite_sce_interface(void);

int main()
{
	/* initialize the CUnit test registry */
	if (CUE_SUCCESS != CU_initialize_registry())
		goto errExit1;

	/* add a suite to the registry */
	if (addsuite_sce_fragment() < 0)
		goto errExit2;
	if (addsuite_sce_cachedev() < 0)
		goto errExit2;
	if (addsuite_sce_lun() < 0)
		goto errExit2;
	if (addsuite_sce_util() < 0)
		goto errExit2;
	if (addsuite_sce_freefraglist() < 0)
		goto errExit2;
	if (addsuite_sce_misslog() < 0)
		goto errExit2;
	if (addsuite_sce_mapping() < 0)
		goto errExit2;
	if (addsuite_sce_eviction() < 0)
		goto errExit2;
	if (addsuite_sce_population() < 0)
		goto errExit2;
	if (addsuite_sce_interface() < 0)
		goto errExit2;

	CU_basic_set_mode(CU_BRM_VERBOSE);
	CU_basic_run_tests();

errExit2:
	CU_cleanup_registry();

errExit1:
	return CU_get_error();
}
