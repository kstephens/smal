# SMAL
# Copyright (c) 2011 Kurt A. Stephens

INC_DIR=include/#

CFLAGS_OPT = -O2 #
CFLAGS_OPT = -O3 #
CFLAGS_PROF = -pg -DSMAL_PROF #
CFLAGS_PROF = #
PTHREAD_CFLAGS = -DSMAL_PTHREAD=1 #
CFLAGS = $(CFLAGS_OPT) $(CFLAGS_PROF) -g -Wall -Werror $(PTHREAD_CFLAGS) -I$(INC_DIR) #

H_FILES := $(shell echo $(INC_DIR)/smal/*.h) #
C_FILES := $(shell echo src/*.c) #
O_FILES := $(C_FILES:%.c=%.o) #
TESTS_C := $(shell echo t/*.c) #
TESTS_T := $(TESTS_C:%.c=%.t) #
SRC_LIB := src/libsmal.a #

all : $(SRC_LIB) $(TESTS_T)

src/smal.s : src/smal.c $(H_FILES)
	$(CC) $(CFLAGS) -S -o $@ $<

$(SRC_LIB) : $(O_FILES)
	rm -f $@
	ar -rs $@ $(O_FILES)

t/%.t : t/%.c $(SRC_LIB) $(H_FILES)
	$(CC) $(CFLAGS) -DSMAL_UNIT_TEST=1 -DSMAL_DEBUG=1 -o $@ $(@:%.t=%.c) $(SRC_LIB) 

clean :
	rm -rf *.s *.dSYM *.o *.a src/*.o src/*.a t/*.t t/*.dSYM

test : all $(TESTS_T)
	set -ex ;\
	for t in $(TESTS_T) ;\
	do \
	  $$t || gdb --args $$t ;\
	done
 