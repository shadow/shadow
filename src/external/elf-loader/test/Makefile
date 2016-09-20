SRCDIR= $(abspath $(dir $(firstword $(MAKEFILE_LIST))))/
BLDDIR= $(PWD)/
VPATH=$(SRCDIR)
CFLAGS=-g3 -Wall -Werror
CXXFLAGS=$(CFLAGS)
LDFLAGS=-Wl,--warn-unresolved-symbols -L. -Wl,--no-as-needed
LINKER=$(CC)
ARCH?=$(shell uname -m)
TMP_ARCH=$(ARCH)
ifeq ($(TMP_ARCH),i586)
TMP_ARCH=i686
else ifeq ($(TMP_ARCH),i386)
TMP_ARCH=i686
endif
MACHINE_MAKEFILE=Makefile.$(TMP_ARCH)


include $(SRCDIR)$(MACHINE_MAKEFILE)

TESTS=test0 test0_1 test0_2 test1 test2 test3 test4 test5 test6 test7 test8 test8_5 test9 test10 test11 test15 test12 test13 test14 test16 test17 test18 test19 test21 test20 $(TEST64) test23 test24 test25 test26
TARGETS=hello libr.so libq.so libp.so libn.so libo.o libo.so circular-dep libl.so libk.so libj.so libi.so libh.so libg.so libf.so libe.so libd.so libb.so liba.so libefl.so $(LIB64) \
 $(TESTS) $(addsuffix -ldso,$(TESTS))

all: $(TARGETS)

OUTPUT_DIR=output
generate-ref: $(addsuffix .ref,$(addprefix $(OUTPUT_DIR)/,$(TESTS)))
$(OUTPUT_DIR)/test%.ref: test%
	LD_LIBRARY_PATH=. ./$^ > $@
diff: $(addprefix diff-,$(TESTS))
update: $(addprefix update-,$(TESTS))
run: $(TARGETS)
	@PYTHONPATH=./ python testjunit.py $(addprefix ,$(TESTS))
run-valgrind: $(TARGETS) $(addprefix run-valgrind-,$(TESTS))
test%-ldso: test%
	@cp $^ $@
	@../elfedit $@ ../ldso
run-valgrind-test%: test%-ldso FORCE
	@-LD_LIBRARY_PATH=.:../ ./run-valgrind.py ./$<; \
		if test $$? -eq 0; then 						\
			echo "PASS $@"; 						\
		else 									\
			echo "FAIL $@ -- LD_LIBRARY_PATH=.:../ ./run-valgrind.py ./$<"; \
		fi
run-test%: test%-ldso $(OUTPUT_DIR) FORCE
	@-mkdir -p $(OUTPUT_DIR) 2>/dev/null
	@-$(TEST_PRE_COMMAND)
	@-LD_LIBRARY_PATH=.:../ ./$< > $(OUTPUT_DIR)/test$* 2>/dev/null || true;
	@-diff -q $(OUTPUT_DIR)/test$* $(SRCDIR)$(OUTPUT_DIR)/test$*.ref > /dev/null; \
		if test $$? -eq 0; then 				\
			echo "PASS $@"; 				\
		else 							\
			echo "FAIL $@  -- LD_LIBRARY_PATH=.:../ ./$<"; 	\
		fi
$(OUTPUT_DIR):
	mkdir -p $@ 2>/dev/null
diff-test%:
	@-diff -u $(OUTPUT_DIR)/test$*.ref $(OUTPUT_DIR)/test$*
update-test%: 
	@-cp $(OUTPUT_DIR)/test$* $(OUTPUT_DIR)/test$*.ref

FORCE:
.SECONDARY:




lib%.o: lib%.c
	$(CC) $(CFLAGS) -fpic -c -o $@ $<
lib%.o: lib%.cc
	$(CXX) $(CXXFLAGS) -fpic -c -o $@ $<
lib%.so:lib%.o
	$(LINKER) $^ $(LDFLAGS) -shared -o $@ 

test%.o: test%.c
	$(CC) $(CFLAGS) -c -o $@ $<
test%.o: test%.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $<
test%: test%.o
	$(LINKER) $< $(LDFLAGS) -ldl -o $@

test7.o: CFLAGS+=-Wno-unused-but-set-variable

circular-dep:
	$(CC) $(LDFLAGS) -shared -ldl -ln -o libo.so libo.o
	$(CC) $(LDFLAGS) -shared -ldl -lo -o libn.so libn.o

liba.so: LDFLAGS+=-lb
libb.so: LDFLAGS+=-ld -le
libd.so: LDFLAGS+=-lf
libe.so: LDFLAGS+=-lf
libefl.so: LDFLAGS+=-lf -ll
libg.so: LDFLAGS+=-lh
libj.so: LDFLAGS+=-lk
libk.so: LDFLAGS+=-ll
libp.so: LDFLAGS+=-lq -nostdlib
libq.so: LDFLAGS+=-nostdlib
lb22.o: lb22.c
	$(CC) $(CFLAGS) -mcmodel=large -c -o $@ $^
lb22.so: lb22.o
	$(CC) $(LDFLAGS) -shared -o $@ $^
test0: LDFLAGS+=-la
test0_1: LDFLAGS+=-lb -la
test8_5: LDFLAGS+=-lpthread
test9: LDFLAGS+=-lpthread
test10: LDFLAGS+=-lpthread
test11:
test12: LDFLAGS+=-lpthread
test13: LDFLAGS+=-lf
test14: LDFLAGS+=-ln
test16: LINKER=$(CXX)
test19: LDFLAGS+=-lf
test20.o: CFLAGS+=-fpic
test20: LDFLAGS+=-pie
test21: LINKER=$(CXX)
run-test12: TEST_PRE_COMMAND=rm libl.so
test24: LDFLAGS+=-lpthread
test25: LDFLAGS+=-lpthread
test26: LDFLAGS+=-lpthread


clean:
	-rm -f *.o *~ 2>/dev/null
	-rm -f $(TARGETS) 2>/dev/null
	-rm -f $(addprefix $(OUTPUT_DIR)/,$(TESTS)) 2>/dev/null
	-rm -f $(addsuffix -ldso,$(TESTS)) 2>/dev/null
