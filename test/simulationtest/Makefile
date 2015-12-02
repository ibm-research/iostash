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

all: 
	gcc -g -I$(SCE_DIR) -D_SCE_CROSSTEST -o sce_simultest sce_simultest.c $(SCE_SOURCES)

clean:
	rm -f sce_simultest

