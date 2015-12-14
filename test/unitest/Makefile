TEST_DIR := $(shell pwd)/

SCE_DIR     = $(TEST_DIR)../../src/

SCE_SOURCES =	$(SCE_DIR)sce_fragment.c \
		$(SCE_DIR)sce_cachedev.c \
		$(SCE_DIR)sce_util.c \
		$(SCE_DIR)sce_mapping.c \
		$(SCE_DIR)sce_eviction.c \
		$(SCE_DIR)sce_freefraglist.c \
		$(SCE_DIR)sce_lun.c \
		$(SCE_DIR)sce_population.c \
		$(SCE_DIR)sce_misslog.c \
		$(SCE_DIR)sce_interface.c

SCE_TESTS   =   $(TEST_DIR)sce_fragment_test.c \
		$(TEST_DIR)sce_cachedev_test.c \
		$(TEST_DIR)sce_lun_test.c \
		$(TEST_DIR)sce_util_test.c \
		$(TEST_DIR)sce_freefraglist_test.c \
		$(TEST_DIR)sce_misslog_test.c \
		$(TEST_DIR)sce_mapping_test.c \
		$(TEST_DIR)sce_eviction_test.c \
		$(TEST_DIR)sce_population_test.c \
		$(TEST_DIR)sce_interface_test.c

all:
	gcc -g -I$(SCE_DIR) -o sce_test sce_test.c $(SCE_SOURCES) $(SCE_TESTS) -lcunit

clean:
	rm -rf sce_test sce_test.dSYM

