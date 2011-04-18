# SMAL
# Copyright (c) 2011 Kurt A. Stephens

INC_DIR=include/#

CFLAGS_OPT = -O2 #
CFLAGS_OPT = -O3 #
#CFLAGS_OPT = #
CFLAGS_PROF = -pg -DSMAL_PROF #
CFLAGS_PROF = #
CFLAGS_THREAD = -DSMAL_PTHREAD=1 #
# CFLAGS_THREAD += -DSMAL_THREAD_MUTEX_DEBUG=3 #
CFLAGS = $(CFLAGS_OPT) $(CFLAGS_PROF) -g -Wall -Werror -DSMAL_DEBUG=1 $(CFLAGS_THREAD) -I$(INC_DIR) -Isrc #

H_FILES := $(shell echo $(INC_DIR)/smal/*.h) #
C_FILES := $(shell echo src/*.c) src/hash/voidP_voidP_Table.c #
O_FILES := $(C_FILES:%.c=%.o) #
TESTS_C := $(shell echo t/*.c) #
TESTS_T := $(TESTS_C:%.c=%.t) #
SRC_LIB := src/libsmal.a #

all : $(SRC_LIB) $(TESTS_T)

src/smal.s : src/smal.c $(H_FILES)
	$(CC) $(CFLAGS) -DNASSERT=1 -S -o $@ $<

src/hash/voidP_voidP_Table.o : src/hash/*.c src/hash/*.h src/hash/*.def

$(SRC_LIB) : $(O_FILES)
	rm -f $@
	ar -rs $@ $(O_FILES)

t/%.t : t/%.c $(SRC_LIB) $(H_FILES)
	$(CC) $(CFLAGS:$(CFLAGS_OPT)=) -I./t -DSMAL_UNIT_TEST=1 -DSMAL_DEBUG=1 -o $@ $(@:%.t=%.c) $(SRC_LIB) 

clean :
	rm -rf *.s *.dSYM *.o *.a src/*.o src/*.a t/*.t t/*.dSYM

test : all $(TESTS_T)
	set -ex ;\
	for t in $(TESTS_T) ;\
	do \
	  time $$t || gdb --args $$t ;\
	done
 
valgrind : all $(TEST_T)
	set -ex ;\
	for t in $(TESTS_T) ;\
	do \
	  time valgrind $$t || gdb --args $$t ;\
	done
