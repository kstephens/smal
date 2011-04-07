
INC_DIR=include/#

CFLAGS = -O2 -g -Wall -Werror -I$(INC_DIR) #

INCLUDES = $(INC_DIR)/smal/*.h #
CFILES = *.c src/*.c

all : t/smal_test_1.t

smal.s : smal.c $(INCLUDES)
	$(CC) $(CFLAGS) -S -o $@ $<

t/smal_test_1.t : t/smal_test_1.c $(CFILES) $(INCLUDES)
	$(CC) $(CFLAGS) -DSMAL_UNIT_TEST=1 -DSMAL_DEBUG=1 -o $@ $(@:.t=.c) $(CFILES)

clean :
	rm -rf *.s *.dSYM *.o *.a t/*.t t/*.dSYM

test : all t/*.t
	set -ex ;\
	for t in t/*.t ;\
	do \
	  $$t || gdb --args $$t ;\
	done
 