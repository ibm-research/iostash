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

#ifndef __HELPERS_H__
#define __HELPERS_H__

/* SYSFS macros */
/* These are normally also found in include/linux/sysfs.h */


#ifndef __ATTR
#define __ATTR(_name,_mode,_show,_store) { \
    .attr = {.name = __stringify(_name), .mode = _mode },   \
        .show   = _show,                                        \
        .store  = _store,                                       \
	 }
#endif

#ifndef __ATTR_RO
#define __ATTR_RO(_name) { \
    .attr   = { .name = __stringify(_name), .mode = 0444 }, \
        .show   = _name##_show,                                 \
	 }
#endif

#ifndef __ATTR_RW
#define __ATTR_RW(_name) __ATTR(_name, 0644, _name##_show, _name##_store)
#endif

#ifndef __ATTR_NULL
#define __ATTR_NULL { .attr = { .name = NULL } }
#endif

#ifndef attr_name
#define attr_name(_attr) (_attr).attr.name
#endif

/* SYSFS macros nd here */


const char * _basename(const char *path);

#ifdef	DEBUG
#define DBG(format, arg...)				\
	do {						\
		printk(KERN_DEBUG "%s:%d: "format" \n", \
			__FUNCTION__, __LINE__, ##arg); \
	} while (0)
#else
#define DBG(format, arg...)				\
	do {						\
	} while (0)
#endif
#define ERR(format, arg...)				\
	do {						\
		printk(KERN_ERR "%s:%d: "format" \n",	\
			__FUNCTION__, __LINE__, ##arg); \
	} while (0)

#define MSG(format, arg...)				\
	do {						\
		printk(KERN_ALERT "%s:%d: "format" \n", \
			__FUNCTION__, __LINE__, ##arg); \
	} while (0)

#endif /* __HELPERS_H__ */
