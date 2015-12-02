PROJ_DIR=$(shell pwd)
include $(PROJ_DIR)/config/build.properties
HAVE_LOCAL_PROPS := $(wildcard $(PROJ_DIR)/config/build.local.properties)
ifneq ($(strip $(HAVE_LOCAL_PROPS)),)
include $(PROJ_DIR)/config/build.local.properties
endif

KERNEL_VERSION=$(shell uname -r)

all:
	$(MAKE) core

core:
	cd $(PROJ_DIR)/src       && $(MAKE)

$(PROJ_DIR)/${LIB_BUILD_DIR}:
	$(MKDIR) -p $@

install:
	cd $(PROJ_DIR)/src       && sudo $(MAKE) install
	sudo install -o root -g root -m 0755 scripts/10-iostash.rules /etc/udev/rules.d/
	sudo install -o root -g root -m 0755 scripts/iostash  $(DESTDIR)/usr/bin/
	sudo depmod -a

tests:
	cd $(PROJ_DIR)/$(UNIT_TEST_DIR)/kernel/pdm && $(MAKE)

clean:
	cd $(PROJ_DIR)/src       && $(MAKE) clean
