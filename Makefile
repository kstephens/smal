# SMAL
# Copyright (c) 2011 Kurt A. Stephens

UNAME_S:=$(shell uname -s 2>/dev/null)#

INC_DIR=include/#

CFLAGS_OPT = -O3 #
#CFLAGS_OPT = -fast #
#CFLAGS_OPT = -O2 #

# CFLAGS_OPT = #
ifneq ($(ENABLE_PROF),)
CFLAGS_PROF = -pg -DSMAL_PROF #
else
CFLAGS_PROF = #
endif
ifeq ($(ENABLE_ASSERT),0)
CFLAGS_ASSERT += -DNASSERT=1 #
CFLAGS_ASSERT += -DSMAL_NASSERT=1 #
else
CFLAGS_ASSERT += -DSMAL_ASSERT=1 #
CFLAGS_ASSERT += #
endif
ifneq ($(ENABLE_DEBUG),)
CFLAGS_DEBUG = -DSMAL_DEBUG=1 #
else
CFLAGS_DEBUG = #
endif
# ENABLE_PTHREAD=
ifneq ($(ENABLE_PTHREAD),)
CFLAGS_THREAD = -DSMAL_PTHREAD=1 #
LIBS_THREAD += -lpthread
else
CFLAGS_THREAD = #
LIBS_THREAD = #
endif

CFLAGS_MARK_QUEUE = #
# CFLAGS_MARK_QUEUE = -DSMALL_MARK_QUEUE=1 #

# CFLAGS_THREAD += -DSMAL_THREAD_MUTEX_DEBUG=3 #

CFLAGS_SMAL_OPTIONS = $(CFLAGS_DEBUG) $(CFLAGS_ASSERT) $(CFLAGS_THREAD) $(CFLAGS_MARK_QUEUE) $(CFLAGS_SEG_BUFFER) #
CFLAGS = $(CFLAGS_OPT) $(CFLAGS_PROF) -g -Wall -Werror $(CFLAGS_SMAL_OPTIONS) -I$(INC_DIR) -Isrc #

H_FILES := $(shell echo $(INC_DIR)/smal/*.h src/*.h) #
C_FILES := $(shell echo src/*.c) src/hash/voidP_voidP_Table.c src/hash/voidP_Table.c #
O_FILES := $(C_FILES:%.c=%.o) #
TESTS_C := $(shell echo t/*.c) #
TESTS_T := $(TESTS_C:%.c=%.t) #
SRC_LIB := src/libsmal.a #

T_LIBS := $(LIBS_THREAD) # 

all : $(SRC_LIB) $(TESTS_T)

GCC_ASM_OPTS=-Wa,-a,-ad
src/smal.s : src/smal.c $(H_FILES) Makefile
	$(CC) $(CFLAGS:-g=) -DSMAL_NASSERT=1 -DNASSERT=1 -DSMAL_DEBUG=0 -S -fverbose-asm -v -o $@ $<

src/hash/voidP_voidP_Table.o : src/hash/*.c src/hash/*.h src/hash/*.def
src/hash/voidP_Table.o : src/hash/*.c src/hash/*.h src/hash/*.def

$(SRC_LIB) : $(O_FILES)
	rm -f $@
	ar -rs $@ $(O_FILES)
	mkdir -p lib
	cp -p $@ lib/

$(O_FILES) : $(H_FILES)

t/%.t : t/%.c $(SRC_LIB) $(H_FILES)
	$(CC) $(CFLAGS:$(CFLAGS_OPT)=) -I./t -DSMAL_UNIT_TEST=1 -DSMAL_DEBUG=1 -o $@ $(@:%.t=%.c) $(SRC_LIB) $(T_LIBS) -lpthread

t/%.t : override CFLAGS_OPT=

doc/html : doc/html/index.html
doc/html/index.html : Makefile $(C_FILES) $(H_FILES) doc/doxygen.conf # doc/*.png
	mkdir -p doc/html
	-#cp -p doc/*.png doc/html/
	doxygen doc/doxygen.conf 2>&1 | tee doc/doxygen.log
	open doc/html/index.html

doc-clean :
	rm -rf doc/html/ doc/latex/

clean : doc-clean
	rm -rf *.s *.dSYM *.o *.a src/*.o src/*.a t/*.t t/*.dSYM lib/*

TEST_FAILED = /bin/false

test-interactive : all
	$(MAKE) TEST_FAILED='gdb --args' test

test : all $(TESTS_T)
	@set -e; for t in $(TESTS_T) ;\
	do \
	  echo "$$t:" ;\
	  time $$t >$$t.out 2>&1 || $(TEST_FAILED) $$t ;\
	done

valgrind : all $(TEST_T)
	set -ex ;\
	for t in $(TESTS_T) ;\
	do \
	  time valgrind $$t || gdb --args $$t ;\
	done

threaded:
	ENABLE_PTHREAD=1 make clean all

single:
	make clean all

threaded-vs-single:
	make threaded > /dev/null
	(time t/stress_test_2.t 2>&1) | grep 'real'
	make single > /dev/null
	(time t/stress_test_2.t 2>&1) | grep 'real'

mark-queue:
	make clean all CFLAGS_MARK_QUEUE='-DSMAL_MARK_QUEUE=1'

non-mark-queue:
	make clean all CFLAGS_MARK_QUEUE=''

mark-queue-vs-non:
	make mark-queue > /dev/null
	(time t/stress_test_2.t 2>&1) | grep 'real'
	make non-mark-queue > /dev/null
	(time t/stress_test_2.t 2>&1) | grep 'real'

seg-buffer:
	make clean all CFLAGS_SEG_BUFFER='-DSMAL_SEGREGATE_BUFFER_FROM_PAGE=1'

seg-buffer-vs-non:
	make single > /dev/null
	t/stress_test_2.t >/dev/null 2>&1
	-(time t/stress_test_2.t 2>&1) | grep 'real'
	make seg-buffer > /dev/null
	t/stress_test_2.t >/dev/null 2>&1
	-(time t/stress_test_2.t 2>&1) | grep 'real'

both:
	(make threaded && $(cmd) && make single && $(cmd))

