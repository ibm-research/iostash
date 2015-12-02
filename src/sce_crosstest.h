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


#ifndef _SCE_CROSSTEST_H
#define _SCE_CROSSTEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

/* type definitions */
typedef signed char int8_t;
typedef short       int16_t;
typedef int         int32_t;
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

#if __APPLE__
typedef int pthread_spinlock_t;


#ifdef _TEST_MAIN_

int pthread_spin_init(pthread_spinlock_t *lock, int pshared)
{
	__asm__ __volatile__ ("" ::: "memory");
	*lock = 0;
	return 0;
}

int pthread_spin_destroy(pthread_spinlock_t *lock)
{
	return 0;
}

int pthread_spin_lock(pthread_spinlock_t *lock)
{
	while (1) {
		int i;

		for (i=0; i < 10000; i++) {
			if (__sync_bool_compare_and_swap(lock, 0, 1)) {
				return 0;
			}
		}
		sched_yield();
	}
}

int pthread_spin_trylock(pthread_spinlock_t *lock)
{
	if (__sync_bool_compare_and_swap(lock, 0, 1)) {
		return 0;
	}
	return -1;
}

int pthread_spin_unlock(pthread_spinlock_t *lock)
{
	__asm__ __volatile__ ("" ::: "memory");
	*lock = 0;
	return 0;
}
#else

int pthread_spin_init(pthread_spinlock_t *lock, int pshared);
int pthread_spin_destroy(pthread_spinlock_t *lock);
int pthread_spin_lock(pthread_spinlock_t *lock);
int pthread_spin_trylock(pthread_spinlock_t *lock);
int pthread_spin_unlock(pthread_spinlock_t *lock);

#endif /* _TEST_MAIN_ */

#endif /* __APPLE__ */

typedef uint64_t sector_t;
typedef pthread_spinlock_t spinlock_t;
typedef int bool;

#define BUG_ON(cond)		assert(!(cond))

#define kmalloc(a, b)           malloc(a)
#define kfree(a)                free(a)

#define spin_lock_init(a)	pthread_spin_init(a, PTHREAD_PROCESS_SHARED)
#define spin_lock_irqsave(a, b) pthread_spin_lock(a)
#define spin_unlock_irqrestore(a, b) pthread_spin_unlock(a)

#define true			(1)
#define false			(0)


#define ADDTEST(s, func) {if (CU_add_test(s, "\""#func"\"", func) == NULL) \
				return -1;}

#endif /* _SCE_CROSSTEST_H */
