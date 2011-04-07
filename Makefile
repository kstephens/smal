# SMAL
# Copyright (c) 2011 Kurt A. Stephens

INC_DIR=include/#

PTHREAD_CFLAGS = -DSMAL_PTHREAD=1 #
CFLAGS = -O2 -g -Wall -Werror $(PTHREAD_CFLAGS) -I$(INC_DIR) #

INCLUDES := $(shell echo $(INC_DIR)/smal/*.h) #
CFILES := $(shell echo src/*.c) #

TESTS_C := $(shell echo t/*.c) #
TESTS_T := $(TESTS_C:%.c=%.t) #

all : $(TESTS_T)

src/smal.s : src/smal.c $(INCLUDES)
	$(CC) $(CFLAGS) -S -o $@ $<

t/%.t : t/%.c $(CFILES) $(INCLUDES)
	$(CC) $(CFLAGS) -DSMAL_UNIT_TEST=1 -DSMAL_DEBUG=1 -o $@ $(@:.t=.c) $(CFILES)

clean :
	rm -rf *.s *.dSYM *.o *.a src/*.o src/*.a t/*.t t/*.dSYM

test : all $(TESTS_T)
	set -ex ;\
	for t in $(TESTS_T) ;\
	do \
	  $$t || gdb --args $$t ;\
	done
 