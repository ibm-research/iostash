#
# Flash cache solution iostash
#
# Authors: Ioannis Koltsidas <iko@zurich.ibm.com>
#          Nikolas Ioannou   <nio@zurich.ibm.com>
#
# Copyright (c) 2014-2015, IBM Corporation. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
#
#

################################################################################
# kbuild part of makefile
################################################################################
ifneq ($(KERNELRELEASE),)

obj-m     +=	iostash.o
iostash-y +=	iostash_module.o \
		iostash_population.o \
		iostash_iohook.o \
		iostash_ssd.o \
		iostash_hdd.o \
		pdm.o \
		helpers.o \
		sce_cachedev.o \
		sce_eviction.o \
		sce_fragment.o \
		sce_freefraglist.o \
		sce_interface.o \
		sce_lun.o \
		sce_mapping.o \
		sce_misslog.o \
		sce_population.o \
		sce_util.o

# Note, PWD will point to COMPONENT_DIR when we enter here (2nd Makefile invocation)
EXTRA_CFLAGS := $(INCLUDES) -D_LINUX -g -Wall -Werror
EXTRA_CFLAGS += -I$(KERNEL_TREE)/drivers/md -I./ -I$(COMPONENT_DIR)/
EXTRA_CFLAGS += -I$(KERNEL_TREE)/include/ -I$(KERNEL_TREE)/include/linux

#CFLAGS_iostash_iohook.o += -DDEBUG
#CFLAGS_sce_cachedev.o += -DDEBUG
#CFLAGS_sce_lun.o += -DDEBUG
#CFLAGS_sce_util.o += -DDEBUG
#CFLAGS_sce_population.o += -DDEBUG
#CFLAGS_pdm.o += -DDEBUG
#CFLAGS_iostash_hdd.o += -DDEBUG
#CFLAGS_iostash_ssd.o += -DDEBUG


################################################################################
# Invoke kernel build system when called from command line
################################################################################
else

COMPONENT_DIR := $(shell pwd)
PROJ_DIR=$(COMPONENT_DIR)/..

SRC_FILES := $(wildcard *.c)

ifndef $(KERNEL_DIR)
include $(PROJ_DIR)/config/build.properties
HAVE_LOCAL_PROPS := $(wildcard $(PROJ_DIR)/config/build.local.properties)
ifneq ($(strip $(HAVE_LOCAL_PROPS)),)
include $(PROJ_DIR)/config/build.local.properties
endif
endif

KERNELDIR := ${KERNEL_DIR}

# Things we need to have in the build dir
COMPONENT_BUILD_DIR=$(PROJ_DIR)/$(OBJ_BUILD_DIR)/$(COMPONENT)
COMPONENT_BUILD_MAKEFILE=$(addprefix $(COMPONENT_BUILD_DIR), Makefile)

default: $(COMPONENT_BUILD_DIR) c_file_links $(COMPONENT_BUILD_MAKEFILE)
	$(MAKE) -C $(KERNELDIR) M=$(COMPONENT_BUILD_DIR) COMPONENT_DIR=$(COMPONENT_DIR) modules

# Create the build directory
$(COMPONENT_BUILD_DIR):
	$(MKDIR) -p $@

# Link required files into build directory
c_file_links: $(SRC_FILES) $(COMPONENT_BUILD_DIR)
	$(LN) -f $(COMPONENT_DIR)/*.c $(COMPONENT_BUILD_DIR)

$(COMPONENT_BUILD_MAKEFILE): Makefile $(COMPONENT_BUILD_DIR)
	$(LN) -f $(COMPONENT_DIR)/$< $@

install:
	$(MAKE) -C $(KERNELDIR) M=$(COMPONENT_BUILD_DIR) COMPONENT_DIR=$(COMPONENT_DIR) modules_install

# Cleanup
clean:
	$(RM) $(COMPONENT_BUILD_DIR)

.PHONY: clean c_file_links install
endif

