#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "CUnit/Basic.h"

#include "sce.h"
#include "sce_internal.h"

#define RANDTEST_COUNT                  (10000)
#define RANDTEST_UPDATE                 (1000)
#define RANDTEST_VALID                  (RANDTEST_COUNT / RANDTEST_UPDATE)

static void test_frag_init(void)
{
	frag_t frag;

	_frag_init(&frag);

	CU_ASSERT(SCE_PAGEPERFRAG == frag.nr_valid);
}

/* Compare real bitmap to simple-bitmap (char array) 
   for verification */
static int _chk_bitmap(bitmap_t * bitmap1, char *bitmap2)
{
	int off;
	int idx;
	int i;
	int b1, b2;

	for (i = 0; i < SCE_PAGEPERFRAG; i++) {
		idx = i / BITMAPENT_SIZE;
		off = i % BITMAPENT_SIZE;

		b1 = (bitmap1[idx] & (1 << off)) ? 1 : 0;
		b2 = (bitmap2[i] > 0) ? 1 : 0;
		if (b1 != b2)
			return -1;
	}
	return 0;
}

static int _count_bitmap(char* bitmap)
{
	int i, cnt = 0;

	for (i = 0; i < SCE_PAGEPERFRAG; i++)
	{
		if (bitmap[i])
			cnt++;
	}
	return cnt;
}


static void test_set_bitmap(void)
{
	frag_t   frag;
	char     bitmap2[SCE_PAGEPERFRAG];
	uint32_t pagenum;
	uint32_t pagecnt;
	int      i, j, k;

	srand(0);

	memset(bitmap2, 0, SCE_PAGEPERFRAG);
	_frag_init(&frag);

	for (i = 0; i < RANDTEST_COUNT; i++) {
		/* generate random page number and count */
		pagenum = rand() % SCE_PAGEPERFRAG;
		pagecnt = (rand() % (SCE_PAGEPERFRAG - pagenum)) + 1;

		for (j = k = 0; j < pagecnt; j++) {
			if (bitmap2[pagenum + j] == 0) {
				bitmap2[pagenum + j] = 1;
				k++;
			}
		}
		j = _set_bitmap(frag.bitmap, pagenum, pagecnt);
		if (j != k)
			break;

		if (_chk_bitmap(frag.bitmap, bitmap2) < 0)
			break;
	}

	CU_ASSERT(i == RANDTEST_COUNT);
}

static void test_reset_bitmap(void)
{
	frag_t   frag;
	char     bitmap2[SCE_PAGEPERFRAG];
	uint32_t pagenum;
	uint32_t pagecnt;
	int      i, j, k;

	srand(0);

	memset(bitmap2, 1, SCE_PAGEPERFRAG);

	_frag_init(&frag);
	memset(frag.bitmap, 0xff, sizeof(frag.bitmap));

	for (i = 0; i < RANDTEST_COUNT; i++) {
		pagenum = rand() % SCE_PAGEPERFRAG;
		pagecnt = (rand() % (SCE_PAGEPERFRAG - pagenum)) + 1;

		for (j = k = 0; j < pagecnt; j++) {
			if (bitmap2[pagenum + j] == 1) {
				bitmap2[pagenum + j] = 0;
				k++;
			}
		}
		j = _reset_bitmap(frag.bitmap, pagenum, pagecnt);
		if (j != k)
			break;

		if (_chk_bitmap(frag.bitmap, bitmap2) < 0)
			break;
	}

	CU_ASSERT(i == RANDTEST_COUNT);
}

static void test_frag_invalidate(void)
{
	frag_t   frag;
	char     bitmap2[SCE_PAGEPERFRAG];
	uint32_t pagenum;
	uint32_t pagecnt;
	int      i, j, k;
#ifdef SCE_AWT
	awtbt_t  awtbt;
#endif

	srand(0);

	memset(bitmap2, 0, SCE_PAGEPERFRAG);
	_frag_init(&frag);

	for (i = 0; i < RANDTEST_COUNT; i++) {
		pagenum = rand() % SCE_PAGEPERFRAG;
		pagecnt = (rand() % (SCE_PAGEPERFRAG - pagenum)) + 1;

		for (j = k = 0; j < pagecnt; j++) {
			if (bitmap2[pagenum + j] == 0) {
				bitmap2[pagenum + j] = 1;
				k++;
			}
		}
		j = _frag_invalidate(&frag, pagenum, pagecnt);
		if (j != k)
			break;

		if (_chk_bitmap(frag.bitmap, bitmap2) < 0)
			break;

		if (frag.nr_valid !=
		    (SCE_PAGEPERFRAG - _count_bitmap(bitmap2))) break;
	}
	CU_ASSERT(i == RANDTEST_COUNT);

#ifdef SCE_AWT
	memset(&awtbt, 0, sizeof(awtbt));

	_frag_init(&frag);
	frag.pending_awt = &awtbt;

	CU_ASSERT(_frag_invalidate(&frag, 0, 4) == 4);
	CU_ASSERT(_frag_invalidate(&frag, 0, 4) == 0);
	CU_ASSERT(frag.nr_valid == SCE_PAGEPERFRAG - 4);

	_setabit(awtbt.bitmap, 3);
	_setabit(awtbt.bitmap, 4);
	CU_ASSERT(_getabit(awtbt.bitmap, 2) == 0);
	CU_ASSERT(_getabit(frag.bitmap,  2) == 1);
	CU_ASSERT(_getabit(awtbt.bitmap, 3) == 1);
	CU_ASSERT(_getabit(frag.bitmap,  3) == 1);
	CU_ASSERT(_getabit(awtbt.bitmap, 4) == 1);
	CU_ASSERT(_getabit(frag.bitmap,  4) == 0);
	CU_ASSERT(_getabit(awtbt.bitmap, 5) == 0);
	CU_ASSERT(_getabit(frag.bitmap,  5) == 0);

	CU_ASSERT(_frag_invalidate(&frag, 2, 4) == 1);

	CU_ASSERT(_getabit(awtbt.bitmap, 2) == 0);
	CU_ASSERT(_getabit(frag.bitmap,  2) == 1);
	CU_ASSERT(_getabit(awtbt.bitmap, 3) == 1);
	CU_ASSERT(_getabit(frag.bitmap,  3) == 0);
	CU_ASSERT(_getabit(awtbt.bitmap, 4) == 1);
	CU_ASSERT(_getabit(frag.bitmap,  4) == 0);
	CU_ASSERT(_getabit(awtbt.bitmap, 5) == 0);
	CU_ASSERT(_getabit(frag.bitmap,  5) == 1);
#endif

}

static void test_frag_isvalid(void)
{
	frag_t   frag;
	char     bitmap2[SCE_PAGEPERFRAG];
	uint32_t pagenum;
	uint32_t pagecnt;
	int      i, j, k;
#ifdef SCE_AWT
	awtbt_t	 awtbt;
#endif

	srand(0);

	memset(bitmap2, 0, SCE_PAGEPERFRAG);

	_frag_init(&frag);

	for (i = 0; i < RANDTEST_UPDATE; i++) {
		pagenum = rand() % SCE_PAGEPERFRAG;
		pagecnt = (rand() % (SCE_PAGEPERFRAG - pagenum)) + 1;

		for (j = k = 0; j < pagecnt; j++) {
			if (bitmap2[pagenum + j] == 0) {
				bitmap2[pagenum + j] = 1;
				k++;
			}
		}
		j = _frag_invalidate(&frag, pagenum, pagecnt);
		if (j != k)
			break;

		if (_chk_bitmap(frag.bitmap, bitmap2) < 0)
			break;

		for (j = 0; j < RANDTEST_VALID; j++) {
			pagenum = rand() % SCE_PAGEPERFRAG;
			pagecnt = (rand() % (SCE_PAGEPERFRAG - pagenum)) + 1;

			for (k = 0; k < pagecnt; k++) {
				if (bitmap2[pagenum + k] > 0)
					break;
			}

			if (_frag_isvalid(&frag, pagenum, pagecnt) ==
			    SCE_SUCCESS) {
				if (k < pagecnt)
					break;
			} else {
				if (k == pagecnt)
					break;
			}
		}

		if (j < RANDTEST_VALID)
			break;
	}
	CU_ASSERT(i == RANDTEST_UPDATE);

#ifdef SCE_AWT
	_frag_init(&frag);

	CU_ASSERT(_frag_isvalid(&frag, 0, 1) == SCE_SUCCESS);

	frag.pending_awt = &awtbt;
	memset(awtbt.bitmap, 0xff, sizeof(frag.bitmap));

	CU_ASSERT(_frag_isvalid(&frag, 0, 1) != SCE_SUCCESS);
#endif

}

#ifdef SCE_AWT
static void test_getabit(void)
{
	frag_t   frag;
	char     bitmap2[SCE_PAGEPERFRAG];
	uint32_t pagenum;
	uint32_t pagecnt;
	int      i;

	srand(0);

	memset(bitmap2, 0, SCE_PAGEPERFRAG);
	_frag_init(&frag);
	while (frag.nr_valid > (SCE_PAGEPERFRAG / 2)) {
		pagenum = rand() % SCE_PAGEPERFRAG;
		pagecnt = (rand() % (SCE_PAGEPERFRAG - pagenum)) + 1;

		for (i = 0; i < pagecnt; i++)
			if (bitmap2[pagenum + i] == 0)
				bitmap2[pagenum + i] = 1;
		_frag_invalidate(&frag, pagenum, pagecnt);
	}

	for (i = 0; i < SCE_PAGEPERFRAG; i++) {
		CU_ASSERT(_getabit(frag.bitmap, i) == bitmap2[i]);
	}
}

static void test_setabit(void)
{
	frag_t   frag;
	char     bitmap2[SCE_PAGEPERFRAG];
	uint32_t pagenum;
	int      i;

	srand(0);

	memset(bitmap2, 0, SCE_PAGEPERFRAG);
	_frag_init(&frag);

	for (i = 0; i < RANDTEST_COUNT; i++) {
		/* generate random page number and count */
		pagenum = rand() % SCE_PAGEPERFRAG;

		bitmap2[pagenum] = 1;
		_setabit(frag.bitmap, pagenum);

		if (_chk_bitmap(frag.bitmap, bitmap2) < 0)
			break;
	}

	CU_ASSERT(i == RANDTEST_COUNT);
}

static void test_resetabit(void)
{
	frag_t   frag;
	char     bitmap2[SCE_PAGEPERFRAG];
	uint32_t pagenum;
	int      i;

	srand(0);

	memset(bitmap2, 1, SCE_PAGEPERFRAG);
	_frag_init(&frag);
	memset(frag.bitmap, 0xff, sizeof(frag.bitmap));

	for (i = 0; i < RANDTEST_COUNT; i++) {
		/* generate random page number and count */
		pagenum = rand() % SCE_PAGEPERFRAG;

		bitmap2[pagenum] = 0;
		_resetabit(frag.bitmap, pagenum);

		if (_chk_bitmap(frag.bitmap, bitmap2) < 0)
			break;
	}

	CU_ASSERT(i == RANDTEST_COUNT);
}

static void test_frag_writestart(void)
{
	frag_t   frag;
	awtbt_t  awtbt;

	_frag_init(&frag);
	memset(awtbt.bitmap, 0, sizeof(frag.bitmap));

	CU_ASSERT(_frag_isvalid(&frag, 0, 1) == SCE_SUCCESS)
	CU_ASSERT(_frag_writestart(&frag, 0, 1) == SCE_ERROR)
	CU_ASSERT(_frag_isvalid(&frag, 0, 1) != SCE_SUCCESS)

	frag.pending_awt = &awtbt;

	CU_ASSERT(_frag_writestart(&frag, 0, 3) == SCE_SUCCESS)
	CU_ASSERT(_frag_isvalid(&frag, 0, 1) != SCE_SUCCESS)
	CU_ASSERT(_frag_isvalid(&frag, 1, 1) != SCE_SUCCESS)
	CU_ASSERT(_frag_isvalid(&frag, 2, 1) != SCE_SUCCESS)
	CU_ASSERT(_frag_isvalid(&frag, 3, 1) == SCE_SUCCESS)

	CU_ASSERT(_getabit(frag.bitmap,  0) == 1)
	CU_ASSERT(_getabit(awtbt.bitmap, 0) == 1)
	CU_ASSERT(_getabit(frag.bitmap,  1) == 1)
	CU_ASSERT(_getabit(awtbt.bitmap, 1) == 1)
	CU_ASSERT(_getabit(frag.bitmap,  2) == 1)
	CU_ASSERT(_getabit(awtbt.bitmap, 2) == 1)
	CU_ASSERT(_getabit(frag.bitmap,  3) == 0)
	CU_ASSERT(_getabit(awtbt.bitmap, 3) == 0)

	CU_ASSERT(frag.nr_valid == (SCE_PAGEPERFRAG - 3))
	CU_ASSERT(_frag_writestart(&frag, 0, 1) != SCE_SUCCESS)
	CU_ASSERT(_getabit(frag.bitmap,  0) == 0)
	CU_ASSERT(_getabit(awtbt.bitmap, 0) == 1)
	CU_ASSERT(frag.nr_valid == (SCE_PAGEPERFRAG - 3))

	CU_ASSERT(_frag_writestart(&frag, 0, 4) == SCE_SUCCESS)
	CU_ASSERT(frag.nr_valid == (SCE_PAGEPERFRAG - 4))
	CU_ASSERT(_getabit(frag.bitmap,  0) == 0)
	CU_ASSERT(_getabit(awtbt.bitmap, 0) == 1)
	CU_ASSERT(_getabit(frag.bitmap,  1) == 0)
	CU_ASSERT(_getabit(awtbt.bitmap, 1) == 1)
	CU_ASSERT(_getabit(frag.bitmap,  2) == 0)
	CU_ASSERT(_getabit(awtbt.bitmap, 2) == 1)
	CU_ASSERT(_getabit(frag.bitmap,  3) == 1)
	CU_ASSERT(_getabit(awtbt.bitmap, 3) == 1)
}

static void test_frag_writeend(void)
{
	frag_t   frag;
	awtbt_t  awtbt;

	_frag_init(&frag);
	memset(awtbt.bitmap, 0, sizeof(frag.bitmap));
	frag.pending_awt = &awtbt;

	CU_ASSERT(frag.nr_valid == SCE_PAGEPERFRAG)
	CU_ASSERT(_frag_writestart(&frag, 0, 2) == SCE_SUCCESS)
	CU_ASSERT(_frag_writestart(&frag, 0, 1) == SCE_ERROR)

	CU_ASSERT(_getabit(frag.bitmap,  0) == 0)
	CU_ASSERT(_getabit(awtbt.bitmap, 0) == 1)
	CU_ASSERT(_getabit(frag.bitmap,  1) == 1)
	CU_ASSERT(_getabit(awtbt.bitmap, 1) == 1)
	CU_ASSERT(frag.nr_valid == (SCE_PAGEPERFRAG - 2))

	_frag_writeend(&frag, 0, 2, 0);
	CU_ASSERT(_getabit(frag.bitmap,  0) == 0)
	CU_ASSERT(_getabit(awtbt.bitmap, 0) == 1)
	CU_ASSERT(_getabit(frag.bitmap,  1) == 0)
	CU_ASSERT(_getabit(awtbt.bitmap, 1) == 0)
	CU_ASSERT(frag.nr_valid == (SCE_PAGEPERFRAG - 1))

	_frag_init(&frag);
	memset(awtbt.bitmap, 0, sizeof(frag.bitmap));
	frag.pending_awt = &awtbt;

	CU_ASSERT(frag.nr_valid == SCE_PAGEPERFRAG)
	CU_ASSERT(_frag_writestart(&frag, 0, 2) == SCE_SUCCESS)
	CU_ASSERT(_frag_writestart(&frag, 0, 1) == SCE_ERROR)
	CU_ASSERT(frag.nr_valid == (SCE_PAGEPERFRAG - 2))

	_frag_writeend(&frag, 0, 2, 1);
	CU_ASSERT(_getabit(frag.bitmap,  0) == 0)
	CU_ASSERT(_getabit(awtbt.bitmap, 0) == 1)
	CU_ASSERT(_getabit(frag.bitmap,  1) == 1)
	CU_ASSERT(_getabit(awtbt.bitmap, 1) == 0)
	CU_ASSERT(frag.nr_valid == (SCE_PAGEPERFRAG - 2))
}

static void test_frag_mergebitmap(void)
{
	frag_t   frag;
	awtbt_t  awtbt;

	_frag_init(&frag);
	memset(awtbt.bitmap, 0, sizeof(frag.bitmap));
	frag.pending_awt = &awtbt;

	CU_ASSERT(frag.nr_valid == SCE_PAGEPERFRAG)
	CU_ASSERT(_frag_writestart(&frag, 0, 8) == SCE_SUCCESS)
	CU_ASSERT(_frag_writestart(&frag, 0, 4) == SCE_ERROR)
	CU_ASSERT(frag.nr_valid == (SCE_PAGEPERFRAG - 8))

	_frag_writeend(&frag, 0, 8, 0);

	CU_ASSERT(frag.nr_valid == (SCE_PAGEPERFRAG - 4))

	_frag_mergebitmap(&frag);
	frag.pending_awt = NULL;

	CU_ASSERT(frag.nr_valid == (SCE_PAGEPERFRAG - 4))
	CU_ASSERT(_frag_isvalid(&frag, 4, 4) == SCE_SUCCESS)
	CU_ASSERT(_frag_isvalid(&frag, 0, 4) != SCE_SUCCESS)
}

#endif

int addsuite_sce_fragment(void)
{
	CU_pSuite suite;

	suite = CU_add_suite("sce_fragment.c", NULL, NULL);
	if (suite == NULL)
		return -1;

	ADDTEST(suite, test_frag_init);
	ADDTEST(suite, test_set_bitmap);
	ADDTEST(suite, test_reset_bitmap);
	ADDTEST(suite, test_frag_invalidate);
	ADDTEST(suite, test_frag_isvalid);

#ifdef SCE_AWT
	ADDTEST(suite, test_getabit);
	ADDTEST(suite, test_setabit);
	ADDTEST(suite, test_resetabit);
	ADDTEST(suite, test_frag_writestart);
	ADDTEST(suite, test_frag_writeend);
	ADDTEST(suite, test_frag_mergebitmap);
#endif
	return 0;
}
