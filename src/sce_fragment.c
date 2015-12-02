/*
 * Flash cache solution iostash
 *
 * Authors: Ioannis Koltsidas <iko@zurich.ibm.com>
 *          Nikolas Ioannou   <nio@zurich.ibm.com>
 *
 * Copyright (c) 2014-2015, IBM Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */


#include "sce.h"
#include "sce_internal.h"

/* ---------------------------------------------------------------------------------------- */
/*                                  Fragment related                                        */
/* ---------------------------------------------------------------------------------------- */

/*
    initialize a fragment structure before mapping 
 */
int _frag_init(frag_t * frag)
{
	int ret = SCE_ERROR;

	if (frag) {
		memset(frag, 0, sizeof(frag_t));
		frag->nr_valid = SCE_PAGEPERFRAG;
		ret = SCE_SUCCESS;
	}
	return ret;
}

int _set_bitmap(bitmap_t * bitmap, uint32_t pgnum, uint32_t pgcnt)
{
	bitmap_t mask;
	bitmap_t b;
	int nr_set;

	bitmap += (pgnum / BITMAPENT_SIZE);
	mask = (bitmap_t) 1 << (pgnum % BITMAPENT_SIZE);

	for (nr_set = 0; pgcnt > 0;) {
		for (b = *bitmap; (mask > 0) && (pgcnt > 0);
		     mask <<= 1, pgcnt--) {
			if ((b & mask) == 0) {
				nr_set++;
				b |= mask;
			}
		}
		*bitmap++ = b;
		mask = (bitmap_t) 1;
	}
	return nr_set;
}


int _reset_bitmap(bitmap_t * bitmap, uint32_t pgnum, uint32_t pgcnt)
{
	bitmap_t mask;
	bitmap_t b;
	int nr_reset;

	bitmap += (pgnum / BITMAPENT_SIZE);
	mask = (bitmap_t) 1 << (pgnum % BITMAPENT_SIZE);

	for (nr_reset = 0; pgcnt > 0;) {
		for (b = *bitmap; (mask > 0) && (pgcnt > 0);
		     mask <<= 1, pgcnt--) {
			if ((b & mask) != 0) {
				nr_reset++;
				b &= ~mask;
			}
		}
		*bitmap++ = b;
		mask = (bitmap_t) 1;
	}
	return nr_reset;
}

/*
    set bitmap for given page range, number of invalidated pages are returned.
*/
int _frag_invalidate(frag_t * frag, uint32_t pgnum, uint32_t pgcnt)
{
	int nr_invalidated;

	BUG_ON(!frag);
	BUG_ON(pgcnt == 0);
	BUG_ON((pgnum + pgcnt) > SCE_PAGEPERFRAG);

#ifndef SCE_AWT
	nr_invalidated = _set_bitmap(frag->bitmap, pgnum, pgcnt);
#else
	if (!frag->pending_awt) {
		nr_invalidated = _set_bitmap(frag->bitmap, pgnum, pgcnt);
	} else {
		nr_invalidated = _set_bitmap4awt(frag->bitmap,
				frag->pending_awt->bitmap, pgnum, pgcnt);
	}
#endif

#ifdef _LINUX
	if (frag->nr_valid < nr_invalidated)
	{
		printk(":::: Error: frag->nr_valid = %d, nr_invalidated = %d\n",
		       frag->nr_valid, nr_invalidated);
	}
#endif


	BUG_ON(frag->nr_valid < nr_invalidated);
	frag->nr_valid -= (uint16_t) nr_invalidated;

	return nr_invalidated;
}

PRIVATE int _check_bitmap(bitmap_t* bitmap, uint32_t pgnum, uint32_t pgcnt)
{
	uint32_t offset;
	bitmap_t mask;
	uint32_t nr_check;

	/* move to the bitmap entry */
	bitmap += pgnum / BITMAPENT_SIZE;
	offset  = pgnum % BITMAPENT_SIZE;

	/* One page checking: perhaps the most common case */
	if (pgcnt == 1) {
		/* one bit checking */
		return ((*bitmap) & ((bitmap_t) 1 << offset)) ?
			SCE_ERROR : SCE_SUCCESS;
	}
	/* check bitmap entry by entry */
	while (pgcnt > 0) {
		/* prepare bit mask for checking */
		nr_check = BITMAPENT_SIZE - offset;
		mask = (bitmap_t) (-1);
		if (pgcnt < nr_check) {
			mask >>= (BITMAPENT_SIZE - pgcnt);
			nr_check = pgcnt;
		}
		mask <<= offset;

		/* if any bit in the bit mask is non-zero, invalid */
		if (*bitmap++ & mask)
			break;

		/* move to next bitmap entry */
		pgcnt -= nr_check;
		offset = 0;
	}
	return (pgcnt > 0) ? SCE_ERROR : SCE_SUCCESS;
}

int _frag_isvalid(frag_t * frag, uint32_t pgnum, uint32_t pgcnt)
{
	int ret;

	BUG_ON(!frag);
	BUG_ON(pgcnt == 0);
	BUG_ON((pgnum + pgcnt) > SCE_PAGEPERFRAG);

	/* if there is nothing invalid, we don't have to check bitmap */
	ret = _check_bitmap(frag->bitmap, pgnum, pgcnt);

#ifdef SCE_AWT
	if ((ret == SCE_SUCCESS) && (frag->pending_awt))
		ret = _check_bitmap(frag->pending_awt->bitmap,
				    pgnum, pgcnt);
#endif
	return ret;
}

#ifdef SCE_AWT

PRIVATE int _getabit(bitmap_t* bitmap, uint32_t pgnum)
{
	uint32_t offset;

	bitmap += pgnum / BITMAPENT_SIZE;
	offset  = pgnum % BITMAPENT_SIZE;

	return ((*bitmap) & ((bitmap_t) 1 << offset)) ? 1 : 0;
}

PRIVATE void _setabit(bitmap_t* bitmap, uint32_t pgnum)
{
	uint32_t offset;

	bitmap += pgnum / BITMAPENT_SIZE;
	offset  = pgnum % BITMAPENT_SIZE;

	*bitmap |= (bitmap_t) (1 << offset);
}

PRIVATE void _resetabit(bitmap_t* bitmap, uint32_t pgnum)
{
	uint32_t offset;

	bitmap += pgnum / BITMAPENT_SIZE;
	offset  = pgnum % BITMAPENT_SIZE;

	*bitmap &= ~((bitmap_t) (1 << offset));
}

int _set_bitmap4awt(bitmap_t *bitmap0, bitmap_t *bitmap1,
		    uint32_t pgnum,    uint32_t pgcnt)
{
	int nr_set;
	int b0, b1;

	for (nr_set = 0; pgcnt > 0; pgnum++, pgcnt--) {
		b0 = _getabit(bitmap0, pgnum);
		b1 = _getabit(bitmap1, pgnum);

		if ((b0 == 0) && (b1 == 0))
		{
			_setabit(bitmap0, pgnum);
			nr_set++;
		} else if ((b0 == 1) && (b1 == 1)) {
			_resetabit(bitmap0, pgnum);
		}
	}
	return nr_set;
}

int _frag_writestart(frag_t *frag, uint32_t pgnum, uint32_t pgcnt)
{
	bitmap_t *bitmap0;
	bitmap_t *bitmap1;
	int       validwrite;

	BUG_ON(!frag);
	BUG_ON(pgcnt == 0);
	BUG_ON((pgnum + pgcnt) > SCE_PAGEPERFRAG);

	if (!frag->pending_awt) {
		_frag_invalidate(frag, pgnum, pgcnt);
		return SCE_ERROR;
	}

	bitmap0 = frag->bitmap;
	bitmap1 = frag->pending_awt->bitmap;

	validwrite = 0;

	for (; pgcnt > 0; pgnum++, pgcnt--) {
		switch ( _getabit(bitmap0, pgnum) |
			(_getabit(bitmap1, pgnum) << 1)) {
		case 0: /* 00  --> 11 */
			_setabit(bitmap0, pgnum);
			_setabit(bitmap1, pgnum);
			frag->nr_valid--;
			validwrite++;
			break;
		case 1: /* 01  --> 11 */
			_setabit(bitmap1, pgnum);
			validwrite++;
			break;

		/* case 2: 10 --> 10 */

		case 3: /* 11  --> 10 */
			_resetabit(bitmap0, pgnum);
			break;
		}
	}

	return (validwrite) ? SCE_SUCCESS : SCE_ERROR;
}

void _frag_writeend(frag_t *frag, uint32_t pgnum, uint32_t pgcnt, bool failed)
{
	bitmap_t *bitmap0;
	bitmap_t *bitmap1;

	BUG_ON(!frag);
	BUG_ON(pgcnt == 0);
	BUG_ON((pgnum + pgcnt) > SCE_PAGEPERFRAG);
	BUG_ON(!frag->pending_awt);

	bitmap0 = frag->bitmap;
	bitmap1 = frag->pending_awt->bitmap;

	for (; pgcnt > 0; pgnum++, pgcnt--) {
		BUG_ON(!_getabit(bitmap1, pgnum));

		if (_getabit(bitmap0, pgnum) == 1) {
			if (!failed) {
				_resetabit(bitmap0, pgnum);
				_resetabit(bitmap1, pgnum);
				frag->nr_valid++;
			} else {
				_resetabit(bitmap1, pgnum);
			}
		}
	}
}

void _frag_mergebitmap(frag_t *frag)
{
	bitmap_t *bitmap0;
	bitmap_t *bitmap1;
	int       i;

	BUG_ON(!frag);

	bitmap0 = frag->bitmap;
	bitmap1 = frag->pending_awt->bitmap;

	BUG_ON(!bitmap1);

	for (i = 0; i < BITMAPARRAYSIZE; i++) {
		*bitmap0++ |= (*bitmap1++);
	}
}

#endif

