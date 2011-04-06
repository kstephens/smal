CFLAGS = -O2 -g -Wall -Werror

all : gc.s gc_unit_test

gc.s : gc.c sgc.h
	$(CC) $(CFLAGS) -S -o $@ $<

gc_unit_test : gc_unit_test.c gc.c sgc.h
	$(CC) $(CFLAGS) -DGC_UNIT_TEST=1 -o $@ gc_unit_test.c gc.c

clean :
	rm -f gc.s gc_unit_test


